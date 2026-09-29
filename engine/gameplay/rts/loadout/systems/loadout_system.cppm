export module engine.gameplay.rts.loadout.systems.loadout_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.loadout.components.loadout;
export import engine.gameplay.rts.loadout.resources.loadout_catalog;
export import engine.gameplay.rts.loadout.algorithms.equip;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.rts.veterancy.components.experience;

// Keeps every entity in the weapon and armor sets its flags pick, in
// parallel per chunk: its veterancy level's weapon flag first (the original's
// onVeterancyLevelChanged), then, when the best weapon set is another,
// WeaponSet::updateWeaponSet (fresh weapons), and when the best armor set is
// another, its armor (ActiveBody::validateArmorAndDamageFX).
export namespace engine::gameplay
{
struct LoadoutSystem
{
	using Query = ecs::Query<ecs::Write<Loadout>, ecs::Read<DefinitionRef>, ecs::Optional<Experience>, ecs::OptionalWrite<Armament>,
		ecs::OptionalWrite<WeaponSlots>, ecs::OptionalWrite<Health>>;
	using Resources = ecs::Resources<ecs::Read<LoadoutCatalog>, ecs::Read<WeaponCatalog>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const LoadoutCatalog &catalog = context.Read<LoadoutCatalog>();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		auto loadouts = chunk.Get<Loadout>();
		const auto definitions = chunk.Get<DefinitionRef>();
		const auto experiences = chunk.Get<Experience>();
		auto armaments = chunk.Get<Armament>();
		auto slotSets = chunk.Get<WeaponSlots>();
		auto healths = chunk.Get<Health>();
		const auto entities = chunk.Entities();
		const std::uint32_t levelMask = catalog.LevelMask();
		for (std::size_t row = 0; row < loadouts.size(); ++row)
		{
			Loadout &loadout = loadouts[row];
			const DefinitionLoadout *sets = catalog.Of(definitions[row].index);
			if (sets == nullptr)
				continue;
			if (!experiences.empty() && levelMask != 0)
				loadout.weaponFlags = (loadout.weaponFlags & ~levelMask) | catalog.levelWeaponFlags[experiences[row].level & 3u];
			if (!sets->weaponSets.empty() && !armaments.empty())
			{
				const std::uint16_t best = BestSet(sets->weaponSets, loadout.weaponFlags);
				if (best != loadout.weaponSet)
				{
					if (loadout.weaponSet != Loadout::Unpicked)
						if (auto added = EquipWeapons(armaments[row], slotSets.empty() ? nullptr : &slotSets[row], sets->weaponSets[best].weapons, weapons,
								sets->weaponSets[best].lockShared))
							context.Commands().Add<WeaponSlots>(entities[row], *added);
					loadout.weaponSet = best;
				}
			}
			if (!sets->armorSets.empty() && !healths.empty())
			{
				const std::uint16_t best = BestSet(sets->armorSets, loadout.armorFlags);
				if (best != loadout.armorSet)
				{
					healths[row].armor = sets->armorSets[best].armor;
					loadout.armorSet = best;
				}
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::LoadoutSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.loadout";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
