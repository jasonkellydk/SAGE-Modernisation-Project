export module engine.gameplay.rts.combat.systems.weapon_bonus_retime_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.systems.weapon_system;
import Engine.Core.Math.FixedRandom;

// Weapon::onWeaponBonusChange, chunk-parallel, at the end of the tick (after
// whatever changed them: drones, promotions, upgrades; a change the original
// made after the unit's shot that frame): in the tick an object's weapon
// bonus conditions changed, each of its weapons that is reloading its clip or
// waiting between shots starts that wait over from now, as long as the new
// bonus makes it (a fresh random shot delay divided by the rate-of-fire
// bonus, or the clip reload divided by it). Ready weapons and ones out of
// ammo are left alone.
export namespace engine::gameplay
{
namespace retime_detail
{
template<typename Slot>
bool Retime(Slot &slot, std::uint32_t weapon, const WeaponCatalog &weapons, std::uint32_t conditions, std::uint64_t tick, Engine::Math::RandomStream &random)
{
	if (weapon == WeaponCatalog::None || slot.readyTick == OutOfAmmo || tick >= slot.readyTick)
		return false;
	const WeaponDefinition &definition = weapons.At(weapon);
	const WeaponBonus bonus = weapons.Bonus(definition, conditions);
	if (slot.reloading)
	{
		slot.readyTick = tick + std::max<std::uint64_t>(BonusDelay(definition.clipReload, bonus), 1);
		return true;
	}
	const std::uint64_t spread = definition.delayMax > definition.delayMin ? definition.delayMax - definition.delayMin : 0;
	const std::uint64_t delay = definition.delayMin +
		(spread == 0 ? 0 : static_cast<std::uint64_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(spread))));
	slot.readyTick = tick + std::max<std::uint64_t>(BonusDelay(delay, bonus), 1);
	return true;
}
}

struct WeaponBonusRetimeSystem
{
	using Query = ecs::Query<ecs::Read<WeaponBonusConditions>, ecs::Write<Armament>, ecs::OptionalWrite<WeaponSlots>>;
	using Resources = ecs::Resources<ecs::Read<RandomSeed>, ecs::Read<WeaponCatalog>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0xB0A5u;
		const std::uint64_t tick = context.Tick();
		const auto conditions = chunk.Get<WeaponBonusConditions>();
		auto armaments = chunk.Get<Armament>();
		auto sets = chunk.Get<WeaponSlots>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < conditions.size(); ++row)
		{
			if (conditions[row].changedTick != tick)
				continue;
			auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation});
			Armament &armament = armaments[row];
			if (!sets.empty() && sets[row].sharedReload != 0)
			{
				// Shared (WeaponSet::weaponSetOnWeaponBonusChange, PRIMARY to TERTIARY): each weapon re-timed holds them all,
				// reloading (retail sets RELOADING_CLIP whichever wait it was), so the next is re-timed from that.
				WeaponSlots &set = sets[row];
				StoreSlot(set.slots[set.current], armament);
				for (std::size_t slot = 0; slot < WeaponSlotCount; ++slot)
					if (retime_detail::Retime(set.slots[slot], set.slots[slot].weapon, weapons, conditions[row].Effective(), tick, random))
						ShareReloadTime(set, set.slots[slot].readyTick, true);
				armament.readyTick = set.slots[set.current].readyTick;
				armament.reloading = set.slots[set.current].reloading;
				continue;
			}
			retime_detail::Retime(armament, armament.weapon, weapons, conditions[row].Effective(), tick, random);
			if (sets.empty())
				continue;
			// The other slots (the one in use lives in the Armament).
			WeaponSlots &set = sets[row];
			for (std::size_t slot = 0; slot < WeaponSlotCount; ++slot)
				if (slot != set.current)
					retime_detail::Retime(set.slots[slot], set.slots[slot].weapon, weapons, conditions[row].Effective(), tick, random);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::WeaponBonusRetimeSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.weapon_bonus_retime";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
