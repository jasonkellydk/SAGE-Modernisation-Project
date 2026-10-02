export module engine.gameplay.rts.slaves.systems.slaved_system;
import std;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.gameplay.rts.slaves.components.slaved;
export import engine.gameplay.rts.slaves.resources.slave_orders;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.body_extent;
export import engine.gameplay.common.spatial.components.surface_layer;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.healing.resources.heal_pulses;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.common.physics.components.physics_body;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.slaves.algorithms.enslave;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
import Engine.Core.Math.FixedRandom;

// SlavedUpdate::update: a slave's wait counts down every tick; out of a repair
// it looks when that runs out (then waits SLAVED_UPDATE_RATE again), in a
// repair state every tick. A master gone, dead or unmanned leaves it
// DISABLED_UNMANNED and idle: its AI stops, so its locomotor no longer holds it
// and physics brings it down, going on at the speed it flew (the original's
// physics already had it), to be killed at rest on the ground. Else it takes
// its master's layer (StayOnSameLayerAsMaster), then by priority: its master's
// health (a whole percent, truncated) at or below RepairWhenBelowHealth%, it
// repairs; with an AttackRange it heads for its master's victim, or as near as
// that range from the master allows, wandering by AttackWanderRange; with a
// ScoutRange it heads for its master's move destination while the master is
// more than half its GuardMaxRange from it (ScoutRange, ScoutWanderRange
// alike); a master below full health it repairs; else it guards: idle and more
// than 15 from its guard point (the master plus its offset), or anywhere more
// than twice GuardMaxRange from its master, it picks a new offset (GuardMaxRange
// in a random direction, with a GuardWanderRange) and heads for the point.
// Attacking, scouting and guarding end a repair first (endRepair: its arm packed,
// its normal locomotor). Only a drone with a RepairRatePerSecond repairs (its
// master's health counts as full otherwise).
// Repairing (doRepairLogic): more than 12 from its master (bounding circles) it
// heads over the master at a random altitude (RepairMin/MaxAltitude), at that
// precise height once within twice the master's bounding sphere; its arm
// retracts to ready when due. Within 12 its arm works through its states
// (setRepairState: unpacking 15 ticks, ready RepairMin/MaxReadyTime, extending 5,
// welding RepairMin/MaxWeldTime with sparks, retracting 5 and off to a new spot
// RepairRange about the master on its panic locomotor, ultra-accurate at a precise
// height over the ground), healing its master RepairRatePerSecond / 30 a tick once
// its first sparks flew. Its arm's look is the repair look (packing from creation).
// Retail bug fixed: moveToNewRepairSpot kept its spot in m_guardPointOffset, so the
// guard point after a repair was the master plus that absolute spot; the spot is
// kept apart here and the guard offset stays the guard offset.
// Moves go through the tick's commands, locomotor changes through SlaveOrders.
// Distances as the original's partition manager: to the victim and destinations
// from the slave's bounding circle, the master's distance to its destination from
// the master's, the slave's to its master from both (FROM_BOUNDINGSPHERE_2D: the
// circles' radii off the centre distance, never below zero); to the guard point
// (at the master's height) and to the master centre to centre in 3D. Directions
// are a whole number of radians (GameLogicRandomValue(0, 2*PI)).
// Spotting (drones): each look first clears the master's spotting bonus; heading
// for the victim from within DistToTargetToGrantRangeBonus of it (measured
// before it moves) sets it again. Every change re-times the master's weapons
// (WeaponBonusRetimeSystem), as the original's did.
export namespace engine::gameplay
{
namespace slaved_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;
using Engine::Math::FixedVector3;

// distCalcProc_BoundaryAndBoundary_2D against a point: the centre distance less `radius`, never below zero.
inline Fixed BoundaryDistance(FixedVector2 from, Fixed radius, FixedVector2 to)
{
	const Fixed distance = Engine::Math::Length(to - from) - radius;
	return distance > Fixed{} ? distance : Fixed{};
}

// doAttackLogic / doScoutLogic: the goal, or as near as `range` from the master allows, then wandering.
inline FixedVector2 Approach(Slaved &slave, FixedVector2 me, FixedVector2 master, FixedVector2 goal, Fixed range, Fixed wander, Engine::Math::RandomStream &random)
{
	FixedVector2 position = goal;
	if (BoundaryDistance(me, slave.radius, goal) > range)
	{
		const FixedVector2 toward = goal - master;
		const Fixed length = Engine::Math::Length(toward);
		position = length > Fixed{} ? master + toward * (range / length) : master;
	}
	if (wander > Fixed{})
	{
		slave.guardOffset = RandomReach(random, wander);
		position = position + slave.guardOffset;
	}
	return position;
}

// The master's health as a whole percent, truncated ((Int)(health / maxHealth * 100)).
inline std::int64_t HealthPercent(const Health &health) noexcept
{
	if (health.maximum <= Fixed{})
		return 100;
	return health.current.Raw() * 100 / health.maximum.Raw();
}

inline constexpr std::uint32_t ArmTicks = 15;       // unpacking
inline constexpr std::uint32_t ExtendTicks = 5;     // extending, retracting
inline constexpr std::int64_t CloseEnough = 12;     // doRepairLogic: within 12 it repairs
inline constexpr std::uint32_t LogicFramesPerSecond = 30;
}

struct SlavedSystem
{
	using Query = ecs::Query<ecs::Write<Slaved>, ecs::Read<Transform>, ecs::Optional<MoveOrder>, ecs::Optional<Locomotion>, ecs::Optional<PhysicsBody>,
		ecs::Optional<Disabled>, ecs::Optional<SurfaceLayer>>;
	using Lookup = ecs::Lookup<ecs::Read<Transform>, ecs::Read<Health>, ecs::Read<AttackTarget>, ecs::Read<MoveOrder>, ecs::Read<Disabled>,
		ecs::Read<WeaponBonusConditions>, ecs::Read<BodyExtent>, ecs::Read<SurfaceLayer>>;
	using Resources = ecs::Resources<ecs::Read<RandomSeed>, ecs::Read<GroundHeight>, ecs::Write<SlaveOrders>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		using namespace slaved_detail;
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		const std::uint64_t tick = context.Tick();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0x51A7u;
		const GroundHeight &ground = context.Read<GroundHeight>();
		SlaveOrders &out = context.Write<SlaveOrders>();
		out.locomotors.clear();
		out.welds.clear();
		out.heals.clear();
		// Masters' conditions as this tick's looks leave them, in order (committed after the pass).
		std::vector<std::pair<ecs::Entity, WeaponBonusConditions>> masters;
		const auto spotting = [&](ecs::Entity master, std::uint32_t bits, bool on) {
			if (bits == 0)
				return;
			auto found = std::find_if(masters.begin(), masters.end(), [&](const auto &entry) { return entry.first == master; });
			if (found == masters.end())
			{
				const WeaponBonusConditions *now = lookup.Get<WeaponBonusConditions>(master);
				if (now == nullptr)
					return;
				masters.emplace_back(master, *now);
				found = masters.end() - 1;
			}
			SetWeaponBonus(found->second, bits, on, tick);
		};
		query.ForEachChunk([&](auto chunk) {
			auto slaves = chunk.template Get<Slaved>();
			const auto transforms = chunk.template Get<Transform>();
			const auto orders = chunk.template Get<MoveOrder>();
			const auto locomotions = chunk.template Get<Locomotion>();
			const auto bodies = chunk.template Get<PhysicsBody>();
			const auto entities = chunk.Entities();
			const auto disabledRows = chunk.template Get<Disabled>();
			const auto layerRows = chunk.template Get<SurfaceLayer>();
			for (std::size_t row = 0; row < slaves.size(); ++row)
			{
				if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::None))
					continue;
				Slaved &slave = slaves[row];
				if (slave.waitTicks > 0)
					--slave.waitTicks;
				if (slave.repairState == SlaveRepairState::None)
				{
					if (slave.waitTicks > 0)
						continue;
					slave.waitTicks = SlavedUpdateTicks;
				}
				if (slave.enslaved == 0 || orders.empty())
					continue;
				const ecs::Entity self = entities[row];
				const Transform *master = lookup.IsAlive(slave.master) ? lookup.Get<Transform>(slave.master) : nullptr;
				const Health *masterHealth = master != nullptr ? lookup.Get<Health>(slave.master) : nullptr;
				const Disabled *masterOff = master != nullptr ? lookup.Get<Disabled>(slave.master) : nullptr;
				if (master == nullptr || (masterHealth != nullptr && IsDead(*masterHealth)) ||
					(masterOff != nullptr && (masterOff->mask & disabled_type::Unmanned) != 0))
				{
					// stopSlavedEffects; setDisabled(DISABLED_UNMANNED); aiIdle.
					slave.master = {};
					slave.enslaved = 0;
					const Disabled *mine = lookup.Get<Disabled>(self);
					if (mine != nullptr)
						commands.Set<Disabled>(self, Disabled{mine->mask | disabled_type::Unmanned});
					else
						commands.Add<Disabled>(self, Disabled{disabled_type::Unmanned});
					commands.Set<MoveOrder>(self, MoveOrder{});
					if (!bodies.empty() && !locomotions.empty())
					{
						PhysicsBody body = bodies[row];
						const auto heading = Engine::Math::Direction(transforms[row].facing);
						const Fixed speed = locomotions[row].speed;
						body.velocity = {heading.x * speed, heading.y * speed, body.velocity.z};
						commands.Set<PhysicsBody>(self, body);
					}
					continue;
				}
				const SlavedDefinition &d = slave.definition;
				// StayOnSameLayerAsMaster: setLayer(master->getLayer()).
				if (d.stayOnMasterLayer != 0)
				{
					const SurfaceLayer *theirs = lookup.Get<SurfaceLayer>(slave.master);
					const std::uint8_t wanted = theirs != nullptr ? theirs->layer : std::uint8_t{0};
					const std::uint8_t mine = layerRows.empty() ? std::uint8_t{0} : layerRows[row].layer;
					if (wanted != mine)
					{
						if (wanted == 0)
							commands.Remove<SurfaceLayer>(self);
						else if (layerRows.empty())
							commands.Add<SurfaceLayer>(self, SurfaceLayer{wanted});
						else
							commands.Set<SurfaceLayer>(self, SurfaceLayer{wanted});
					}
				}
				// Clear the drone spotting bonus; up to the drone to earn it again this look.
				spotting(slave.master, slave.definition.spottingBonus, false);
				auto random = Engine::Math::Stream(seed, {tick, self.index, self.generation});
				const FixedVector2 me = transforms[row].position.XY();
				const FixedVector2 home = master->position.XY();
				const Locomotion *motion = locomotions.empty() ? nullptr : &locomotions[row];

				// endRepair: out of any repair state (its arm packs, it waits a look again), on its normal locomotor, neither
				// ultra-accurate nor at a precise height (nothing to ask when it is so already).
				const auto endRepair = [&] {
					if (slave.repairState != SlaveRepairState::None)
					{
						slave.repairState = SlaveRepairState::None;
						slave.waitTicks = SlavedUpdateTicks;
						slave.repairing = 0;
						slave.repairLook = SlaveRepairLook::Packing;
					}
					if (motion != nullptr && (motion->set != 0 || motion->preciseZ != 0 || motion->ultraAccurate != 0))
						out.locomotors.push_back({self, Fixed{}, SlaveLocomotorSet::Normal, 0, 0});
				};
				// moveToNewRepairSpot: RepairRange from its master in a random direction, at a random altitude over the ground
				// there, on its panic locomotor, ultra-accurate, at that precise height.
				const auto moveToNewRepairSpot = [&] {
					if (d.repairRange == Fixed{})
						return;
					const FixedVector2 spot = home + RandomReach(random, d.repairRange);
					const Fixed height = ground.At(spot) + Engine::Math::UniformFixed(random, d.repairMinAltitude, d.repairMaxAltitude);
					out.locomotors.push_back({self, height, SlaveLocomotorSet::Panic, 1, 1});
					commands.Set<MoveOrder>(self, Replanned(MoveToPoint(spot)));
				};
				const auto setRepairState = [&](SlaveRepairState to) {
					if (to == slave.repairState)
						return;
					if (to == SlaveRepairState::Ready)
					{
						switch (slave.repairState)
						{
						case SlaveRepairState::None:
							// Not in a repair state: it unpacks first.
							slave.repairLook = SlaveRepairLook::Unpacking;
							slave.repairState = SlaveRepairState::Unpacking;
							slave.waitTicks = ArmTicks;
							break;
						case SlaveRepairState::Welding:
							// Welding: it retracts before it is ready, and moves on.
							slave.repairState = SlaveRepairState::Retracting;
							slave.waitTicks = ExtendTicks;
							slave.repairLook = SlaveRepairLook::FiringC;
							moveToNewRepairSpot();
							break;
						default:
							slave.repairState = SlaveRepairState::Ready;
							slave.waitTicks = static_cast<std::uint32_t>(Engine::Math::UniformInt(random, d.minReadyTicks, d.maxReadyTicks));
							break;
						}
					}
					else if (to == SlaveRepairState::Welding)
					{
						if (slave.repairState == SlaveRepairState::Ready)
						{
							slave.repairState = SlaveRepairState::Extending;
							slave.waitTicks = ExtendTicks;
							slave.repairLook = SlaveRepairLook::FiringB;
						}
						else
						{
							slave.repairState = SlaveRepairState::Welding;
							slave.waitTicks = static_cast<std::uint32_t>(Engine::Math::UniformInt(random, d.minWeldTicks, d.maxWeldTicks));
							out.welds.push_back({self, slave.waitTicks * LogicFramesPerSecond});
							// It heals only once its first sparks flew.
							slave.repairing = 1;
						}
					}
				};
				const auto doRepairLogic = [&] {
					const Fixed apart = std::max(Fixed{}, Engine::Math::Length(home - me) - slave.radius - slave.masterRadius);
					const bool closeEnough = apart < Fixed::FromInt(CloseEnough);
					if (closeEnough)
					{
						switch (slave.repairState)
						{
						case SlaveRepairState::None:
							setRepairState(SlaveRepairState::Ready);
							break;
						case SlaveRepairState::Ready:
						case SlaveRepairState::Extending:
							if (slave.waitTicks == 0)
								setRepairState(SlaveRepairState::Welding);
							break;
						case SlaveRepairState::Unpacking:
						case SlaveRepairState::Welding:
						case SlaveRepairState::Retracting:
							if (slave.waitTicks == 0)
								setRepairState(SlaveRepairState::Ready);
							break;
						}
					}
					else
					{
						slave.repairing = 0;
						const BodyExtent *extent = lookup.Get<BodyExtent>(slave.master);
						const Fixed sphere = extent != nullptr ? extent->sphereRadius * Fixed::FromInt(2) : Fixed{};
						const Fixed height = master->position.z + Engine::Math::UniformFixed(random, d.repairMinAltitude, d.repairMaxAltitude);
						out.locomotors.push_back({self, height, SlaveLocomotorSet::Keep, apart < sphere ? std::uint8_t{1} : std::uint8_t{0}, SlaveLocomotorOrder::Keep});
						commands.Set<MoveOrder>(self, Replanned(MoveToPoint(home)));
						// Its arm retracts on the way, so it shows what it means to do.
						if (slave.waitTicks == 0)
							setRepairState(SlaveRepairState::Ready);
					}
					if (closeEnough && slave.repairing != 0 && masterHealth != nullptr)
						out.heals.push_back({slave.master, self, d.repairPerTick, 0, master->position});
				};

				const std::int64_t percent = d.repairPerTick > Fixed{} && masterHealth != nullptr ? HealthPercent(*masterHealth) : 100;
				// 1ST PRIORITY: its master needs repairing.
				if (percent <= d.repairBelowPercent)
				{
					doRepairLogic();
					continue;
				}
				// 2ND: the master's victim.
				if (d.attackRange > Fixed{})
					if (const AttackTarget *attack = lookup.Get<AttackTarget>(slave.master); attack != nullptr && lookup.IsAlive(attack->target))
						if (const Transform *victim = lookup.Get<Transform>(attack->target))
						{
							endRepair();
							const Fixed away = BoundaryDistance(me, slave.radius, victim->position.XY());
							commands.Set<MoveOrder>(self, Replanned(MoveToPoint(Approach(slave, me, home, victim->position.XY(), d.attackRange, d.attackWanderRange, random))));
							if (away < d.spottingRange)
								spotting(slave.master, slave.definition.spottingBonus, true);
							continue;
						}
				// 3RD: scout ahead of a master on the move.
				if (d.scoutRange > Fixed{})
					if (const MoveOrder *going = lookup.Get<MoveOrder>(slave.master); going != nullptr && going->mode == MoveMode::Point)
					{
						const Fixed half = d.guardMaxRange / Fixed::FromInt(2);
						if (BoundaryDistance(home, slave.masterRadius, going->destination) > half)
						{
							endRepair();
							commands.Set<MoveOrder>(self, Replanned(MoveToPoint(Approach(slave, me, home, going->destination, d.scoutRange, d.scoutWanderRange, random))));
							continue;
						}
					}
				// Idle: repair a master below full health.
				if (percent < 100)
				{
					doRepairLogic();
					continue;
				}
				// Guard the master's area.
				if (d.guardMaxRange <= Fixed{})
					continue;
				FixedVector2 pinned = home + slave.guardOffset;
				const FixedVector3 at = transforms[row].position;
				const FixedVector3 pinned3{pinned.x, pinned.y, master->position.z}; // the master's height (its position plus the offset)
				const bool idle = orders[row].mode == MoveMode::Idle;
				const Fixed stray = d.guardMaxRange * Fixed::FromInt(2);
				if ((idle && Engine::Math::LengthSquared(at - pinned3) > Fixed::FromInt(15 * 15)) ||
					Engine::Math::LengthSquared(at - master->position) > stray * stray)
				{
					endRepair();
					if (d.guardWanderRange > Fixed{})
					{
						// doGuardLogic: a new offset of GuardMaxRange (as the original), on top of the old pinned point.
						slave.guardOffset = RandomReach(random, d.guardMaxRange);
						pinned = pinned + slave.guardOffset;
					}
					commands.Set<MoveOrder>(self, Replanned(MoveToPoint(pinned)));
				}
			}
		});
		for (const auto &[master, conditions] : masters)
			commands.Set<WeaponBonusConditions>(master, conditions);
	}
};

// The repairs slaves made this tick (body->attemptHealing: DAMAGE_HEALING, which no armour filters) join the tick's
// heal pulses, after the area heals gather them and before they are applied.
struct SlaveRepairSystem
{
	using Query = ecs::Query<ecs::Read<Slaved>>;
	using Resources = ecs::Resources<ecs::Read<SlaveOrders>, ecs::Write<HealPulses>>;

	void Execute(ecs::SystemContext &context) const
	{
		const SlaveOrders &orders = context.Read<SlaveOrders>();
		if (orders.heals.empty())
			return;
		HealPulses &pulses = context.Write<HealPulses>();
		for (const HealPulse &pulse : orders.heals)
			pulses.Add(pulse);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::SlavedSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.slaved";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<engine::gameplay::SlaveRepairSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.slave_repair";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the slaves and the area heals, before the heals are applied.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
