export module games.generalszh.content.combat.cooldown_creation_content;
import std;

export import games.generalszh.content.upgrades.upgrade_content;

// FireOCLAfterWeaponCooldownUpdate modules (the Toxin Tractor's lingering poison): the weapon slot watched
// (WeaponSlot), the creation list (OCL), the shots it takes (MinShotsToCreateOCL), the list's lifetime per second of
// firing (OCLLifetimePerSecond, ms) and its cap (OCLLifetimeMaxCap: ms up to whole ticks), and its upgrade mux
// (TriggeredBy, ConflictsWith, RequiresAllTriggers).
export namespace generalszh::content
{
struct CooldownCreationModule
{
	std::uint32_t slot{0};
	std::string creation;
	std::uint32_t minShots{1};
	std::uint32_t lifetimePerSecond{1000};
	std::uint64_t maxTicks{1000};
	engine::gameplay::UpgradeMask activation;
	engine::gameplay::UpgradeMask conflicting;
	bool requiresAll{false};
};

inline std::vector<CooldownCreationModule> ReadCooldownCreations(const ObjectDefinition &object, const UpgradeCatalog &upgrades, std::uint64_t ticksPerSecond)
{
	using upgrade_detail::SameText;
	std::vector<CooldownCreationModule> out;
	const auto whole = [](const engine::config::Node *node, std::int64_t fallback) {
		return node != nullptr ? engine::config::values::ParseInt(node->Value()).value_or(fallback) : fallback;
	};
	const auto mask = [&](const engine::config::Node *node) {
		engine::gameplay::UpgradeMask bits;
		if (node != nullptr)
			for (const std::string_view name : node->values)
				if (const auto bit = upgrades.Find(name))
					bits.Set(*bit);
		return bits;
	};
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "FireOCLAfterWeaponCooldownUpdate")
			continue;
		CooldownCreationModule data;
		if (const auto *slot = module.block->Find("WeaponSlot"); slot != nullptr && !slot->values.empty())
			data.slot = SameText(slot->Value(), "SECONDARY") ? 1u : SameText(slot->Value(), "TERTIARY") ? 2u : 0u;
		if (const auto *list = module.block->Find("OCL"); list != nullptr && !list->values.empty())
			data.creation = std::string(list->Value());
		data.minShots = static_cast<std::uint32_t>(std::max<std::int64_t>(0, whole(module.block->Find("MinShotsToCreateOCL"), 1)));
		data.lifetimePerSecond = static_cast<std::uint32_t>(std::max<std::int64_t>(0, whole(module.block->Find("OCLLifetimePerSecond"), 1000)));
		// OCLLifetimeMaxCap: parseDurationUnsignedInt (default 1000 frames).
		if (const auto *cap = module.block->Find("OCLLifetimeMaxCap"))
		{
			const std::int64_t ms = std::max<std::int64_t>(0, whole(cap, 0));
			data.maxTicks = static_cast<std::uint64_t>((ms * static_cast<std::int64_t>(ticksPerSecond) + 999) / 1000);
		}
		data.activation = mask(module.block->Find("TriggeredBy"));
		data.conflicting = mask(module.block->Find("ConflictsWith"));
		if (const auto *all = module.block->Find("RequiresAllTriggers"))
			data.requiresAll = engine::config::values::ParseBool(all->Value()).value_or(false);
		out.push_back(std::move(data));
	}
	return out;
}
}
