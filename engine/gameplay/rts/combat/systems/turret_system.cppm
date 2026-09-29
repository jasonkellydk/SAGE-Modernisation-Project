export module engine.gameplay.rts.combat.systems.turret_system;
import std;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.algorithms.attack_goal;
export import engine.gameplay.rts.combat.components.turret;
export import engine.gameplay.rts.combat.systems.targeting_system;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.common.spatial.components.body_extent;
import Engine.Core.Math.FixedRandom;

// Turrets aim, chunk-parallel, as the original's TurretAI state machine:
// with a target a turret AIMs, turning toward it at its rate (snapping on
// when within one step) and pitching to it (or its fire pitch); aligned
// within about 2 degrees, its weapon may fire the tick after (FIRE). Losing
// it, it HOLDs its aim for its recenter time, then RECENTERs to rest at half
// speed, then idles; an IDLE turret waits a random scan interval and
// IDLESCANs to a random angle beside rest at half speed, then holds again.
// A state handed over to acts from the next tick.
export namespace engine::gameplay
{
namespace turret_detail
{
// Turns `current` toward `desired` by at most `rate`; true when there.
constexpr bool TurnToward(Engine::Math::TurnAngle &current, Engine::Math::TurnAngle desired, Engine::Math::TurnAngle rate) noexcept
{
	const std::int64_t off = Engine::Math::DeltaTo(current, desired);
	if (std::llabs(off) < static_cast<std::int64_t>(rate.units) || rate.units == 0)
	{
		current = desired;
		return true;
	}
	current += off > 0 ? rate : Engine::Math::TurnAngle{0u - rate.units};
	return false;
}

constexpr Engine::Math::TurnAngle Half(Engine::Math::TurnAngle rate) noexcept { return {rate.units / 2u}; }

// A pitch below `floor` (both signed) is raised to it.
constexpr Engine::Math::TurnAngle AtLeast(Engine::Math::TurnAngle pitch, Engine::Math::TurnAngle floor) noexcept
{
	return static_cast<std::int32_t>(pitch.units) < static_cast<std::int32_t>(floor.units) ? floor : pitch;
}

// About 2 degrees (the original's 0.035 radians).
inline constexpr Engine::Math::TurnAngle AlignedWithin = Engine::Math::TurnFromRadians(Engine::Math::Fixed::FromRatio(35, 1000));

// An angle scaled by a (non-negative) factor.
constexpr Engine::Math::TurnAngle Scaled(Engine::Math::TurnAngle angle, Engine::Math::Fixed factor) noexcept
{
	return {static_cast<std::uint32_t>((static_cast<std::uint64_t>(angle.units) * static_cast<std::uint64_t>(std::max<std::int64_t>(factor.Raw(), 0))) >>
		Engine::Math::Fixed::FractionBits)};
}

// TurretAIAimTurretState's GroundUnitPitch targets: a spot on the ground, or an object on the ground (KINDOF_IMMOBILE, or
// one whose AI moves on the ground: not an aircraft, nor something in the air).
constexpr bool GroundTarget(const SpatialEntry &target) noexcept
{
	if (target.entity == ecs::Entity{} || (target.classes & target_class::Structure) != 0)
		return true;
	return (target.classes & (target_class::Aircraft | target_class::AirborneVehicle | target_class::AirborneInfantry)) == 0;
}
}

// What a turret aims with this tick (TurretAIAimTurretState): its entity's current weapon slot (getCurrentWeapon), whether
// it fired in the last two ticks (TurretAI::updateTurretAI: sweeping on for ENABLE_SWEEP_FRAME_COUNT frames after a
// shot, from the next), that weapon's attack range (with its bonuses), and how far up its pitch is taken from.
struct TurretAim
{
	std::uint8_t slot{0};
	bool sweeping{false};
	Engine::Math::Fixed range{Engine::Math::Fixed::One()};
	// Half its height (GeometryInfo::getMaxHeightAbovePosition / 2): the pitch is taken from there.
	Engine::Math::Fixed halfHeight;
};

// TurretAI::updateTurretAI's sweep window: the two ticks after the one it fired on.
constexpr bool SweepingAfterShot(std::uint64_t firedTick, std::uint64_t tick) noexcept
{
	return firedTick != 0 && tick > firedTick && tick <= firedTick + 2;
}

// One turret's tick (the original's TurretAI state machine), aiming at `target` when there is one.
inline void StepTurret(Turret &turret, const Transform &transform, const SpatialEntry *target, std::uint64_t tick, std::uint64_t seed, ecs::Entity entity,
	const TurretAim &with = {})
{
	using namespace turret_detail;
	const TurretDefinition &d = turret.definition;
	// Off (setTurretEnabled false), its state machine does not run unless it is recentering: it stays as it is, aims
	// at nothing and does not fire.
	if (!turret.enabled && turret.state != TurretState::Recenter)
	{
		turret.fireReady = false;
		turret.aligned = false;
		turret.rotating = false;
		return;
	}
	const Engine::Math::TurnAngle before = turret.angle;
	const Engine::Math::TurnAngle pitchBefore = turret.pitch;
	// FIRE runs the tick after AIM succeeds.
	turret.fireReady = turret.aligned && target != nullptr && turret.state == TurretState::Aim;
	turret.aligned = false;
	auto random = [&] { return Engine::Math::Stream(seed, {tick, entity.index, entity.generation}); };
	// Entering IDLE draws when it next scans (TurretAIIdleState::resetIdleScan).
	const auto idle = [&] {
		auto stream = random();
		turret.state = TurretState::Idle;
		turret.until = tick + static_cast<std::uint64_t>(Engine::Math::UniformInt(stream, static_cast<std::int64_t>(d.minScanTicks),
			static_cast<std::int64_t>(std::max(d.minScanTicks, d.maxScanTicks))));
	};
	const auto hold = [&] {
		turret.state = TurretState::Hold;
		turret.until = tick + d.recenterTicks;
	};
	if (target != nullptr)
	{
		// setTurretTargetObject: any state goes to AIM, which acts at once.
		turret.state = TurretState::Aim;
		const Engine::Math::FixedVector2 toTarget = target->position.XY() - transform.position.XY();
		const Engine::Math::TurnAngle toward = Engine::Math::Heading(toTarget) - transform.facing;
		// TurretFireAngleSweep: just after a shot it swings past the target to one side, then the other (at its sweep
		// speed), and anywhere within the sweep of the target it is on it.
		const Engine::Math::TurnAngle sweep = with.slot < d.sweep.size() ? d.sweep[with.slot] : Engine::Math::TurnAngle{};
		Engine::Math::TurnAngle aim = toward;
		Engine::Math::TurnAngle rate = d.turnRate;
		if (sweep.units != 0 && with.sweeping)
		{
			aim = turret.positiveSweep ? toward + sweep : toward - sweep;
			rate = Scaled(rate, d.sweepSpeed[with.slot]);
		}
		TurnToward(turret.angle, aim, rate);
		bool turned = std::llabs(static_cast<std::int64_t>(Engine::Math::DeltaTo(turret.angle, aim))) <= AlignedWithin.units;
		if (sweep.units != 0)
		{
			if (turned)
				turret.positiveSweep = !turret.positiveSweep;
			turned = std::llabs(static_cast<std::int64_t>(Engine::Math::DeltaTo(turret.angle, toward))) < static_cast<std::int64_t>(sweep.units);
		}
		bool pitched = true;
		if (d.allowsPitch)
		{
			Engine::Math::TurnAngle desired = d.firePitch;
			if (d.firePitch.units == 0)
			{
				// From the firer raised by half its height (getVectorTo FROM_CENTER_3D, then v.z -= maxHeight / 2).
				const Engine::Math::Fixed rise = target->position.z - transform.position.z - with.halfHeight;
				const Engine::Math::TurnAngle actual = Engine::Math::Atan2(rise, Engine::Math::Length(toTarget));
				desired = AtLeast(actual, d.minPitch);
				// GroundUnitPitch: at ground targets it aims higher by that much at full range, less as they come closer
				// (so it does not shoot over them).
				if (d.groundUnitPitch.units != 0 && GroundTarget(*target))
				{
					const Engine::Math::Fixed distance = Engine::Math::Length(Engine::Math::FixedVector3{toTarget.x, toTarget.y, rise});
					const Engine::Math::Fixed range = std::max(with.range, Engine::Math::Fixed::One());
					desired = AtLeast(actual + Scaled(d.groundUnitPitch, distance / range), d.minPitch);
				}
			}
			// friend_turnTowardsPitch: on it once there, a step landing on it included.
			TurnToward(turret.pitch, desired, d.pitchRate);
			pitched = turret.pitch == desired;
		}
		turret.aligned = turned && pitched;
	}
	else
		switch (turret.state)
		{
		case TurretState::Aim:
			hold(); // its target went: HOLD from this tick
			break;
		case TurretState::Hold:
			if (tick >= turret.until)
				turret.state = TurretState::Recenter;
			break;
		case TurretState::Recenter:
		{
			const bool turned = TurnToward(turret.angle, d.naturalAngle, Half(d.turnRate));
			const bool pitched = !d.allowsPitch || TurnToward(turret.pitch, d.naturalPitch, Half(d.pitchRate));
			if (turned && pitched)
				idle();
			break;
		}
		case TurretState::Idle:
			if (turret.until == 0)
				idle(); // its first tick
			else if (tick >= turret.until)
			{
				// IDLESCAN: a random angle between the scan bounds, either side of rest; none to scan: HOLD.
				if (d.minScanUnits == 0 && d.maxScanUnits == 0)
					hold();
				else
				{
					auto stream = random();
					const std::int64_t spread = std::max<std::int64_t>(d.maxScanUnits - d.minScanUnits, 0);
					auto offset = static_cast<std::uint32_t>(d.minScanUnits + Engine::Math::UniformInt(stream, 0, spread));
					if (Engine::Math::UniformInt(stream, 0, 1) == 0)
						offset = 0u - offset;
					turret.scanAngle = {offset};
					turret.state = TurretState::IdleScan;
				}
			}
			break;
		case TurretState::IdleScan:
		{
			const bool turned = TurnToward(turret.angle, d.naturalAngle + turret.scanAngle, Half(d.turnRate));
			const bool pitched = !d.allowsPitch || TurnToward(turret.pitch, d.naturalPitch, Half(d.pitchRate));
			if (turned && pitched)
				hold();
			break;
		}
		}
	// Turning or pitching this tick (its rotation sound plays, as TurretAI's m_playRotSound / m_playPitchSound).
	turret.rotating = turret.angle != before || turret.pitch != pitchBefore;
}

// Off the map, only a turret its carrier lets fire (a mounted portable structure) turns.
struct TurretSystem
{
	using Query = ecs::Query<ecs::Write<Turret>, ecs::Read<Transform>, ecs::Read<AttackTarget>, ecs::Optional<OffMap>, ecs::Optional<Disabled>,
		ecs::Optional<Armament>, ecs::Optional<WeaponSlots>, ecs::Optional<WeaponBonusConditions>, ecs::Optional<BodyExtent>>;
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Read<RandomSeed>, ecs::Read<WeaponCatalog>>;

	// What the turret on `row` aims with this tick.
	template<typename Chunk>
	static TurretAim AimOf(const Chunk &chunk, std::size_t row, const WeaponCatalog &weapons, std::uint64_t tick)
	{
		TurretAim with;
		const auto armaments = chunk.template Get<Armament>();
		const auto slots = chunk.template Get<WeaponSlots>();
		const auto bonuses = chunk.template Get<WeaponBonusConditions>();
		const auto extents = chunk.template Get<BodyExtent>();
		if (!extents.empty())
			with.halfHeight = extents[row].maxHeight / Engine::Math::Fixed::FromInt(2);
		if (!slots.empty())
			with.slot = slots[row].current;
		if (!armaments.empty() && armaments[row].weapon != WeaponCatalog::None)
		{
			const WeaponDefinition &weapon = weapons.At(armaments[row].weapon);
			with.range = BonusAttackRange(weapon.attackRange, weapons.Bonus(weapon, bonuses.empty() ? 0u : bonuses[row].Effective()));
			with.sweeping = SweepingAfterShot(armaments[row].firedTick, tick);
		}
		return with;
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using namespace turret_detail;
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		auto turrets = chunk.Get<Turret>();
		const auto transforms = chunk.Get<Transform>();
		const auto targets = chunk.Get<AttackTarget>();
		const std::uint64_t tick = context.Tick();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0x70A7u;
		const auto entities = chunk.Entities();
		const auto disabledRows = chunk.Get<Disabled>();
		const auto offMap = chunk.Get<OffMap>();
		for (std::size_t row = 0; row < turrets.size(); ++row)
		{
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::Held))
				continue;
			// Carried, it turns only where it may fire (a portable structure on its carrier).
			if (!offMap.empty() && !offMap[row].armed)
				continue;
			SpatialEntry point;
			StepTurret(turrets[row], transforms[row], AttackGoal(spatial, targets[row], point), tick, seed,
				entities[row], AimOf(chunk, row, weapons, tick));
		}
	}
};

// Second turrets (AltTurret), chunk-parallel, as the first: a linked one (TurretsLinked) aims at what its
// entity attacks; one not linked has nothing of its own to aim at here, so it holds, recenters and scans.
struct AltTurretSystem
{
	using Query = ecs::Query<ecs::Write<AltTurret>, ecs::Read<Transform>, ecs::Read<AttackTarget>, ecs::Optional<OffMap>, ecs::Optional<Disabled>,
		ecs::Optional<Armament>, ecs::Optional<WeaponSlots>, ecs::Optional<WeaponBonusConditions>, ecs::Optional<BodyExtent>>;
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Read<RandomSeed>, ecs::Read<WeaponCatalog>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		auto turrets = chunk.Get<AltTurret>();
		const auto transforms = chunk.Get<Transform>();
		const auto targets = chunk.Get<AttackTarget>();
		const std::uint64_t tick = context.Tick();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0x70A8u;
		const auto entities = chunk.Entities();
		const auto disabledRows = chunk.Get<Disabled>();
		const auto offMap = chunk.Get<OffMap>();
		for (std::size_t row = 0; row < turrets.size(); ++row)
		{
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::Held))
				continue;
			// Carried, it turns only where it may fire (a portable structure on its carrier).
			if (!offMap.empty() && !offMap[row].armed)
				continue;
			AltTurret &alt = turrets[row];
			SpatialEntry point;
			const SpatialEntry *target = !alt.linked ? nullptr : AttackGoal(spatial, targets[row], point);
			StepTurret(alt.turret, transforms[row], target, tick, seed, entities[row], TurretSystem::AimOf(chunk, row, weapons, tick));
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::TurretSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.turrets";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::MovementSystem, engine::gameplay::TargetingSystem>;
};
template<>
struct SystemTraits<engine::gameplay::AltTurretSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.alt_turrets";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::MovementSystem, engine::gameplay::TargetingSystem>;
};
}
