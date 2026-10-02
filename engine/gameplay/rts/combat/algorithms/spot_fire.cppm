export module engine.gameplay.rts.combat.algorithms.spot_fire;
import std;

export import engine.gameplay.rts.combat.systems.weapon_system;
import Engine.Core.Math.FixedRandom;

// Object::fireCurrentWeapon(const Coord3D *) (GeneralsMD Object.cpp) -> Weapon::fireWeapon -> privateFireWeapon at a
// spot, with no victim, for a behaviour that fires an object's weapon itself (a payload carrier's strafing run, its
// FireWeapon delivery): only when its current weapon is READY_TO_FIRE (not between shots, reloading or out of ammo);
// no range, facing or wind-up checks (those are the attack state's). The shot leaves from its barrel's launch point
// (a projectile weapon) at the spot, scattered as the weapon scatters (its ScatterTarget pattern, then ScatterRadius);
// then the weapon waits between shots or reloads its clip exactly as WeaponSystem's shots do, a reload letting go of a
// temporary weapon lock, the set's other weapons waiting as long when it shares reload times, and the shot goes to its
// FiringTracker.
export namespace engine::gameplay
{
struct SpotFirer
{
	ecs::Entity entity;
	std::uint32_t player{0};
	const Transform *transform{nullptr};
	const LaunchLayout *layout{nullptr};
	Armament *armament{nullptr};
	WeaponSlots *slots{nullptr};       // its weapon set beyond one weapon (none: the Armament is all it has)
	FiringTracker *tracker{nullptr};
	std::uint32_t conditions{0};       // its weapon bonus conditions
	std::uint8_t veterancy{0};
};

// Returns the shot fired, or nothing when its current weapon is not ready.
inline std::optional<Shot> FireCurrentWeaponAt(SpotFirer &firer, Engine::Math::FixedVector3 aim, const WeaponCatalog &weapons, const GroundHeight &ground,
	std::uint64_t tick, std::uint64_t seed)
{
	using Engine::Math::Fixed;
	if (firer.armament == nullptr || firer.transform == nullptr)
		return std::nullopt;
	Armament &armament = *firer.armament;
	if (armament.weapon == WeaponCatalog::None || armament.readyTick == OutOfAmmo || tick < armament.readyTick)
		return std::nullopt;
	const std::uint8_t slotIndex = firer.slots != nullptr ? firer.slots->current : std::uint8_t{0};
	const WeaponDefinition &weapon = weapons.At(armament.weapon);
	const WeaponBonus bonus =
		weapons.Bonus(weapon, firer.conditions | (firer.tracker != nullptr ? WeaponSystem::ContinuousFireConditions(*firer.tracker, weapons) : 0u));
	const Transform &transform = *firer.transform;
	auto random = Engine::Math::Stream(seed, {tick, firer.entity.index, firer.entity.generation});
	// As Weapon::privateFireWeapon: wrap at the barrel count; this barrel fires.
	if (armament.barrel >= armament.barrels)
		armament.barrelShots = 0;
	const std::uint8_t barrel = armament.barrel >= armament.barrels ? std::uint8_t{0} : armament.barrel;
	Engine::Math::FixedVector3 origin = transform.position;
	if (weapon.projectile)
		origin = LaunchPosition(firer.layout, barrel, false, {}, {}, transform, slotIndex, false);
	// Its ScatterTarget pattern, then ScatterRadius (WeaponTemplate::fireWeaponTemplate), as WeaponSystem aims them.
	const std::uint32_t patternSize = std::min(weapon.scatterCount, ScatterTargetMax);
	const std::uint64_t unused = patternSize == 0 ? 0 : ~armament.scatterUsed & (patternSize == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << patternSize) - 1);
	if (unused != 0)
	{
		auto patternRandom = Engine::Math::Stream(seed ^ 0x5CA77A6Eu, {tick, firer.entity.index, firer.entity.generation});
		auto pick = Engine::Math::UniformInt(patternRandom, 0, static_cast<std::int64_t>(std::popcount(unused)) - 1);
		std::uint64_t left = unused;
		for (; pick > 0; --pick)
			left &= left - 1;
		const auto entry = static_cast<std::uint32_t>(std::countr_zero(left));
		armament.scatterUsed |= std::uint64_t{1} << entry;
		const Engine::Math::FixedVector2 offset = weapons.scatterTargets[weapon.scatterFirst + entry];
		aim.x += offset.x * weapon.scatterTargetScalar;
		aim.y += offset.y * weapon.scatterTargetScalar;
		aim.z = ground.At(aim.XY());
	}
	// fireWeaponTemplate's victimPos (the spot, after the ScatterTarget pattern, before the scatter): its delay's end.
	const Engine::Math::FixedVector3 victimAt = aim;
	if (weapon.scatterRadius > Fixed{})
	{
		auto scatterRandom = Engine::Math::Stream(seed ^ 0x5CA77E4u, {tick, firer.entity.index, firer.entity.generation});
		const Fixed scatter = Engine::Math::UniformFixed(scatterRandom, Fixed{}, weapon.scatterRadius);
		const Engine::Math::TurnAngle way{static_cast<std::uint32_t>(Engine::Math::UniformInt(scatterRandom, 0, 0xFFFFFFFFll))};
		if (weapon.projectile && scatter > Fixed{})
		{
			aim.x += scatter * Engine::Math::Cos(way);
			aim.y += scatter * Engine::Math::Sin(way);
			aim.z = ground.At(aim.XY());
		}
	}
	const std::uint64_t travel = weapon.laser && !weapon.projectile ? 0 : HitDelayTicks(transform.position, victimAt, weapon.speed);
	Shot shot;
	shot.source = firer.entity;
	shot.weapon = armament.weapon;
	shot.sourcePlayer = firer.player;
	shot.origin = origin;
	shot.aim = aim;
	shot.fireTick = tick;
	shot.impactTick = weapon.lobbed || weapon.guided || weapon.objectFlown ? LandsWithProjectile : tick + travel;
	shot.launchYaw = transform.facing;
	shot.slot = slotIndex;
	shot.veterancy = firer.veterancy;
	shot.damageScale = bonus.Get(WeaponBonusField::Damage);
	shot.radiusScale = bonus.Get(WeaponBonusField::Radius);
	// Between shots (a random pick between the weapon's bounds, divided by the rate of fire bonus).
	const std::uint64_t spread = weapon.delayMax > weapon.delayMin ? weapon.delayMax - weapon.delayMin : 0;
	const std::uint64_t delay = weapon.delayMin + (spread == 0 ? 0 : static_cast<std::uint64_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(spread))));
	armament.readyTick = tick + std::max<std::uint64_t>(BonusDelay(delay, bonus), 1);
	armament.firedTick = tick;
	armament.firedBarrel = barrel;
	if (++armament.barrelShots >= weapon.shotsPerBarrel)
	{
		armament.barrel = static_cast<std::uint8_t>(barrel + 1);
		armament.barrelShots = 0;
	}
	else
		armament.barrel = barrel;
	armament.reloading = false;
	if (weapon.clipSize > 0)
	{
		if (armament.clip == 0 || armament.clip > weapon.clipSize)
			armament.clip = weapon.clipSize;
		if (--armament.clip == 0)
		{
			if (weapon.reloadsAtBase || weapon.noReload)
			{
				armament.readyTick = OutOfAmmo;
				armament.reloading = true;
			}
			else
			{
				armament.clip = weapon.clipSize;
				armament.scatterUsed = 0;
				armament.readyTick = tick + std::max<std::uint64_t>(BonusDelay(weapon.clipReload, bonus), 1);
				armament.reloading = true;
			}
			// Object::fireCurrentWeapon: reloaded, it lets go of a temporary lock.
			if (firer.slots != nullptr)
				ReleaseTemporaryLock(*firer.slots);
		}
	}
	if (firer.slots != nullptr && armament.readyTick != OutOfAmmo)
	{
		WeaponSlots &set = *firer.slots;
		ShareReloadTime(set, armament.readyTick, armament.reloading);
		set.slots[set.current].readyTick = armament.readyTick;
		set.slots[set.current].reloading = armament.reloading;
	}
	if (firer.tracker != nullptr)
	{
		FiringTracker &tracker = *firer.tracker;
		const std::uint8_t level = tracker.level, faerie = tracker.faerie;
		// No victim: a spot on the ground clears TARGET_FAERIE_FIRE.
		tracker.faerie = 0;
		WeaponSystem::ShotFired(tracker, weapon, armament.weapon, ecs::Entity{}, armament.readyTick, tick);
		if (tracker.level != level || tracker.faerie != faerie)
		{
			const std::uint64_t before = armament.readyTick;
			WeaponSystem::Retime(armament, weapon, weapons.Bonus(weapon, firer.conditions | WeaponSystem::ContinuousFireConditions(tracker, weapons)), tick, random);
			if (firer.slots != nullptr && armament.readyTick != before)
				ShareReloadTime(*firer.slots, armament.readyTick, true);
		}
	}
	return shot;
}
}
