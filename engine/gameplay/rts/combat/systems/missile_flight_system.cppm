export module engine.gameplay.rts.combat.systems.missile_flight_system;
import std;
export import engine.gameplay.rts.combat.resources.garrison_kills;
export import engine.gameplay.common.lifetime.components.lifetime;
export import engine.gameplay.common.appearance.components.draw_hidden;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.sneaky_target;
export import engine.gameplay.rts.combat.components.countermeasures;
export import engine.gameplay.rts.combat.algorithms.countermeasure_decoys;
export import engine.gameplay.rts.combat.components.missile;
export import engine.gameplay.rts.combat.components.missile_waypoint_path;
export import engine.gameplay.rts.combat.algorithms.thrust;
export import engine.gameplay.rts.combat.systems.projectile_flight_system;
export import engine.gameplay.common.physics.resources.physics_settings;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.rts.combat.algorithms.projectile_collision;

// Guided missiles fly, chunk-parallel, a tick each as the original's
// MissileAIUpdate::update, its THRUST locomotor and its physics:
//   LAUNCH coasts (no thrust, no turning) until the ignition delay is over;
//   IGNITION arms it and starts its fuel; ATTACK_NOTURN thrusts straight
//   ahead until it has flown its distance before turning; ATTACK steers at
//   its goal (its victim while it tracks one) at its turn rate, thrusting up
//   to its maximum thrust angle off its nose; within its lock distance (half
//   for a spot on the ground) it makes its KILL run, moving straight at the
//   goal at its speed until it gets there and detonates. Out of fuel it
//   detonates or coasts; a victim gone, it burns out (KILL_SELF, gone a few
//   ticks later). Touching its victim armed, it detonates. Physics adds
//   gravity and the thrust to its velocity and moves it (not on its final
//   run), holding it on the ground.
export namespace engine::gameplay
{

// MissileAIUpdate::projectileFireAtObjectOrPosition: the missile as it leaves (along its barrel, tipped up
// twice the climb to a higher target, at its initial speed, steering for its victim or the spot, 10 over
// it when it locks on).
inline MissileFlight LaunchMissile(const Shot &shot, const WeaponDefinition &weapon) noexcept
{
	using Engine::Math::Fixed;
	using Engine::Math::FixedVector3;
	const MissileFlightDefinition &d = weapon.missile;
	const Fixed cp = Engine::Math::Cos(shot.launchPitch);
	FixedVector3 direction{Engine::Math::Cos(shot.launchYaw) * cp, Engine::Math::Sin(shot.launchYaw) * cp, Engine::Math::Sin(shot.launchPitch)};
	const FixedVector3 to = shot.aim - shot.origin;
	const Fixed across = std::max(Engine::Math::Length(to.XY()), Fixed::One());
	if (to.z > Fixed{})
		direction.z += Fixed::FromInt(2) * to.z / across;
	direction = Normalized(direction);
	MissileFlight flight;
	const Fixed initial = d.useWeaponSpeed ? weapon.speed : d.initialSpeed;
	flight.velocity = direction * initial;
	flight.forward = direction;
	flight.previous = shot.origin;
	flight.maxSpeed = d.useWeaponSpeed ? weapon.speed : d.maxSpeed;
	flight.maxAccel = d.useWeaponSpeed ? weapon.speed : d.acceleration;
	flight.noTurnLeft = d.noTurnDistance;
	flight.stateTick = shot.fireTick;
	flight.tracking = d.followsTarget && shot.target != ecs::Entity{};
	flight.originalGoal = shot.aim;
	flight.goal = shot.aim;
	if (!flight.tracking && d.lockDistance > Fixed{})
		flight.goal.z += Fixed::FromInt(10); // APPROACH_HEIGHT
	flight.shot = shot;
	return flight;
}

struct MissileFlightSystem
{
	using Query = ecs::Query<ecs::Write<MissileFlight>, ecs::Write<Transform>, ecs::OptionalWrite<Attitude>, ecs::Optional<MissileWaypointPath>>;
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Read<GroundHeight>, ecs::Read<WeaponCatalog>, ecs::Read<PhysicsSettings>,
		ecs::Read<Relationships>, ecs::Write<MissileDetonations>, ecs::Write<MissileGarrisonHits>>;
	// Whether a victim that left the fight still exists (dying, its hulk still there) or is gone; a decoyed victim's
	// flares and where they are.
	using Lookup = ecs::Lookup<ecs::Read<DefinitionRef>, ecs::Read<Countermeasures>, ecs::Read<Transform>, ecs::Read<Health>, ecs::Read<SneakyTarget>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context)
	{
		context.Write<MissileDetonations>().Reset(query.PreparedChunkCount());
		context.Write<MissileGarrisonHits>().Reset(query.PreparedChunkCount());
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const Fixed gravity = context.Read<PhysicsSettings>().gravity;
		const Relationships &relationships = context.Read<Relationships>();
		auto &detonated = context.Write<MissileDetonations>().Slot(context);
		auto &garrisonHits = context.Write<MissileGarrisonHits>().Slot(context);
		auto missiles = chunk.Get<MissileFlight>();
		auto transforms = chunk.Get<Transform>();
		auto attitudes = chunk.Get<Attitude>();
		const auto paths = chunk.Get<MissileWaypointPath>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		const auto lookup = context.Lookup<Lookup>();
		for (std::size_t row = 0; row < missiles.size(); ++row)
		{
			MissileFlight &m = missiles[row];
			const MissileFlightDefinition &d = weapons.At(m.shot.weapon).missile;
			FixedVector3 position = transforms[row].position;
			const auto detonate = [&] {
				Shot landed = m.shot;
				landed.aim = position;
				landed.carrier = entities[row];
				if (m.noDamage != 0)
					landed.damageScale = Fixed{}; // handleProjectileDetonation without damage
				detonated.push_back(landed);
				// MissileAIUpdate::detonate: its drawable hidden as it holds KILL_SELF (MISSILE_KILLING_SELF).
				context.Commands().Add<DrawHidden>(entities[row]);
				// MissileCallsOnDie: it stays for the impact to kill it (its die modules run), and takes itself away once its
				// KILL_SELF state has held KillSelfDelay (doKillSelfState: destroyObject); else it takes itself away now.
				if (weapons.At(m.shot.weapon).missileCallsOnDie && lookup.Get<Health>(entities[row]) != nullptr)
				{
					context.Commands().Remove<MissileFlight>(entities[row]);
					context.Commands().Add<Lifetime>(entities[row], Lifetime{tick + d.killSelfTicks, d.detonateCallsKill ? 0u : 1u, weapons.normalDeath});
				}
				// DetonateCallsKill alone: it holds KILL_SELF for KillSelfDelay, then is killed (DEATH_NORMAL).
				else if (d.detonateCallsKill && lookup.Get<Health>(entities[row]) != nullptr)
				{
					context.Commands().Remove<MissileFlight>(entities[row]);
					context.Commands().Add<Lifetime>(entities[row], Lifetime{tick + d.killSelfTicks, 0u, weapons.normalDeath});
				}
				// Else it holds KILL_SELF for KillSelfDelay (its contrail catches up), then is destroyed (doKillSelfState).
				else
				{
					context.Commands().Remove<MissileFlight>(entities[row]);
					context.Commands().Add<Lifetime>(entities[row], Lifetime{tick + d.killSelfTicks, 1u, weapons.normalDeath});
				}
			};
			// The distance flown since the last tick counts against the straight run once ignited.
			if (m.noTurnLeft > Fixed{} && m.state >= MissileState::Ignition)
			{
				m.noTurnLeft -= Engine::Math::Length(position - m.previous);
				m.previous = position;
			}
			if (position.z < Fixed{})
			{
				context.Commands().Destroy(entities[row]); // under the world: gone
				continue;
			}
			// Decoyed: harmless from now, it turns for the flare of its victim's newest volley nearest the victim
			// (calculateCountermeasureToDivertTo; none: on as it was).
			if (m.decoyTick != 0 && m.decoyTick <= tick)
			{
				m.decoyTick = 0;
				m.noDamage = 1;
				const Transform *victimAt = lookup.IsAlive(m.shot.target) ? lookup.Get<Transform>(m.shot.target) : nullptr;
				if (const Countermeasures *decoys = victimAt != nullptr ? lookup.Get<Countermeasures>(m.shot.target) : nullptr)
					if (const ecs::Entity flare = DivertTarget(*decoys, victimAt->position.XY(), [&](ecs::Entity candidate) -> const Transform * {
							return lookup.IsAlive(candidate) ? lookup.Get<Transform>(candidate) : nullptr;
						});
						flare != ecs::Entity{})
					{
						m.goal = lookup.Get<Transform>(flare)->position;
						m.originalGoal = m.goal;
						m.tracking = true;
						m.shot.target = flare;
					}
			}
			const SpatialEntry *victim = m.tracking ? spatial.Find(m.shot.target) : nullptr;
			// Its AI's goal position (getGoalPosition): its waypoint path state's, when it flies one, else where it steers.
			const MissileWaypointPath *path = paths.empty() ? nullptr : &paths[row];
			const FixedVector3 aiGoal = path != nullptr && path->waypoint != MissileWaypointPath::None ? path->lockGoal : m.goal;
			if (victim != nullptr)
				m.goal = victim->position; // aiMoveToObject: its victim's position
			else if (m.tracking && lookup.IsAlive(m.shot.target))
			{
				// Its victim is out of the fight but still there (dying, its hulk): it flies on to where it last was
				// (the original keeps the dying object as its goal until it is destroyed).
				m.tracking = false;
				m.originalGoal = m.goal;
			}
			// The locomotor's caps this tick (setMaxAcceleration / setMaxTurnRate).
			Fixed accelCap = m.maxAccel;
			std::int64_t turnCap = 0;
			const auto burnOut = [&] { // airborneTargetGone
				m.fuelTick = tick;
				m.exhaustLit = false;
				m.state = MissileState::KillSelf;
				m.stateTick = tick;
			};
			bool gone = false;
			switch (m.state)
			{
			case MissileState::Launch:
				accelCap = Fixed{};
				if (tick - m.stateTick < d.ignitionTicks)
					break;
				m.state = MissileState::Ignition;
				m.stateTick = tick;
				[[fallthrough]];
			case MissileState::Ignition:
				accelCap = m.maxAccel;
				m.armed = true;
				m.exhaustLit = true; // its exhaust starts (IgnitionFX, createAttachedParticleSystemID)
				m.fuelTick = d.fuelTicks != 0 ? tick + d.fuelTicks : ~std::uint64_t{0};
				m.state = MissileState::AttackNoTurn;
				m.stateTick = tick;
				break;
			case MissileState::AttackNoTurn:
			case MissileState::Attack:
			{
				const bool turnOk = m.state == MissileState::Attack;
				if (tick >= m.fuelTick)
				{
					if (d.detonateOnNoFuel)
					{
						detonate();
						gone = true;
						break;
					}
					accelCap = Fixed{};
					m.exhaustLit = false; // tossExhaust
				}
				else
					turnCap = turnOk ? Unlimited : 0;
				if (d.lockDistance > Fixed{})
				{
					const FixedVector3 to = victim != nullptr ? victim->position : aiGoal;
					const Fixed distance = Engine::Math::DistanceSquared(position.XY(), to.XY());
					const Fixed lock = m.tracking ? d.lockDistance : d.lockDistance / Fixed::FromInt(2);
					if (distance < lock * lock)
					{
						if (!m.tracking)
							m.goal = m.originalGoal;
						// aiMoveToPosition: a waypoint path it flew is over.
						if (path != nullptr)
							context.Commands().Remove<MissileWaypointPath>(entities[row]);
						m.state = MissileState::Kill;
						m.stateTick = tick;
						break;
					}
				}
				if (d.preferredHeight > Fixed{})
				{
					const FixedVector3 to = victim != nullptr ? victim->position : aiGoal;
					if (Engine::Math::DistanceSquared(position.XY(), to.XY()) < d.diveDistance * d.diveDistance)
						m.preciseZ = true;
				}
				if (m.noTurnLeft <= Fixed{})
				{
					m.state = MissileState::Attack;
					m.stateTick = tick;
				}
				if (m.tracking && victim == nullptr)
					burnOut();
				break;
			}
			case MissileState::Kill:
				if (tick >= m.fuelTick)
				{
					if (d.detonateOnNoFuel)
					{
						detonate();
						gone = true;
					}
					else
						burnOut();
					break;
				}
				m.braking = true;
				turnCap = Unlimited;
				// The move is done once it got there: at its victim (within its speed) or its spot, it detonates.
				if (Engine::Math::LengthSquared(m.goal - position) < Fixed::One())
				{
					if (victim == nullptr || Engine::Math::Length(victim->position - position) - victim->radius - d.radius < m.maxSpeed)
					{
						if (victim != nullptr)
							position = victim->position;
						detonate();
						gone = true;
						break;
					}
				}
				if (m.tracking && victim == nullptr)
					burnOut();
				break;
			case MissileState::KillSelf:
				// Held a few ticks (its trail catches up), then gone (doKillSelfState): killed with DetonateCallsKill (its die
				// modules run; the lifetime system kills it the tick the hold ends), else destroyed.
				if (d.detonateCallsKill)
				{
					if (tick + 1 - m.stateTick >= d.killSelfTicks)
					{
						context.Commands().Remove<MissileFlight>(entities[row]);
						context.Commands().Add<Lifetime>(entities[row], Lifetime{std::max(tick + 1, m.stateTick + d.killSelfTicks), 0u, weapons.normalDeath});
					}
				}
				else if (tick - m.stateTick >= d.killSelfTicks)
					context.Commands().Destroy(entities[row]);
				gone = true;
				break;
			}
			if (gone)
				continue;
			// Running into its victim or anything its weapon collides with, armed, it blows up (projectileHandleCollision).
			const auto sneaky = [&](ecs::Entity thing) {
				const SneakyTarget *miss = context.Lookup<Lookup>().Get<SneakyTarget>(thing);
				return miss != nullptr && miss->Active(tick);
			};
			if (const SpatialEntry *other = m.armed ? ProjectileCollision(weapons.At(m.shot.weapon), m.shot, entities[row], position, d.radius, spatial, relationships, sneaky) : nullptr)
			{
				// GarrisonHitKillCount: a building is left to the garrison clearing (it kills riders instead, or it detonates).
				if (d.garrisonHitKill > 0 && (other->classes & target_class::Structure) != 0)
				{
					Shot landed = m.shot;
					landed.aim = position;
					landed.carrier = entities[row];
					const DefinitionRef *ref = lookup.Get<DefinitionRef>(entities[row]);
					garrisonHits.push_back({landed, other->entity, d.garrisonHitKill, d.garrisonHitRequired, d.garrisonHitForbidden,
						ref != nullptr ? ref->index : 0xFFFFFFFFu});
					context.Commands().Destroy(entities[row]);
					continue;
				}
				detonate();
				continue;
			}

			// The locomotor (locoUpdate_moveTowardsPosition with moveTowardsPositionThrust).
			const bool wasBraking = m.braking;
			const Fixed maxForward = std::max(std::min(d.maxSpeed, m.maxSpeed), Fixed::FromRatio(1, 100));
			const Fixed desired = maxForward;
			const Fixed actual = ForwardSpeed3D(m.velocity, m.forward);
			FixedVector3 localGoal = m.goal;
			if (d.preferredHeight != Fixed{} && !m.preciseZ)
			{
				const Fixed wanted = d.preferredHeight + ground.Surface(position.XY());
				localGoal.z = position.z + (wanted - position.z) * d.preferredHeightDamping;
			}
			const Fixed speedDelta = desired - actual;
			const Fixed accel = speedDelta > Fixed{} || d.braking == Fixed{} ? std::min(d.acceleration, accelCap) : -d.braking;
			const std::int64_t turn = std::min<std::int64_t>(static_cast<std::int64_t>(d.turnRate.units), turnCap);
			const FixedVector3 wantThrust = ThrustDirection(position, m.velocity, m.forward, localGoal, accel, gravity);
			const Turned thrust = RotateToward(turn > 0 ? static_cast<std::int64_t>(d.maxThrustAngle.units) : 0, m.forward, wantThrust);
			if (!NearlyZero(Engine::Math::Length(m.velocity)))
			{
				FixedVector3 heading = m.velocity;
				bool adjust = true;
				std::int64_t rate = turn;
				if (m.braking)
				{
					// Aligned to its goal, where it is going anyway, three times as fast.
					heading = m.goal - position;
					adjust = !NearlyZero(Engine::Math::LengthSquared(heading));
					rate = turn * 3;
				}
				if (adjust)
					if (const Turned nose = RotateToward(rate, m.forward, heading); nose.angle != 0)
						m.forward = nose.direction;
			}
			FixedVector3 push{};
			if (speedDelta != Fixed{} || thrust.angle != 0)
			{
				const Fixed damping = std::clamp(accel / maxForward, Fixed{}, Fixed::One());
				push = thrust.direction * accel - m.velocity * damping;
			}
			// Its final run cheats: straight at the goal by its speed, never past it.
			if (wasBraking)
			{
				const FixedVector3 to = m.goal - position;
				const Fixed distance = Engine::Math::Length(to);
				Fixed step = std::max(Engine::Math::Length(m.velocity), Fixed::FromRatio(1, 3)); // MIN_VEL: a cell a second
				step = std::min(step, distance);
				if (distance > Fixed::FromRatio(1, 1000))
					position = position + to * (step / distance);
			}

			// Physics: gravity and the push into the velocity; it moves by it (not on its final run), held on the ground.
			push.z += gravity;
			m.velocity = m.velocity + push;
			const Fixed tiny = Fixed::FromRatio(1, 1000);
			for (Fixed *component : {&m.velocity.x, &m.velocity.y, &m.velocity.z})
				if (Engine::Math::Abs(*component) < tiny)
					*component = Fixed{};
			if (!m.braking)
				position = position + m.velocity;
			const Fixed floor = ground.At(position.XY());
			if (position.z <= floor)
			{
				m.velocity.z += floor - position.z;
				if (m.velocity.z > Fixed{})
					m.velocity.z = Fixed{};
				position.z = floor;
			}
			transforms[row].position = position;
			if (m.forward.x != Fixed{} || m.forward.y != Fixed{})
				transforms[row].facing = Engine::Math::Heading(m.forward.XY());
			if (!attitudes.empty())
				attitudes[row].pitch = -Engine::Math::Heading({Engine::Math::Length(m.forward.XY()), m.forward.z});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::MissileFlightSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.missile_flight";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// After the tick's shots and the index its victims are found in; its detonations land this tick.
	using Before = SystemTypeList<engine::gameplay::ImpactSystem, engine::gameplay::AutoFireSystem>;
	using After = SystemTypeList<engine::gameplay::WeaponSystem, engine::gameplay::SpatialIndexSystem, engine::gameplay::ProjectileFlightSystem>;
};
}
