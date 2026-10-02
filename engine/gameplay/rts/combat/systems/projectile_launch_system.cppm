export module engine.gameplay.rts.combat.systems.projectile_launch_system;
import std;

export import engine.gameplay.common.random.resources.random_seed;
import Engine.Core.Math.FixedRandom;
export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.countermeasures;
export import engine.gameplay.rts.combat.components.projectile;
export import engine.gameplay.rts.combat.systems.projectile_flight_system;
export import engine.gameplay.rts.combat.systems.missile_flight_system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.spatial.components.attitude;

// The tick's projectile shots put their projectile objects out (Weapon::
// fireWeaponTemplate's ProjectileObject), at the launch point, carrying the
// shot; each is its shooter's player's but nobody's unit (no team, body,
// collision or targeting), and flies from the next tick. A lobbed one
// (DumbProjectileBehavior::projectileFireAtObjectOrPosition) faces the aim
// with its arc to it and its point count from the weapon's speed; a guided
// missile (MissileAIUpdate::projectileFireAtObjectOrPosition) leaves along
// its barrel, tipped up twice the climb to a higher target, at its initial
// speed, steering for its victim (when it follows one) or the spot (10 over
// it when it locks on).
export namespace engine::gameplay
{
// DumbProjectileBehavior::projectileFireAtObjectOrPosition's flight speed: WeaponSpeed, or with ScaleWeaponSpeed the
// share of the way the 2D range from launch to aim is from MinimumAttackRange to (the unmodified) AttackRange, of the
// way from MinWeaponSpeed to WeaponSpeed (beyond either end it carries on the same line).
inline Engine::Math::Fixed LobSpeed(const WeaponDefinition &weapon, Engine::Math::FixedVector2 from, Engine::Math::FixedVector2 to) noexcept
{
	const Engine::Math::Fixed span = weapon.attackRange - weapon.minimumRange;
	if (!weapon.scaleWeaponSpeed || span == Engine::Math::Fixed{})
		return weapon.speed;
	const Engine::Math::Fixed ratio = (Engine::Math::Length(to - from) - weapon.minimumRange) / span;
	return ratio * (weapon.speed - weapon.minWeaponSpeed) + weapon.minWeaponSpeed;
}

struct ProjectileLaunchSystem
{
	static void Launch(ecs::CommandBuffer &commands, const Shot &shot, const WeaponDefinition &weapon, std::uint64_t decoyTick = 0)
	{
		MissileFlight flight = LaunchMissile(shot, weapon);
		flight.decoyTick = decoyTick;
		const Engine::Math::FixedVector3 direction = flight.forward;
		const auto missile = commands.Create();
		commands.Add<Transform>(missile, Transform{shot.origin, Engine::Math::Heading(direction.XY())});
		commands.Add<DefinitionRef>(missile, DefinitionRef{weapon.projectileDefinition});
		commands.Add<Owner>(missile, Owner{shot.sourcePlayer});
		commands.Add<Attitude>(missile, Attitude{-Engine::Math::Heading({Engine::Math::Length(direction.XY()), direction.z}), {}});
		commands.Add<MissileFlight>(missile, flight);
	}

	using Query = ecs::Query<ecs::Read<ProjectileFlight>>;
	using Resources = ecs::Resources<ecs::Read<FiredShots>, ecs::Read<WeaponCatalog>, ecs::Read<GroundHeight>, ecs::Read<RandomSeed>, ecs::Write<MissileReports>>;
	using Lookup = ecs::Lookup<ecs::Read<Countermeasures>>;

	void Execute(ecs::SystemContext &context)
	{
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0x7B3Bu;
		auto &commands = context.Commands();
		const auto lookup = context.Lookup<Lookup>();
		MissileReports &reports = context.Write<MissileReports>();
		reports.Reset(1);
		auto &reported = reports.SlotAt(0);
		context.Read<FiredShots>().ForEach([&](const Shot &shot) {
			const WeaponDefinition &weapon = weapons.At(shot.weapon);
			// A projectile flying itself is the game's to make (a full object: its death is its detonation).
			if (weapon.objectFlown)
				return;
			if (weapon.guided)
			{
				// Weapon::fireWeaponTemplate: a small missile at a victim with countermeasures on reports to it; while it
				// has flares left or out, EvasionRate of them turn for a flare MissileDecoyDelay from now
				// (reportMissileForCountermeasures).
				std::uint64_t decoyTick = 0;
				if (const Countermeasures *decoys = weapon.smallMissile && shot.target != ecs::Entity{} && lookup.IsAlive(shot.target)
						? lookup.Get<Countermeasures>(shot.target) : nullptr;
					decoys != nullptr && decoys->upgraded != 0)
				{
					bool diverted = false;
					if (decoys->available + decoys->active > 0)
					{
						auto random = Engine::Math::Stream(seed, {shot.fireTick, shot.source.index, shot.source.generation, shot.target.index, 0xDEC0u});
						if (Engine::Math::UniformFixed(random, Fixed{}, Fixed::One()) < decoys->evasionRate)
						{
							diverted = true;
							decoyTick = context.Tick() + decoys->decoyTicks;
						}
					}
					reported.push_back({shot.target, static_cast<std::uint8_t>(diverted ? 1 : 0)});
				}
				Launch(commands, shot, weapon, decoyTick);
				return;
			}
			if (!weapon.lobbed)
				return;
			const ArcPoints points = ArcControlPoints(shot.origin, shot.aim, weapon.arc, HighestTerrainAlong(ground, shot.origin.XY(), shot.aim.XY()));
			const auto projectile = commands.Create();
			commands.Add<Transform>(projectile, Transform{shot.origin, Engine::Math::Heading((shot.aim - shot.origin).XY())});
			commands.Add<DefinitionRef>(projectile, DefinitionRef{weapon.projectileDefinition});
			commands.Add<Owner>(projectile, Owner{shot.sourcePlayer});
			commands.Add<Attitude>(projectile, Attitude{});
			ProjectileFlight flight{points, ArcSegments(points, LobSpeed(weapon, shot.origin.XY(), shot.aim.XY())), 0, weapon.arc, shot};
			if (weapon.arc.tumble)
			{
				// TumbleRandomly: pitch, yaw and roll rates each in [-1/PI, 1/PI] radians a frame.
				constexpr std::int64_t limit = 217585585; // 1/PI radians in turn units (2^32 / (2 PI^2))
				auto random = Engine::Math::Stream(seed, {shot.fireTick, shot.source.index, shot.source.generation, shot.target.index});
				for (auto &rate : flight.tumble)
					rate = Engine::Math::TurnAngle{static_cast<std::uint32_t>(Engine::Math::UniformInt(random, -limit, limit))};
			}
			commands.Add<ProjectileFlight>(projectile, flight);
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::ProjectileLaunchSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.projectile_launch";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	// The tick's flights first: a new projectile flies from the next tick.
	using After = SystemTypeList<engine::gameplay::WeaponSystem, engine::gameplay::ProjectileFlightSystem, engine::gameplay::MissileFlightSystem>;
};
}
