export module games.generalszh.presentation.interaction.algorithms.selection_setup;
import std;

export import games.generalszh.presentation.interaction.resources.interaction_resources;
export import games.generalszh.session.session_view;
import Engine.Core.Math.FixedPresentation;
import games.generalszh.content.crates.crate_content;
import games.generalszh.content.combat.loadout_content;
import games.generalszh.content.control_bar.command_catalog;
import games.generalszh.gameplay.objects.resources.object_templates;

// The selection catalog, filled for the definitions the session has taken on
// (the definitions it knows grow as objects of new kinds appear): the kinds
// selection cares about, and a pick sphere from the geometry (the bounding
// sphere, centred half way up a box or cylinder); what the context commands ask
// of each (its contain module, collides and command set's context buttons); and
// the simulation's tick.
export namespace generalszh::presentation
{
namespace selection_setup_detail
{
// The ContextPower of a SpecialPowerType.
inline ContextPower ContextPowerOf(std::string_view type)
{
	if (type == "SPECIAL_INFANTRY_CAPTURE_BUILDING")
		return ContextPower::InfantryCapture;
	if (type == "SPECIAL_BLACKLOTUS_CAPTURE_BUILDING")
		return ContextPower::BlackLotusCapture;
	if (type == "SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK")
		return ContextPower::DisableVehicleHack;
	if (type == "SPECIAL_BLACKLOTUS_STEAL_CASH_HACK")
		return ContextPower::StealCashHack;
	if (type == "SPECIAL_HACKER_DISABLE_BUILDING")
		return ContextPower::DisableBuildingHack;
	return ContextPower::None;
}

// A command set's context buttons (its GUI_COMMAND_SPECIAL_POWER buttons whose power a context command uses, in slot
// order), kept once per set name; NoCarBomber for no set of that name.
inline std::uint32_t KnowCommandSet(SelectionCatalog &catalog, const content::GameContent &content, std::string_view name)
{
	for (std::size_t index = 0; index < catalog.contextSetNames.size(); ++index)
		if (catalog.contextSetNames[index] == name)
			return static_cast<std::uint32_t>(index);
	const content::CommandSetContent *set = content.commands.Set(name);
	if (set == nullptr)
		return SelectionLook::NoCarBomber;
	std::vector<ContextButton> buttons;
	for (const std::string &slot : set->buttons)
	{
		const content::CommandButtonContent *button = slot.empty() ? nullptr : content.commands.Button(slot);
		// GUI_COMMAND_SPECIAL_POWER exactly (as INI names it, any case).
		if (button == nullptr || !std::ranges::equal(button->commandName, std::string_view{"SPECIAL_POWER"}, [](char a, char b) {
				return std::toupper(static_cast<unsigned char>(a)) == static_cast<unsigned char>(b);
			}))
			continue;
		const auto power = content.powers.Template(button->specialPower);
		if (!power)
			continue;
		if (const ContextPower kind = ContextPowerOf(content.powers.templates[*power].type); kind != ContextPower::None)
			buttons.push_back({kind, button->specialPower, button->options});
	}
	catalog.contextSets.push_back(std::move(buttons));
	catalog.contextSetNames.emplace_back(name);
	return static_cast<std::uint32_t>(catalog.contextSets.size() - 1);
}

// A module block's kinds list (AllowInsideKindOf / ForbidInsideKindOf); none given: nullopt.
inline std::optional<content::KindOfMask> KindsListed(const content::ModuleEntry &module, std::string_view key)
{
	const auto *node = module.block != nullptr ? module.block->Find(key) : nullptr;
	if (node == nullptr)
		return std::nullopt;
	content::KindOfMask mask{};
	for (const std::string_view kind : node->values)
		if (const std::size_t bit = content::KindOfBit(kind); bit < content::KindOfNames.size())
			mask[bit / 64] |= std::uint64_t{1} << (bit % 64);
	return mask;
}

// Its contain module (the first), as canEnterObject asks it.
inline void KnowContain(SelectionLook &look, const content::ObjectDefinition &object)
{
	constexpr std::pair<std::string_view, ContainKind> kinds[] = {{"OpenContain", ContainKind::Open}, {"ParachuteContain", ContainKind::Open},
		{"TransportContain", ContainKind::Transport}, {"InternetHackContain", ContainKind::Transport}, {"HelixContain", ContainKind::Helix},
		{"OverlordContain", ContainKind::Overlord}, {"RiderChangeContain", ContainKind::RiderChange}, {"MobNexusContain", ContainKind::MobNexus},
		{"GarrisonContain", ContainKind::Garrison}, {"HealContain", ContainKind::Heal}, {"TunnelContain", ContainKind::Tunnel},
		{"CaveContain", ContainKind::Cave}};
	for (const content::ModuleEntry &module : object.modules)
		for (const auto &[type, kind] : kinds)
			if (module.type == type)
			{
				look.contain = kind;
				if (const auto allowed = KindsListed(module, "AllowInsideKindOf"))
				{
					look.anyInside = false;
					look.allowInside = *allowed;
				}
				look.forbidInside = KindsListed(module, "ForbidInsideKindOf").value_or(content::KindOfMask{});
				const auto yes = [&](std::string_view key) {
					const auto *node = module.block != nullptr ? module.block->Find(key) : nullptr;
					return node == nullptr || (!node->Value().empty() && (node->Value()[0] == 'Y' || node->Value()[0] == 'y'));
				};
				look.alliesInside = yes("AllowAlliesInside");
				look.enemiesInside = yes("AllowEnemiesInside");
				look.neutralInside = yes("AllowNeutralInside");
				return;
			}
}
}

inline void KnowSelectables(SelectionCatalog &catalog, const session::SessionView &game)
{
	using namespace selection_setup_detail;
	const content::GameContent &content = game.Content();
	catalog.tick = game.CurrentTick();
	if (catalog.powers.size() < content.powers.templates.size())
		for (std::size_t index = catalog.powers.size(); index < content.powers.templates.size(); ++index)
			catalog.powers.push_back(ContextPowerOf(content.powers.templates[index].type));
	// The command sets upgrades swapped in so far (ObjectTemplates::CommandSet ids).
	if (const auto *templates = game.World().FindResource<gameplay::ObjectTemplates>())
		for (std::string_view name = templates->CommandSetName(static_cast<std::uint32_t>(catalog.overrideSets.size())); !name.empty();
			 name = templates->CommandSetName(static_cast<std::uint32_t>(catalog.overrideSets.size())))
			catalog.overrideSets.push_back(KnowCommandSet(catalog, content, name));
	const auto count = static_cast<std::uint32_t>(game.DefinitionCount());
	for (std::uint32_t index = static_cast<std::uint32_t>(catalog.byDefinition.size()); index < count; ++index)
	{
		const content::ObjectDefinition &object = game.Definition(index);
		SelectionLook look;
		const std::pair<std::string_view, std::uint16_t> kinds[] = {{"SELECTABLE", select_kind::Selectable}, {"ALWAYS_SELECTABLE", select_kind::AlwaysSelectable},
			{"FORCEATTACKABLE", select_kind::ForceAttackable}, {"STRUCTURE", select_kind::Structure}, {"INFANTRY", select_kind::Infantry},
			{"CRATE", select_kind::Crate}, {"MINE", select_kind::Mine}, {"SHRUBBERY", select_kind::Shrubbery}, {"IGNORED_IN_GUI", select_kind::IgnoredInGui},
			{"VEHICLE", select_kind::Vehicle}, {"AIRCRAFT", select_kind::Aircraft}, {"REPAIR_PAD", select_kind::RepairPad}, {"CLICK_THROUGH", select_kind::ClickThrough}, {"BRIDGE", select_kind::Bridge},
			{"BRIDGE_TOWER", select_kind::BridgeTower}};
		for (const auto &[name, bit] : kinds)
			if (object.Is(name))
				look.kinds |= bit;
		look.radius = std::max(1.0f, Engine::Math::ToFloat(content::BoundingSphereRadius(object.geometry)));
		look.center = object.geometry.shape == content::GeometryShape::Sphere ? 0.0f : Engine::Math::ToFloat(object.geometry.height) * 0.5f;
		const float major = Engine::Math::ToFloat(object.geometry.majorRadius), minor = Engine::Math::ToFloat(object.geometry.minorRadius);
		look.top = object.geometry.shape == content::GeometryShape::Sphere ? major : Engine::Math::ToFloat(object.geometry.height);
		look.healthBoxWidth = (look.kinds & select_kind::IgnoredInGui) != 0 ? 0.0f : std::max(20.0f, std::clamp(major + minor, 20.0f, 150.0f) * 2.0f);
		// ConvertToCarBombCrateCollide, and ConvertToCarBombCrateCollide::isValidToExecute's checks on the car's template.
		if (const auto collide = content::ReadCrateCollide(object); collide && collide->kind == content::CrateKind::CarBomb)
		{
			look.carBomber = static_cast<std::uint32_t>(catalog.carBombers.size());
			catalog.carBombers.push_back({collide->required, collide->forbidden});
		}
		if (const auto collide = content::ReadCrateCollide(object); collide && collide->kind == content::CrateKind::Hijack)
		{
			look.hijacker = static_cast<std::uint32_t>(catalog.hijackers.size());
			catalog.hijackers.push_back({collide->required, collide->forbidden});
		}
		const std::uint32_t carBomb = content::SetFlag(content::WeaponSetFlagNames, "CARBOMB");
		look.carBombable = game.DefinitionHasAi(index) && !object.Is("AIRCRAFT") && !object.Is("BOAT") &&
			std::ranges::any_of(content::ReadObjectLoadout(object).weaponSets, [&](const content::WeaponSetContent &set) { return (set.conditions & carBomb) != 0; });
		look.hasAi = game.DefinitionHasAi(index);
		look.transportSlots = static_cast<std::uint32_t>(std::max(object.transportSlots, 0));
		KnowContain(look, object);
		look.chinook = std::ranges::any_of(object.modules, [](const content::ModuleEntry &module) { return module.type == "ChinookAIUpdate"; });
		look.salvageCrate = std::ranges::any_of(object.modules, [](const content::ModuleEntry &module) { return module.type == "SalvageCrateCollide"; });
		look.saboteur = static_cast<std::uint32_t>(catalog.saboteurs.size());
		for (const content::SabotageCollideContent &sabotage : content::ReadSabotageCollides(object))
			catalog.saboteurs.push_back({sabotage.kind, sabotage.crate.required, sabotage.crate.forbidden, sabotage.crate.buildingPickup, sabotage.crate.forbidOwner});
		look.saboteurCount = static_cast<std::uint32_t>(catalog.saboteurs.size()) - look.saboteur;
		if (!object.commandSet.empty())
			look.commandSet = KnowCommandSet(catalog, content, object.commandSet);
		catalog.kinds.push_back(object.kinds);
		catalog.byDefinition.push_back(look);
	}
}
}
