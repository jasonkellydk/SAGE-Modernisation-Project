export module engine.gameplay.rts.loadout.resources.loadout_catalog;
import std;

import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.ecs.core.entity;
export import engine.gameplay.common.weapons.components.weapon_slots;

// Per definition (DefinitionRef::index): its weapon sets (the condition bits
// each is for, and its PRIMARY, SECONDARY, TERTIARY weapons: WeaponCatalog
// indices, WeaponCatalog::None for an empty slot) and its armor sets (their
// conditions and armor: ArmorCatalog indices). The game's weapon flags a
// veterancy level brings (none, VETERAN, ELITE, HERO), replacing each other.
// The best set for a set of flags is the original's SparseMatchFinder: the one
// matching the most flags, then the one with the fewest flags not set; ties
// to the first.
export namespace engine::gameplay
{
struct WeaponSetEntry
{
	std::uint32_t conditions{0};
	std::array<std::uint32_t, 3> weapons{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
	// WeaponLockSharedAcrossSets: taking this set keeps a weapon lock (else it is released).
	bool lockShared{false};
	// Its AutoChooseSources and PreferredAgainst per slot.
	SlotRules rules;
};

struct ArmorSetEntry
{
	std::uint32_t conditions{0};
	std::uint32_t armor{0};
};

struct DefinitionLoadout
{
	std::vector<WeaponSetEntry> weaponSets;
	std::vector<ArmorSetEntry> armorSets;
};

// WeaponSet::updateWeaponSet on something without weapons yet: those whose flags now pick a set with a PRIMARY weapon,
// for the game to arm (as it arms what it makes).
struct LoadoutArmings : ecs::ChunkOutputs<ecs::Entity>
{
};

struct LoadoutCatalog
{
	std::vector<DefinitionLoadout> byDefinition;
	std::array<std::uint32_t, 4> levelWeaponFlags{};

	const DefinitionLoadout *Of(std::uint32_t definition) const noexcept
	{
		return definition < byDefinition.size() ? &byDefinition[definition] : nullptr;
	}
	std::uint32_t LevelMask() const noexcept { return levelWeaponFlags[0] | levelWeaponFlags[1] | levelWeaponFlags[2] | levelWeaponFlags[3]; }
};

template<typename Entry>
std::uint16_t BestSet(const std::vector<Entry> &sets, std::uint32_t flags) noexcept
{
	std::uint16_t best = 0xFFFFu;
	int bestMatch = -1, bestExtra = 0;
	for (std::size_t index = 0; index < sets.size() && index < 0xFFFFu; ++index)
	{
		const int match = std::popcount(sets[index].conditions & flags);
		const int extra = std::popcount(sets[index].conditions & ~flags);
		if (match > bestMatch || (match == bestMatch && extra < bestExtra))
		{
			best = static_cast<std::uint16_t>(index);
			bestMatch = match;
			bestExtra = extra;
		}
	}
	return best;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::LoadoutArmings>
{
	static constexpr std::string_view StableName = "engine.gameplay.loadout_armings";
};

template<>
struct ResourceTraits<engine::gameplay::LoadoutCatalog>
{
	static constexpr std::string_view StableName = "engine.gameplay.loadout_catalog";
};
}
