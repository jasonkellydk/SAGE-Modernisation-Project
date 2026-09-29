export module engine.gameplay.rts.death.systems.slow_death_system;
import std;
export import engine.gameplay.common.spatial.resources.deck_surfaces;
export import engine.gameplay.common.spatial.components.surface_layer;
export import engine.gameplay.common.spatial.components.bounding_volume;

export import engine.ecs.system.system;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.death.components.crash;
export import engine.gameplay.rts.death.components.collapse;
export import engine.gameplay.common.appearance.components.draw_offset;
export import engine.gameplay.common.physics.resources.physics_settings;
export import engine.gameplay.common.spatial.components.attitude;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.rts.death.resources.death_events;
export import engine.gameplay.rts.death.algorithms.death_choice;
export import engine.gameplay.rts.death.systems.death_system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.rts.lifecycle.systems.removal_system;
export import engine.gameplay.rts.veterancy.components.experience;
export import engine.gameplay.rts.movement.algorithms.hover_motion;

// Plays out slow deaths, in parallel per chunk: the body sinks at its rate
// once its sink delay is over, plays its midpoint effects once, and at its
// destruction tick plays its final effects and is done (the removal system
// destroys it). Bodies no slow death applied to linger. Crashing wrecks
// fly on, fall and spin until they hit the ground, then slide to a halt and
// blow up (see CrashDefinition). Collapsing structures come down (see
// CollapseDefinition).
export namespace engine::gameplay
{
struct SlowDeathSystem
{
	static Engine::Math::TurnAngle Turn(std::int32_t units) noexcept { return Engine::Math::TurnAngle{static_cast<std::uint32_t>(units)}; }

	// One tick of a crash; true once it is over (blown up: remove it).
	// `snagged`: a helicopter's last collidee is a snag (a tree): it comes down where it is. `body`: a helicopter's
	// physics, when its locomotor flies the spiral (see CrashDefinition::hovering).
	template<typename Play, typename Eject>
	static bool StepCrash(Crash &crash, const CrashDefinition &how, Transform &transform, Attitude *attitude, const GroundHeight &ground, std::uint64_t tick,
		Play &&play, Eject &&eject, bool snagged = false, PhysicsBody *body = nullptr, const PhysicsSettings *settings = nullptr)
	{
		using Engine::Math::Fixed;
		auto &position = transform.position;
		if (how.kind == CrashKind::Helicopter && how.hovering && body != nullptr && settings != nullptr && crash.groundTick == 0)
		{
			// AIUpdateInterface::doLocomotor first (PHASE_INITIAL; LocomotorWorksWhenDead): its locomotor holds its place,
			// braking its forward speed (at most MaxBraking) and lifting it (dead, on its damaged lift, at most
			// -gravity x (1 - FallHowFast): setMaxLift), with its physics options.
			HoverLocomotor hover = how.hover;
			hover.acceleration = hover.accelerationDamaged;
			hover.braking = std::min(hover.braking, how.maxBraking);
			hover.lift = std::min(hover.liftDamaged, (Fixed{} - settings->gravity) * (Fixed::One() - how.fallFactor));
			SetHoverPhysics(*body, hover, false);
			HoverMaintain(*body, transform, hover, false, ground.At(position.XY()), settings->gravity, tick);
			// PhysicsBehavior::update (PHASE_PHYSICS): the forces move it.
			StepBody(*body, transform, attitude, *settings, [&](Engine::Math::FixedVector2 at) { return ground.At(at); }, false, tick < body->motiveUntil);
			// HelicopterSlowDeathBehavior::update (PHASE_NORMAL): spinning about itself, pushed along the spiral (a motive
			// force its physics takes next tick).
			transform.facing = transform.facing + Turn(crash.selfSpin);
			if (how.spinDelay != 0 && tick > crash.spinTick + how.spinDelay)
			{
				const bool down = crash.Has(crash_flag::SpinDown);
				crash.selfSpin += down ? -how.spinStep : how.spinStep;
				if (!down && crash.selfSpin > how.maxSelfSpin)
				{
					crash.selfSpin = how.maxSelfSpin;
					crash.flags |= crash_flag::SpinDown;
				}
				else if (down && crash.selfSpin < how.minSelfSpin)
				{
					crash.selfSpin = how.minSelfSpin;
					crash.flags &= ~crash_flag::SpinDown;
				}
				crash.spinTick = tick;
			}
			const Engine::Math::TurnAngle heading{crash.forwardAngle};
			ApplyMotiveForce(*body, {Engine::Math::Cos(heading) * crash.forwardSpeed, Engine::Math::Sin(heading) * crash.forwardSpeed, Fixed{}}, tick);
			crash.forwardAngle += static_cast<std::uint32_t>(how.spiralTurnRate);
			crash.forwardSpeed = crash.forwardSpeed * how.spiralDamping;
			crash.velocity = body->velocity;
			if (!crash.Has(crash_flag::BladesGone) && crash.bladeTick != 0 && tick >= crash.bladeTick)
			{
				crash.flags |= crash_flag::BladesGone;
				const auto &bone = how.bladeOffset;
				const Fixed c = Engine::Math::Cos(transform.facing), s = Engine::Math::Sin(transform.facing);
				play(DeathPhase::Blade, Engine::Math::FixedVector3{position.x + bone.x * c - bone.y * s, position.y + bone.x * s + bone.y * c, position.z + bone.z});
				eject();
			}
			// Within a unit of the ground, or into a tree: held there (DISABLED_HELD: its physics no longer move it).
			const Fixed floor = ground.At(position.XY());
			if (position.z <= floor + Fixed::One() || snagged)
			{
				crash.groundTick = tick;
				crash.velocity = {};
				body->velocity = {};
				body->acceleration = {};
				play(DeathPhase::HitGround);
			}
			return false;
		}
		if (how.kind == CrashKind::Helicopter)
		{
			if (crash.groundTick == 0)
			{
				// Spinning about itself, orbiting down the spiral.
				transform.facing = transform.facing + Turn(crash.selfSpin);
				if (how.spinDelay != 0 && tick > crash.spinTick + how.spinDelay)
				{
					const bool down = crash.Has(crash_flag::SpinDown);
					crash.selfSpin += down ? -how.spinStep : how.spinStep;
					if (!down && crash.selfSpin > how.maxSelfSpin)
					{
						crash.selfSpin = how.maxSelfSpin;
						crash.flags |= crash_flag::SpinDown;
					}
					else if (down && crash.selfSpin < how.minSelfSpin)
					{
						crash.selfSpin = how.minSelfSpin;
						crash.flags &= ~crash_flag::SpinDown;
					}
					crash.spinTick = tick;
				}
				const Engine::Math::TurnAngle heading{crash.forwardAngle};
				crash.velocity.x = Engine::Math::Cos(heading) * crash.forwardSpeed;
				crash.velocity.y = Engine::Math::Sin(heading) * crash.forwardSpeed;
				crash.forwardAngle += static_cast<std::uint32_t>(how.spiralTurnRate);
				crash.forwardSpeed = crash.forwardSpeed * how.spiralDamping;
				position.x += crash.velocity.x;
				position.y += crash.velocity.y;
				position.z += crash.velocity.z;
				crash.velocity.z += crash.fall;
				if (!crash.Has(crash_flag::BladesGone) && crash.bladeTick != 0 && tick >= crash.bladeTick)
				{
					crash.flags |= crash_flag::BladesGone;
					// At its blade bone (getPristineBonePositions, transformBoneToWorld: turned with it).
					const auto &bone = how.bladeOffset;
					const Fixed c = Engine::Math::Cos(transform.facing), s = Engine::Math::Sin(transform.facing);
					play(DeathPhase::Blade, Engine::Math::FixedVector3{position.x + bone.x * c - bone.y * s, position.y + bone.x * s + bone.y * c, position.z + bone.z});
					eject();
				}
				// On the ground, or into a tree on the way down (hitATree: held where it is).
				const Fixed floor = ground.At(position.XY());
				if (position.z <= floor + Fixed::One() || snagged)
				{
					if (position.z < floor)
						position.z = floor;
					crash.groundTick = tick;
					crash.velocity = {};
					play(DeathPhase::HitGround);
				}
				return false;
			}
			// Held where it came down, until it blows up (leaving its rubble).
			if (tick > crash.groundTick + how.finalDelay)
			{
				play(DeathPhase::FinalBlowUp);
				return true;
			}
			return false;
		}
		if (attitude != nullptr)
			attitude->roll = attitude->roll + Turn(crash.rollRate);
		crash.rollRate = static_cast<std::int32_t>((static_cast<std::int64_t>(crash.rollRate) * how.rollRateDelta.Raw()) >> Fixed::FractionBits);
		if (crash.groundTick == 0)
		{
			position.x += crash.velocity.x;
			position.y += crash.velocity.y;
			position.z += crash.velocity.z;
			crash.velocity.z += crash.fall;
			// Down, or into a tree on the way (JetSlowDeathBehavior: hitATree, where it is).
			const Fixed floor = ground.At(position.XY());
			if (position.z <= floor || snagged)
			{
				if (position.z < floor)
					position.z = floor;
				crash.groundTick = tick;
				play(DeathPhase::HitGround);
			}
			else if (!crash.Has(crash_flag::SecondaryDone) && tick >= crash.deathTick + how.secondaryDelay)
			{
				play(DeathPhase::Secondary);
				crash.flags |= crash_flag::SecondaryDone;
			}
			return false;
		}
		// Sliding to a halt on the ground, pitching over; one brought down by a tree falls on to it first.
		const Fixed keep = Fixed::FromRatio(85, 100);
		const Fixed floor = ground.At(position.XY());
		const bool aloft = position.z > floor;
		crash.velocity = {crash.velocity.x * keep, crash.velocity.y * keep, aloft ? crash.velocity.z + crash.fall : Fixed{}};
		position.x += crash.velocity.x;
		position.y += crash.velocity.y;
		position.z = aloft ? position.z + crash.velocity.z : floor;
		if (const Fixed under = ground.At(position.XY()); position.z < under)
		{
			position.z = under;
			crash.velocity.z = {};
		}
		if (attitude != nullptr)
			attitude->pitch = attitude->pitch + Turn(how.pitchRate);
		if (tick >= crash.groundTick + how.finalDelay)
		{
			play(DeathPhase::FinalBlowUp);
			return true;
		}
		return false;
	}

	// One tick of a structure collapse (StructureCollapseUpdate::update): `play(phase)` plays that phase's effects.
	template<typename Random, typename Play>
	static void StepCollapse(Collapse &collapse, DrawOffset *offset, const CollapseDefinition &how, Engine::Math::Fixed gravity, std::uint64_t tick,
		Random &&random, Play &&play)
	{
		using Engine::Math::Fixed;
		const auto burstDelay = [&] {
			return static_cast<std::uint64_t>(random(static_cast<std::int64_t>(how.minBurstDelay), static_cast<std::int64_t>(std::max(how.minBurstDelay, how.maxBurstDelay))));
		};
		if (collapse.state == CollapseState::Waiting && tick >= collapse.collapseTick)
		{
			collapse.state = CollapseState::Collapsing;
			play(CollapsePhase::Burst);
			collapse.burstTick = tick + burstDelay();
		}
		if (collapse.state != CollapseState::Collapsing)
			return;
		collapse.height -= collapse.velocity;
		collapse.velocity -= gravity * (Fixed::One() - how.damping);
		if (tick >= collapse.burstTick)
		{
			play(random(1, std::max<std::int64_t>(how.bigBurstFrequency, 1)) == 1 ? CollapsePhase::Burst : CollapsePhase::Delay);
			collapse.burstTick += burstDelay();
		}
		if (collapse.height + how.height <= Fixed{})
		{
			collapse.state = CollapseState::Done;
			play(CollapsePhase::Final);
			collapse.height = Fixed{};
			if (offset != nullptr)
				*offset = DrawOffset{};
			return;
		}
		if (offset != nullptr)
			offset->z = collapse.height;
	}

	using Query = ecs::Query<ecs::Write<Transform>, ecs::Write<Dying>, ecs::Optional<TeamMember>, ecs::OptionalWrite<Crash>, ecs::OptionalWrite<Attitude>,
		ecs::OptionalWrite<Collapse>, ecs::OptionalWrite<DrawOffset>, ecs::Optional<Experience>, ecs::OptionalWrite<PhysicsBody>, ecs::Optional<SurfaceLayer>>;
	using Lookup = ecs::Lookup<ecs::Read<BoundingVolume>>;
	using Resources = ecs::Resources<ecs::Read<RandomSeed>, ecs::Read<DeathCatalog>, ecs::Read<GroundHeight>, ecs::Read<DeckSurfaces>, ecs::Read<PhysicsSettings>, ecs::Write<DyingEvents>,
		ecs::Write<Removals>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context)
	{
		DyingEvents &events = context.Write<DyingEvents>();
		Removals &done = context.Write<Removals>();
		events.Reset(query.PreparedChunkCount());
		done.Reset(query.PreparedChunkCount());
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const DeathCatalog &catalog = context.Read<DeathCatalog>();
		DyingEvents &events = context.Write<DyingEvents>();
		Removals &done = context.Write<Removals>();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0x51Du;
		const std::uint64_t tick = context.Tick();
		auto transforms = chunk.Get<Transform>();
		auto dyings = chunk.Get<Dying>();
		const auto members = chunk.Get<TeamMember>();
		auto crashes = chunk.Get<Crash>();
		auto attitudes = chunk.Get<Attitude>();
		auto collapses = chunk.Get<Collapse>();
		auto offsets = chunk.Get<DrawOffset>();
		const auto experiences = chunk.Get<Experience>();
		auto bodies = chunk.Get<PhysicsBody>();
		const Engine::Math::Fixed gravity = context.Read<PhysicsSettings>().gravity;
		const GroundHeight &ground = context.Read<GroundHeight>();
		const DeckSurfaces &decks = context.Read<DeckSurfaces>();
		const auto layers = chunk.Get<SurfaceLayer>();
		const auto lookup = context.Lookup<Lookup>();
		const auto entities = chunk.Entities();
		auto &played = events.Slot(context);
		auto &finished = done.Slot(context);
		for (std::size_t row = 0; row < dyings.size(); ++row)
		{
			Dying &dying = dyings[row];
			Transform &transform = transforms[row];
			const DeathDefinition &definition = catalog.At(dying.death);
			if (!collapses.empty() && collapses[row].state != CollapseState::Done && collapses[row].collapse < definition.collapses.size())
			{
				const CollapseDefinition &how = definition.collapses[collapses[row].collapse];
				auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation, 0xC011u});
				StepCollapse(collapses[row], offsets.empty() ? nullptr : &offsets[row], how, gravity, tick,
					[&](std::int64_t low, std::int64_t high) { return Engine::Math::UniformInt(random, low, high); },
					[&](CollapsePhase phase) {
						const auto at = static_cast<std::size_t>(phase);
						const std::uint32_t team = members.empty() ? NoTeam : members[row].team;
						if (const auto id = PickEffect(how.effects[at], random))
							played.push_back({entities[row], dying.killer, DeathEffectKind::Effect, *id, transform.position, transform.facing, team});
						if (const auto id = PickEffect(how.objects[at], random))
							played.push_back({entities[row], dying.killer, DeathEffectKind::Objects, *id, transform.position, transform.facing, team});
					});
			}
			if (dying.slow >= definition.slow.size())
				continue; // lingering: nothing removes it
			const SlowDeathDefinition &slow = definition.slow[dying.slow];
			// Flung and not down yet: its timers wait (from the tick after it died: its update first runs then); down
			// once it is no higher than the ground (its deck, on one: getLayerHeight).
			const auto floorOf = [&](Engine::Math::FixedVector2 at) { return layers.empty() ? ground.At(at) : LayerHeight(decks, ground, at, layers[row].layer); };
			if (dying.flung != 0 && dying.landed == 0 && tick > dying.since)
			{
				++dying.sinkTick;
				++dying.midpointTick;
				++dying.destructionTick;
				if (transform.position.z - floorOf(transform.position.XY()) <= Engine::Math::Fixed{})
					dying.landed = 1;
				// Into a tree on the way down (its last collidee a snag): caught in it (held, its physics let go), it sinks
				// fifty times its sink rate a tick and is gone (destroyObject) once no longer above the ground.
				if (const BoundingVolume *tree = dying.lastCollidee != ecs::Entity{} ? lookup.template Get<BoundingVolume>(dying.lastCollidee) : nullptr;
					tree != nullptr && tree->snag != 0)
				{
					if (dying.held == 0)
					{
						context.Commands().Remove<PhysicsBody>(entities[row]);
						dying.held = 1;
					}
					dying.snagged = 1;
					transform.position.z -= dying.sinkRate * Engine::Math::Fixed::FromInt(50);
					if (transform.position.z - floorOf(transform.position.XY()) <= Engine::Math::Fixed{})
					{
						finished.push_back(entities[row]);
						continue;
					}
				}
			}
			if (tick >= dying.sinkTick && dying.sinkRate > Engine::Math::Fixed{})
			{
				// Sinking holds it (DISABLED_HELD): its physics no longer move it.
				if (dying.flung != 0 && dying.held == 0)
				{
					context.Commands().Remove<PhysicsBody>(entities[row]);
					dying.held = 1;
				}
				transform.position.z -= dying.sinkRate;
			}
			const auto play = [&](DeathPhase phase, std::optional<Engine::Math::FixedVector3> at = std::nullopt) {
				auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation, static_cast<std::uint64_t>(phase)});
				for (std::uint8_t kind = 0; kind < DeathEffectKinds; ++kind)
					if (const auto id = PickEffect(slow.effects.Of(phase, static_cast<DeathEffectKind>(kind)), random))
						played.push_back({entities[row], dying.killer, static_cast<DeathEffectKind>(kind), *id, at.value_or(transform.position), transform.facing,
								members.empty() ? NoTeam : members[row].team});
			};
			if (!crashes.empty())
			{
				// A veteran's pilot bails out as the blades come off (better than regular: EjectPilotDie::ejectPilot), where it is,
				// with the unit's eject voice and sound; the pilot inherits its veterancy.
				const auto eject = [&] {
					const std::uint32_t veterancy = experiences.empty() ? 0u : experiences[row].level;
					if (slow.crash.ejectPilot == CrashDefinition::NoEject || veterancy == 0)
						return;
					const std::uint32_t team = members.empty() ? NoTeam : members[row].team;
					played.push_back({entities[row], dying.killer, DeathEffectKind::Objects, slow.crash.ejectPilot, transform.position, transform.facing, team, veterancy});
					for (const std::uint32_t sound : slow.crash.ejectSounds)
						if (sound != CrashDefinition::NoEject)
							played.push_back({entities[row], dying.killer, DeathEffectKind::Sound, sound, transform.position, transform.facing, team, veterancy});
				};
				// HelicopterSlowDeathBehavior::update reads the last collidee before it moves (the frame before's collisions).
				const BoundingVolume *snag = crashes[row].groundTick == 0 && crashes[row].lastCollidee != ecs::Entity{} ?
					lookup.template Get<BoundingVolume>(crashes[row].lastCollidee) : nullptr;
				if (StepCrash(crashes[row], slow.crash, transform, attitudes.empty() ? nullptr : &attitudes[row], ground, tick, play, eject,
						snag != nullptr && snag->snag != 0, bodies.empty() ? nullptr : &bodies[row], &context.Read<PhysicsSettings>()))
					finished.push_back(entities[row]);
				continue;
			}
			if (dying.midpointDone == 0 && tick >= dying.midpointTick)
			{
				play(DeathPhase::Midpoint);
				dying.midpointDone = 1;
			}
			if (tick >= dying.destructionTick)
			{
				play(DeathPhase::Final);
				finished.push_back(entities[row]);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::SlowDeathSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.slow_death";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	// After the deaths of this tick have started dying (next tick they play out).
	using Before = SystemTypeList<engine::gameplay::RemovalSystem>;
	using After = SystemTypeList<engine::gameplay::DeathSystem>;
};
}
