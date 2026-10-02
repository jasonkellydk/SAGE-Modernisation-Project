export module engine.gameplay.rts.combat.systems.firing_tracker_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.firing_tracker;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.random.resources.random_seed;
import Engine.Core.Math.FixedRandom;

// FiringTracker::update, chunk-parallel each tick before the weapons fire (even disabled): idle past its
// AutoReloadWhenIdle, every weapon with a clip not full loads it at once (reloadAllAmmo(TRUE): loadAmmoNow); its looping
// fire sound stops
// once its time is up; not having fired past its coast (now past the cool-down tick), it cools down and looks again a
// second on (LOGICFRAMES_PER_SECOND: from MEAN or FAST it spins down, SLOW, then stops), its weapon's wait re-timed from
// now when that slows its rate of fire (Weapon::onWeaponBonusChange).
export namespace engine::gameplay
{
struct FiringTrackerSystem
{
	using Query = ecs::Query<ecs::Write<FiringTracker>, ecs::Write<Armament>, ecs::Optional<WeaponBonusConditions>, ecs::OptionalWrite<WeaponSlots>>;

	// Weapon::reloadWithBonus(loadInstantly): a clip not full (or emptied until it rearms at base) is full, ready now.
	static void LoadNow(std::uint64_t &readyTick, std::uint32_t &clip, bool &reloading, std::uint32_t clipSize, std::uint64_t tick) noexcept
	{
		if (clipSize == 0 || (readyTick != OutOfAmmo && (clip == 0 || clip >= clipSize)))
			return;
		clip = clipSize;
		readyTick = tick;
		reloading = true;
	}
	using Resources = ecs::Resources<ecs::Read<WeaponCatalog>, ecs::Read<RandomSeed>>;

	static constexpr std::uint64_t Second = 30; // LOGICFRAMES_PER_SECOND

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const std::uint64_t tick = context.Tick();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0xF17Au;
		auto trackers = chunk.Get<FiringTracker>();
		auto armaments = chunk.Get<Armament>();
		const auto bonusRows = chunk.Get<WeaponBonusConditions>();
		auto slotSets = chunk.Get<WeaponSlots>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < trackers.size(); ++row)
		{
			FiringTracker &tracker = trackers[row];
			if (tracker.reloadTick != 0 && tick >= tracker.reloadTick)
			{
				tracker.reloadTick = 0;
				Armament &armament = armaments[row];
				if (armament.weapon != WeaponCatalog::None)
					LoadNow(armament.readyTick, armament.clip, armament.reloading, weapons.At(armament.weapon).clipSize, tick);
				if (!slotSets.empty())
					for (std::size_t index = 0; index < slotSets[row].slots.size(); ++index)
					{
						WeaponSlot &slot = slotSets[row].slots[index];
						if (index == slotSets[row].current || slot.weapon == WeaponCatalog::None)
							continue;
						LoadNow(slot.readyTick, slot.clip, slot.reloading, weapons.At(slot.weapon).clipSize, tick);
					}
			}
			if (tracker.loopUntil != 0 && tick >= tracker.loopUntil)
				tracker.loopUntil = 0;
			if (tracker.cooldownTick == 0 || tick <= tracker.cooldownTick)
				continue;
			tracker.cooldownTick = tick + Second;
			const std::uint8_t level = tracker.level;
			CoolDown(tracker);
			Armament &armament = armaments[row];
			if (level == tracker.level || armament.weapon == WeaponCatalog::None || armament.readyTick == OutOfAmmo || armament.readyTick <= tick)
				continue;
			const WeaponDefinition &weapon = weapons.At(armament.weapon);
			const WeaponBonus bonus = weapons.Bonus(weapon, bonusRows.empty() ? 0u : bonusRows[row].Effective());
			if (armament.reloading)
				armament.readyTick = tick + BonusDelay(weapon.clipReload, bonus);
			else
			{
				auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation});
				const std::uint64_t spread = weapon.delayMax > weapon.delayMin ? weapon.delayMax - weapon.delayMin : 0;
				const std::uint64_t delay = weapon.delayMin +
					(spread == 0 ? 0 : static_cast<std::uint64_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(spread))));
				armament.readyTick = tick + BonusDelay(delay, bonus);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::FiringTrackerSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.firing_tracker";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it before the weapons fire.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
