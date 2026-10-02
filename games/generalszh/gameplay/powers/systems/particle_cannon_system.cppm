export module games.generalszh.gameplay.powers.systems.particle_cannon_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.powers.algorithms.particle_cannons;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.rts.powers.algorithms.special_power_timing;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.health.resources.incoming_damage;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.construction.components.sale;
export import engine.gameplay.rts.death.components.dying;
import Engine.Core.Math.FixedRandom;

// ParticleUplinkCannonUpdate::update, each tick for built, living uplinks (a batch: few of them; their damage and
// events go in shared lists):
//   sold: idle, everything off;
//   its attack under way (from startAttack): disabled (underpowered, EMP, subdued, hacked) its beam starts to decay at
//   once; the orbital beam is born BeamTravelTime after the attack starts (widening over WidthGrowTime), decays
//   BeamTravelTime after the decay starts, gone WidthGrowTime later (the attack over: idle); while the beam is alive it
//   is swept across the target by itself (the swath of death), or driven toward where the player clicks (twice
//   quickly: fast) or along a script's waypoints; on the ground there; every TotalFiringTime / TotalScorchMarks a
//   scorch mark (radius ScorchMarkScalar times the beam's) and its GroundHitFX; every TotalFiringTime /
//   TotalDamagePulses a pulse of DamagePerSecond's share to everything alive within DamageRadiusScalar times the beam's
//   radius (2D, centres), leaving its remnant; firing, then after the beam, then packing up;
//   else by how near its power is to ready: charging BeginChargeTime before its antenna rises, preparing RaiseAntennaTime
//   before it is almost ready, almost ready ReadyDelayTime before ready, ready; ready and not charging (fired by
//   someone else's timer), it packs up;
//   firing, its BeamLaunchFX every DelayBetweenLaunchFX where the beam leaves it.
// The revealing ping (doShroudReveal then undoShroudReveal in the same frame) leaves the shroud as it was: not modelled.
export namespace generalszh::gameplay
{
struct ParticleCannonSystem
{
	using Query = ecs::Query<ecs::Write<ParticleCannon>, ecs::Read<engine::gameplay::SpecialPowerTimers>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::TeamMember>, ecs::Read<engine::gameplay::Transform>,
		ecs::Optional<engine::gameplay::Disabled>, ecs::Optional<engine::gameplay::Sale>, ecs::Exclude<engine::gameplay::UnderConstruction>,
		ecs::Exclude<engine::gameplay::Dying>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::SpecialPowerRules>, ecs::Read<engine::gameplay::SharedPowerTimers>,
		ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::GroundHeight>, ecs::Read<engine::gameplay::WaypointGraph>,
		ecs::Read<engine::gameplay::RandomSeed>, ecs::Write<engine::gameplay::IncomingDamage>, ecs::Write<ParticleCannonEvents>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const gp::SpecialPowerRules &rules = context.Read<gp::SpecialPowerRules>();
		const gp::SharedPowerTimers &shared = context.Read<gp::SharedPowerTimers>();
		const gp::SpatialIndex &spatial = context.Read<gp::SpatialIndex>();
		const gp::GroundHeight &ground = context.Read<gp::GroundHeight>();
		const gp::WaypointGraph &graph = context.Read<gp::WaypointGraph>();
		const std::uint64_t seed = context.Read<gp::RandomSeed>().value;
		gp::IncomingDamage &incoming = context.Write<gp::IncomingDamage>();
		ParticleCannonEvents &events = context.Write<ParticleCannonEvents>();
		const std::uint64_t now = context.Tick();
		bool damaged = false;
		query.ForEachChunk([&](auto chunk) {
			auto cannons = chunk.template Get<ParticleCannon>();
			const auto timerSets = chunk.template Get<gp::SpecialPowerTimers>();
			const auto definitions = chunk.template Get<gp::DefinitionRef>();
			const auto owners = chunk.template Get<gp::Owner>();
			const auto members = chunk.template Get<gp::TeamMember>();
			const auto transforms = chunk.template Get<gp::Transform>();
			const auto disabledRows = chunk.template Get<gp::Disabled>();
			const auto sales = chunk.template Get<gp::Sale>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < cannons.size(); ++row)
			{
				ParticleCannon &cannon = cannons[row];
				const ParticleCannonConfig *config = templates.ParticleCannonOf(definitions[row].index);
				const gp::SpecialPowerTimer *timer = config != nullptr ? timerSets[row].Find(config->power) : nullptr;
				if (timer == nullptr)
					continue;
				// Sold: idle, its effects gone (killEverything).
				if (!sales.empty())
				{
					if (cannon.status != CannonStatus::Idle)
					{
						SetCannonStatus(cannon, CannonStatus::Idle, events, entities[row]);
						if (cannon.beam == BeamStatus::Born || cannon.beam == BeamStatus::Decaying)
							events.changes.push_back({entities[row], ParticleCannonEvents::Change::Kind::BeamGone});
						cannon.beam = BeamStatus::Dead;
						cannon.startAttackTick = 0;
					}
					continue;
				}
				const std::uint32_t disabledMask = disabledRows.empty() ? 0u : disabledRows[row].mask;
				const bool disabled = disabledMask != 0;
				const std::uint32_t player = owners[row].player;
				const bool ready = gp::PeekIsReady(*timer, rules, shared, player, now);
				const std::uint64_t readyTick = ready ? now : gp::PeekReadyFrame(*timer, disabled, rules, shared, player, now);
				// UnsignedInt arithmetic, as the original (a frame before 0 wraps round: never reached).
				const auto before = [](std::uint64_t tick, std::uint64_t ticks) { return tick >= ticks ? tick - ticks : std::uint64_t{0}; };
				const std::uint64_t almostReady = before(readyTick, config->readyDelayTicks);
				const std::uint64_t raiseAntenna = before(almostReady, config->raiseAntennaTicks);
				const std::uint64_t beginCharge = before(raiseAntenna, config->beginChargeTicks);
				const gp::Transform &me = transforms[row];
				if (cannon.startAttackTick != 0 && cannon.startAttackTick <= now)
				{
					using namespace gp::disabled_type;
					if (cannon.startDecayTick > now && (disabledMask & (Underpowered | Emp | Subdued | Hacked)) != 0)
						cannon.startDecayTick = now;
					const std::uint64_t endDecay = cannon.startDecayTick + config->widthGrowTicks;
					const std::uint64_t birth = cannon.startAttackTick + config->beamTravelTicks;
					const std::uint64_t decayStart = cannon.startDecayTick + config->beamTravelTicks;
					const std::uint64_t death = decayStart + config->widthGrowTicks;
					switch (cannon.beam)
					{
					case BeamStatus::None:
						if (birth <= now)
						{
							WidenBeam(cannon, now, config->widthGrowTicks);
							cannon.decaying = 0;
							cannon.beam = BeamStatus::Born;
							events.changes.push_back({entities[row], ParticleCannonEvents::Change::Kind::BeamBorn});
							cannon.scorchMarks = 0;
							cannon.nextScorchTick = now;
							cannon.pulses = 0;
							cannon.nextPulseTick = now;
						}
						break;
					case BeamStatus::Born:
						if (decayStart <= now)
						{
							DecayBeam(cannon, now, config->widthGrowTicks);
							cannon.beam = BeamStatus::Decaying;
						}
						break;
					case BeamStatus::Decaying:
						if (death <= now)
						{
							cannon.widening = cannon.decaying = 0;
							cannon.widthScale = Fixed::One();
							cannon.beam = BeamStatus::Dead;
							events.changes.push_back({entities[row], ParticleCannonEvents::Change::Kind::BeamGone});
							cannon.startAttackTick = 0;
							SetCannonStatus(cannon, CannonStatus::Idle, events, entities[row]);
						}
						break;
					case BeamStatus::Dead:
						break;
					}
					if (birth <= now && now < death)
					{
						if (cannon.manual == 0 && cannon.scripted == 0)
						{
							// The swath of death: across the target, swinging to either side along a sine.
							const Fixed factor = death > birth ? Fixed::FromInt(static_cast<std::int64_t>(now - birth)) / Fixed::FromInt(static_cast<std::int64_t>(death - birth))
								: Fixed{};
							const Engine::Math::TurnAngle phase{static_cast<std::uint32_t>((factor.Raw() << (32 - Fixed::FractionBits)) + 0x80000000u)};
							const Fixed across = factor * config->swathDistance - config->swathDistance / Fixed::FromInt(2);
							const Fixed side = Engine::Math::Sin(phase) * config->swathAmplitude;
							const Fixed distance = Engine::Math::Length(cannon.initialTarget - me.position) + across;
							const Engine::Math::FixedVector2 cartesian{distance, side};
							const Fixed length = Engine::Math::Length(cartesian);
							Engine::Math::FixedVector2 toward = cannon.initialTarget.XY() - me.position.XY();
							const Fixed towardLength = Engine::Math::Length(toward);
							toward = towardLength > Fixed{} ? toward / towardLength : Engine::Math::FixedVector2{};
							const Engine::Math::FixedVector2 local = length > Fixed{} ? cartesian / length : Engine::Math::FixedVector2{};
							const Fixed limit = Fixed::FromRatio(99999, 100000);
							const Fixed dot = std::clamp(Engine::Math::Dot(toward, local), -limit, limit);
							// acos(dot), turned against the building's side of the target as the original does.
							Engine::Math::TurnAngle angle = Engine::Math::Atan2(Engine::Math::Sqrt(Fixed::One() - dot * dot), dot);
							if (toward.y < Fixed{})
								angle = Engine::Math::TurnAngle{0u - angle.units};
							cannon.currentTarget.x = me.position.x + Engine::Math::Cos(angle) * length;
							cannon.currentTarget.y = me.position.y + Engine::Math::Sin(angle) * length;
						}
						else
						{
							Fixed speed = config->drivingSpeed;
							if (cannon.scripted != 0 || cannon.lastClickTick - cannon.secondLastClickTick < config->doubleClickTicks)
								speed = config->fastDrivingSpeed;
							speed = speed / Fixed::FromInt(30);
							Engine::Math::FixedVector3 vector = cannon.destination - cannon.currentTarget;
							const Fixed distance = Engine::Math::Length(vector);
							if (distance < speed)
							{
								speed = distance;
								if (cannon.scripted != 0 && cannon.nextWaypoint < graph.Size())
								{
									const auto links = graph.Links(cannon.nextWaypoint);
									if (!links.empty())
									{
										auto random = Engine::Math::Stream(seed, {now, entities[row].index, 0x9C1u});
										const auto which = static_cast<std::size_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(links.size()) - 1));
										cannon.nextWaypoint = links[which];
										cannon.destination = graph.Position(links[which]);
									}
									else
										cannon.nextWaypoint = 0xFFFFFFFFu;
								}
							}
							if (distance > Fixed{})
								vector = vector / distance * speed;
							cannon.currentTarget.x = cannon.currentTarget.x + vector.x;
							cannon.currentTarget.y = cannon.currentTarget.y + vector.y;
						}
						cannon.currentTarget.z = ground.At(cannon.currentTarget.XY());
						UpdateBeamWidth(cannon, now);
						const Fixed radius = config->beamRadius * cannon.widthScale;
						const Fixed damageRadius = radius * config->damageRadiusScalar;
						const Fixed scorchRadius = radius * config->scorchScalar;
						const auto share = [&](std::uint32_t made, std::uint32_t total) {
							return total == 0 ? death : birth + (Fixed::FromInt(static_cast<std::int64_t>(made)) / Fixed::FromInt(static_cast<std::int64_t>(total)) *
								Fixed::FromInt(static_cast<std::int64_t>(death - birth))).Floor();
						};
						if (cannon.nextScorchTick <= now)
						{
							++cannon.scorchMarks;
							events.scorches.push_back({cannon.currentTarget, scorchRadius});
							cannon.nextScorchTick = static_cast<std::uint64_t>(share(cannon.scorchMarks, config->totalScorchMarks));
							if (config->groundHitEffect != 0xFFFFFFFFu)
								events.played.push_back({config->groundHitEffect, cannon.currentTarget});
						}
						if (cannon.nextPulseTick <= now)
						{
							++cannon.pulses;
							const Fixed seconds = Fixed::FromInt(static_cast<std::int64_t>(config->totalFiringTicks)) / Fixed::FromInt(30);
							const Fixed amount = config->totalPulses == 0 ? Fixed{} : seconds * config->damagePerSecond / Fixed::FromInt(static_cast<std::int64_t>(config->totalPulses));
							const auto center = cannon.currentTarget.XY();
							spatial.ForEachWithin(center, damageRadius, [&](const gp::SpatialEntry &entry) {
								if (Engine::Math::DistanceSquared(entry.position.XY(), center) > damageRadius * damageRadius)
									return;
								incoming.Add({entry.entity, entities[row], amount, config->damageType, config->deathType});
								damaged = true;
							});
							if (!config->remnant.empty())
								events.remnants.push_back({definitions[row].index, members[row].team, cannon.currentTarget});
							cannon.nextPulseTick = static_cast<std::uint64_t>(share(cannon.pulses, config->totalPulses));
						}
					}
					// Even the tick its beam ends (idle): it packs up then (the original's end of the attack block).
					SetCannonStatus(cannon, endDecay <= now ? CannonStatus::Packing : cannon.startDecayTick <= now ? CannonStatus::PostFire : CannonStatus::Firing,
						events, entities[row]);
				}
				else if (readyTick <= now)
					SetCannonStatus(cannon, CannonStatus::ReadyToFire, events, entities[row]);
				else if (almostReady <= now)
					SetCannonStatus(cannon, CannonStatus::AlmostReady, events, entities[row]);
				else if (raiseAntenna <= now)
					SetCannonStatus(cannon, CannonStatus::Preparing, events, entities[row]);
				else if (beginCharge <= now)
					SetCannonStatus(cannon, CannonStatus::Charging, events, entities[row]);
				else if (cannon.status == CannonStatus::ReadyToFire)
					SetCannonStatus(cannon, CannonStatus::Packing, events, entities[row]);
				if (cannon.status == CannonStatus::Firing && cannon.nextLaunchFxTick <= now)
				{
					if (config->launchEffect != 0xFFFFFFFFu)
						events.played.push_back({config->launchEffect, me.position});
					cannon.nextLaunchFxTick = now + config->launchFxTicks;
				}
			}
		});
		if (damaged)
			incoming.Seal();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::ParticleCannonSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.particle_cannon";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
