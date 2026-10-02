export module games.generalszh.content.harvesting.harvest_content;
import std;

export import games.generalszh.content.objects.object_definition;
export import games.generalszh.content.models.model_rigs;
export import engine.gameplay.rts.harvesting.components.harvester;
export import engine.gameplay.rts.harvesting.components.resource_store;
import games.generalszh.content.objects.model_draw;

// Zero Hour's supply economy as content:
//   a supply truck (SupplyTruckAIUpdate: MaxBoxes, SupplyCenterActionDelay,
//   SupplyWarehouseActionDelay in milliseconds up to whole ticks,
//   SupplyWarehouseScanDistance, default 100);
//   a dock (DockUpdate: NumberApproachPositions, -1 for any number;
//   AllowsPassthrough, default yes), with its points from its default
//   model's bones in its own frame (DockStart, DockAction, DockEnd,
//   DockWaiting01...; none for KINDOF_IGNORE_DOCKING_BONES), whether it is a
//   warehouse (SupplyWarehouseDockUpdate: StartingBoxes, default 1;
//   DeleteWhenEmpty) or a supply centre (SupplyCenterDockUpdate), and
//   whether it draws trucks in (KINDOF_SUPPLY_SOURCE).
export namespace generalszh::content
{
struct DockLayout
{
	std::vector<RestBone> approach; // the DockWaiting bones found
	std::optional<RestBone> enter;  // none: boneless
	RestBone action;
	RestBone exit;
	std::int32_t approachCount{0}; // NumberApproachPositions
	bool dynamic{false};
	bool passthrough{true};
	bool drawsIn{false};
	bool warehouse{false};
	bool center{false};
	bool repair{false};                // RepairDockUpdate
	bool railed{false};                // RailedTransportDockUpdate: a ferry's (its points move with it)
	Engine::Math::Fixed fullHealTicks; // RepairDockUpdate TimeForFullHeal (parseDurationReal: frames, not rounded)
	engine::gameplay::ResourceStore store;
	std::uint32_t grantStealthTicks{0}; // SupplyCenterDockUpdate GrantTemporaryStealth (parseDurationUnsignedInt)
};

inline const ModuleEntry *FindBehavior(const ObjectDefinition &object, std::span<const std::string_view> types)
{
	for (const ModuleEntry &module : object.modules)
		if (module.slot == ModuleSlot::Behavior && module.block != nullptr && std::find(types.begin(), types.end(), module.type) != types.end())
			return &module;
	return nullptr;
}

std::optional<engine::gameplay::Harvester> ReadObjectHarvester(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	// SupplyTruckAIUpdate, and the AIs that carry supplies the same way (WorkerAIUpdate, ChinookAIUpdate).
	static constexpr std::string_view Types[] = {"SupplyTruckAIUpdate", "WorkerAIUpdate", "ChinookAIUpdate"};
	const ModuleEntry *module = FindBehavior(object, Types);
	if (module == nullptr)
		return std::nullopt;
	engine::config::Diagnostics diagnostics;
	engine::config::BindContext bind{diagnostics, step};
	engine::gameplay::Harvester harvester;
	harvester.scanDistance = Engine::Math::Fixed::FromInt(100);
	if (const auto *node = module->block->Find("MaxBoxes"))
		harvester.maxBoxes = static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::ReadInt(*node, bind).value_or(0)));
	if (const auto *node = module->block->Find("SupplyCenterActionDelay"))
		harvester.depotDelay = engine::config::ReadDurationTicks(*node, bind).value_or(0);
	if (const auto *node = module->block->Find("SupplyWarehouseActionDelay"))
		harvester.storeDelay = engine::config::ReadDurationTicks(*node, bind).value_or(0);
	if (const auto *node = module->block->Find("SupplyWarehouseScanDistance"))
		harvester.scanDistance = engine::config::ReadFixed(*node, bind).value_or(harvester.scanDistance);
	if (const auto *node = module->block->Find("UpgradedSupplyBoost"))
		harvester.boost = engine::config::ReadInt(*node, bind).value_or(0);
	return harvester;
}

// The upgrade its UpgradedSupplyBoost waits for (getUpgradedSupplyBoost): a worker's shoes, a Chinook's supply lines;
// a supply truck has none.
inline std::string_view SupplyBoostUpgrade(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
		if (module.slot == ModuleSlot::Behavior && module.type == "WorkerAIUpdate")
			return "Upgrade_GLAWorkerShoes";
		else if (module.slot == ModuleSlot::Behavior && module.type == "ChinookAIUpdate")
			return "Upgrade_AmericaSupplyLines";
	return {};
}

std::optional<DockLayout> ReadDockLayout(const ObjectDefinition &object, ModelRigs &rigs)
{
	static constexpr std::string_view Types[] = {"SupplyWarehouseDockUpdate", "SupplyCenterDockUpdate", "RepairDockUpdate", "RailedTransportDockUpdate"};
	const ModuleEntry *module = FindBehavior(object, Types);
	if (module == nullptr)
		return std::nullopt;
	engine::config::Diagnostics diagnostics;
	engine::config::BindContext bind{diagnostics, engine::time::FixedStep{30}};
	const engine::config::Node &block = *module->block;
	DockLayout layout;
	layout.warehouse = module->type == "SupplyWarehouseDockUpdate";
	layout.repair = module->type == "RepairDockUpdate";
	layout.railed = module->type == "RailedTransportDockUpdate";
	layout.center = !layout.warehouse && !layout.repair && !layout.railed;
	if (layout.center)
		if (const auto *node = block.Find("GrantTemporaryStealth"))
			layout.grantStealthTicks = static_cast<std::uint32_t>(engine::config::ReadDurationTicks(*node, bind).value_or(0));
	if (layout.repair)
	{
		// m_framesForFullHeal: 1 frame by default (no divide by zero).
		layout.fullHealTicks = Engine::Math::Fixed::One();
		if (const auto *node = block.Find("TimeForFullHeal"))
			if (const auto ms = engine::config::values::ParseFixed(node->Value()))
				layout.fullHealTicks = *ms * Engine::Math::Fixed::FromInt(30) / Engine::Math::Fixed::FromInt(1000);
	}
	if (const auto *node = block.Find("NumberApproachPositions"))
		layout.approachCount = static_cast<std::int32_t>(engine::config::ReadInt(*node, bind).value_or(0));
	layout.dynamic = layout.approachCount == -1; // DYNAMIC_APPROACH_VECTOR_FLAG
	if (const auto *node = block.Find("AllowsPassthrough"))
		layout.passthrough = engine::config::ReadBool(*node, bind).value_or(true);
	layout.drawsIn = object.Is("SUPPLY_SOURCE");
	if (layout.warehouse)
	{
		layout.store.boxes = 1;
		if (const auto *node = block.Find("StartingBoxes"))
			layout.store.boxes = static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::ReadInt(*node, bind).value_or(1)));
		layout.store.startingBoxes = layout.store.boxes;
		if (const auto *node = block.Find("DeleteWhenEmpty"))
			layout.store.deleteWhenEmpty = engine::config::ReadBool(*node, bind).value_or(false);
		// SupplyWarehouseCreate::onCreate: known to every player's gatherers.
		layout.store.listed = std::ranges::any_of(object.modules, [](const ModuleEntry &entry) { return entry.type == "SupplyWarehouseCreate"; });
	}
	if (object.Is("IGNORE_DOCKING_BONES"))
		return layout;
	const std::string model = DefaultModel(object).model;
	if (model.empty())
		return layout;
	layout.enter = rigs.Bone(model, "DockStart");
	layout.action = rigs.Bone(model, "DockAction").value_or(RestBone{});
	layout.exit = rigs.Bone(model, "DockEnd").value_or(RestBone{});
	// getPristineBonePositions("DockWaiting", 1, ...): DockWaiting01, 02, ... up to the count, while there.
	if (!layout.dynamic)
		for (std::int32_t index = 1; index <= layout.approachCount; ++index)
		{
			char name[16];
			std::snprintf(name, sizeof(name), "DockWaiting%02d", index);
			const auto bone = rigs.Bone(model, name);
			if (!bone)
				break;
			layout.approach.push_back(*bone);
		}
	return layout;
}
}
