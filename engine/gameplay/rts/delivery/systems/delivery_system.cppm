export module engine.gameplay.rts.delivery.systems.delivery_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.delivery.components.delivery;
export import engine.gameplay.rts.containment.systems.unloading_system;
export import engine.gameplay.rts.containment.systems.boarding_system;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.combat.algorithms.spot_fire;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
import Engine.Core.Math.FixedRandom;

// Flies payload carriers through their runs (DeliverPayloadAIUpdate::update -> DeliverPayloadStateMachine, then its
// dive), each tick before it moves, chunk-parallel. As the original's state machine: the current state's conditions
// first (considering a new approach off the map: recover), then its update; a success or failure enters the next state
// at once (its onEnter), whose update waits for the next tick. The riders it drops are taken out after the step
// (DropExits), the riders it fires off as weapons removed (DeliveriesDone), its visible payload items made by the game
// (VisibleDrops), the shots it fires from its own weapon put among the tick's shots (DirectShots). A carrier disabled
// otherwise than HELD does nothing (GameLogic runs its AIUpdate only while HELD).
export namespace engine::gameplay
{
struct DeliverySystem
{
	using Query = ecs::Query<ecs::Write<Delivery>, ecs::Write<Transform>, ecs::Write<MoveOrder>, ecs::OptionalWrite<Locomotion>,
		ecs::OptionalWrite<Appearance>, ecs::OptionalWrite<Armament>, ecs::OptionalWrite<WeaponSlots>, ecs::OptionalWrite<FiringTracker>,
		ecs::Optional<WeaponBonusConditions>, ecs::Optional<DefinitionRef>, ecs::Optional<Experience>, ecs::Optional<Owner>, ecs::Optional<Disabled>,
		ecs::Exclude<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<CargoManifest>, ecs::Read<GroundHeight>, ecs::Read<RandomSeed>, ecs::Write<DeliveriesDone>,
		ecs::Write<DropExits>, ecs::Read<WeaponCatalog>, ecs::Read<LaunchLayouts>, ecs::Write<DirectShots>, ecs::Write<VisibleDrops>,
		ecs::Write<DeliveryCues>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context)
	{
		const std::size_t chunks = query.PreparedChunkCount();
		context.Write<DeliveriesDone>().Reset(chunks);
		context.Write<DropExits>().Reset(chunks);
		context.Write<DirectShots>().Reset(chunks);
		context.Write<VisibleDrops>().Reset(chunks);
		context.Write<DeliveryCues>().Reset(chunks);
	}

	// calcMinTurnRadius: its top speed over its turn rate (none: 999999), and the ticks to fly that far.
	static std::pair<Engine::Math::Fixed, Engine::Math::Fixed> MinTurnRadius(const Locomotion *motion) noexcept
	{
		using Engine::Math::Fixed;
		if (motion == nullptr)
			return {Fixed::FromInt(999999), Fixed{}};
		const Fixed speed = motion->locomotor.maxSpeed;
		const Fixed rate = Engine::Math::Radians(motion->locomotor.turnRate);
		const Fixed radius = rate > Fixed{} ? speed / rate : Fixed::FromInt(999999);
		return {radius, speed > Fixed{} ? radius / speed : Fixed{}};
	}

	// isCloseEnoughToTarget: within the delivery distance of the target (2D), plus the pre-open distance while
	// inbound (nearer than at the last test, which this one becomes).
	static bool CloseEnough(Delivery &delivery, Engine::Math::FixedVector2 at) noexcept
	{
		using Engine::Math::Fixed;
		const Fixed current = Engine::Math::DistanceSquared(at, delivery.target);
		const bool inbound = delivery.previousDistance > current;
		delivery.previousDistance = current;
		const Fixed allowed = inbound ? delivery.distance + delivery.preOpen : delivery.distance;
		return allowed * allowed > current;
	}

	// isOffMap: outside the whole field, border included.
	static bool OffMap(const GroundHeight &ground, Engine::Math::FixedVector2 at) noexcept
	{
		const auto [low, high] = ground.ExtentIncludingBorder();
		return at.x < low.x || at.y < low.y || at.x > high.x || at.y > high.y;
	}

	// The dive's strafe point (DeliverPayloadAIUpdate::update): the way it flies laid flat, as long as the share of the
	// dive flown so far (from DiveStartDistance at 0 to DiveEndDistance at 1, by its 3D distance) times 100, from the
	// target less a third of that (0.33), on the ground there.
	static Engine::Math::FixedVector3 StrafePoint(const Delivery &delivery, Engine::Math::FixedVector3 velocity, Engine::Math::Fixed distance,
		const GroundHeight &ground) noexcept
	{
		using Engine::Math::Fixed;
		const Fixed ratio = delivery.diveStart != delivery.diveEnd ? (delivery.diveStart - distance) / (delivery.diveStart - delivery.diveEnd) : Fixed{};
		Engine::Math::FixedVector2 way{velocity.x, velocity.y};
		// Coord3D::normalize leaves a zero vector as it is.
		if (way.x != Fixed{} || way.y != Fixed{})
			way = Engine::Math::Normalize(way);
		way = way * (ratio * Fixed::FromInt(100));
		const Engine::Math::FixedVector2 backwards = way * Fixed::FromRatio(33, 100);
		const Engine::Math::FixedVector2 at = delivery.target - backwards + way;
		return {at.x, at.y, ground.At(at)};
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		using Engine::Math::FixedVector2;
		using Engine::Math::FixedVector3;
		const CargoManifest &manifest = context.Read<CargoManifest>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const LaunchLayouts &layouts = context.Read<LaunchLayouts>();
		const std::uint64_t seed = context.Read<RandomSeed>().value;
		auto &done = context.Write<DeliveriesDone>().Slot(context);
		auto &drops = context.Write<DropExits>().Slot(context);
		auto &shots = context.Write<DirectShots>().Slot(context);
		auto &visible = context.Write<VisibleDrops>().Slot(context);
		auto &cues = context.Write<DeliveryCues>().Slot(context);
		auto deliveries = chunk.Get<Delivery>();
		auto transforms = chunk.Get<Transform>();
		auto orders = chunk.Get<MoveOrder>();
		auto motions = chunk.Get<Locomotion>();
		auto appearances = chunk.Get<Appearance>();
		auto armaments = chunk.Get<Armament>();
		auto slotSets = chunk.Get<WeaponSlots>();
		auto trackers = chunk.Get<FiringTracker>();
		const auto bonusRows = chunk.Get<WeaponBonusConditions>();
		const auto definitions = chunk.Get<DefinitionRef>();
		const auto experiences = chunk.Get<Experience>();
		const auto owners = chunk.Get<Owner>();
		const auto disabledRows = chunk.Get<Disabled>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		for (std::size_t row = 0; row < deliveries.size(); ++row)
		{
			Delivery &delivery = deliveries[row];
			Transform &transform = transforms[row];
			MoveOrder &order = orders[row];
			Locomotion *motion = motions.empty() ? nullptr : &motions[row];
			Appearance *appearance = appearances.empty() ? nullptr : &appearances[row];
			// Its velocity: how far it went since the last tick's run began.
			const FixedVector3 velocity = transform.position - delivery.lastPosition;
			delivery.lastPosition = transform.position;
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::Held))
				continue;
			// Object::fireCurrentWeapon at a spot, its shot among the tick's.
			const auto fire = [&](FixedVector3 aim) {
				if (armaments.empty())
					return false;
				SpotFirer firer;
				firer.entity = entities[row];
				firer.player = owners.empty() ? 0u : owners[row].player;
				firer.transform = &transform;
				firer.layout = definitions.empty() ? nullptr : layouts.Of(definitions[row].index);
				firer.armament = &armaments[row];
				firer.slots = slotSets.empty() ? nullptr : &slotSets[row];
				firer.tracker = trackers.empty() ? nullptr : &trackers[row];
				firer.conditions = bonusRows.empty() ? 0u : bonusRows[row].Effective();
				firer.veterancy = experiences.empty() ? std::uint8_t{0} : experiences[row].level;
				if (const auto shot = FireCurrentWeaponAt(firer, aim, weapons, ground, tick, seed ^ 0xD1F7u))
				{
					shots.push_back(*shot);
					return true;
				}
				return false;
			};
			// A move toward a spot at a height (aiMoveToPosition): the height its PRECISE_Z_POS holds it at.
			const auto moveTo = [&](FixedVector2 point, Fixed height) {
				order = MoveToPoint(point);
				delivery.goalHeight = height;
				if (motion != nullptr && motion->preciseZ != 0)
					motion->preciseHeight = height;
			};
			const auto doors = [&](bool open) {
				if (appearance == nullptr)
					return;
				appearance->Set(open ? delivery.doorClosing : delivery.doorOpening, false);
				appearance->Set(open ? delivery.doorOpening : delivery.doorClosing, true);
			};
			enum class Result : std::uint8_t
			{
				Continue,
				Success,
				Failure,
			};
			// The states' onEnter.
			const auto onEnter = [&](DeliveryPhase phase) -> Result {
				const FixedVector2 at = transform.position.XY();
				const FixedVector2 facing = Engine::Math::Direction(transform.facing);
				switch (phase)
				{
				case DeliveryPhase::Approach:
					moveTo(delivery.moveTo, delivery.targetHeight);
					return Result::Continue;
				case DeliveryPhase::Delivering:
					// Open the pod bay doors.
					doors(true);
					delivery.dropDelayLeft = delivery.doorDelay;
					return Result::Continue;
				case DeliveryPhase::ConsiderNewApproach:
				{
					if (++delivery.attempts > delivery.maxAttempts)
						return Result::Failure;
					// On past, twice its turning circle and a bit (DIST_FUDGE 2.2), to come round again (at height 0).
					const Fixed reach = MinTurnRadius(motion).first * Fixed::FromRatio(22, 10);
					moveTo(at + facing * reach, Fixed{});
					return Result::Continue;
				}
				case DeliveryPhase::RecoverFromOffMap:
				{
					// Hold there while it would have turned (its turning circle's travel time), its motion stopped.
					moveTo(at, transform.position.z);
					delivery.reEntryTick = tick + static_cast<std::uint64_t>(std::max<std::int64_t>(MinTurnRadius(motion).second.Ceil(), 0));
					if (motion != nullptr)
						motion->speed = {};
					return Result::Continue;
				}
				case DeliveryPhase::HeadOffMap:
				{
					if (delivery.selfDestruct != 0)
					{
						done.push_back({entities[row]});
						return Result::Continue;
					}
					// Straight on along its heading, well past the map (1.2 its diagonal), at its height.
					const auto [low, high] = ground.Extent();
					const Fixed diagonal = Engine::Math::Length(high - low) * Fixed::FromRatio(12, 10);
					delivery.exit = at + facing * diagonal;
					delivery.deliveredFacing = facing;
					moveTo(delivery.exit, transform.position.z);
					return Result::Continue;
				}
				case DeliveryPhase::CleanUp:
					done.push_back({entities[row]});
					return Result::Continue;
				}
				return Result::Continue;
			};
			// Into a state (the old one's onExit, the new one's onEnter), on through what an onEnter decides at once.
			const auto next = [&](DeliveryPhase from, Result result) {
				static constexpr std::array<std::array<DeliveryPhase, 2>, 6> transitions{{
					{DeliveryPhase::Delivering, DeliveryPhase::ConsiderNewApproach}, // Approach
					{DeliveryPhase::HeadOffMap, DeliveryPhase::ConsiderNewApproach}, // Delivering
					{DeliveryPhase::Approach, DeliveryPhase::HeadOffMap},             // ConsiderNewApproach
					{DeliveryPhase::Approach, DeliveryPhase::Approach},               // RecoverFromOffMap
					{DeliveryPhase::CleanUp, DeliveryPhase::CleanUp},                 // HeadOffMap
					{DeliveryPhase::CleanUp, DeliveryPhase::CleanUp},                 // CleanUp
				}};
				DeliveryPhase phase = transitions[static_cast<std::size_t>(from)][result == Result::Success ? 0 : 1];
				for (int guard = 0; guard < 8; ++guard)
				{
					if (delivery.phase == DeliveryPhase::Delivering)
						doors(false); // close the doors
					delivery.phase = phase;
					const Result entered = onEnter(phase);
					if (entered == Result::Continue || phase == DeliveryPhase::CleanUp)
						return;
					phase = transitions[static_cast<std::size_t>(phase)][entered == Result::Success ? 0 : 1];
				}
			};
			const auto set = [&](DeliveryPhase phase) {
				if (delivery.phase == DeliveryPhase::Delivering)
					doors(false);
				delivery.phase = phase;
				if (const Result entered = onEnter(phase); entered != Result::Continue)
					next(phase, entered);
			};
			if (delivery.entered == 0)
			{
				delivery.entered = 1;
				delivery.phase = DeliveryPhase::Approach;
				if (const Result entered = onEnter(DeliveryPhase::Approach); entered != Result::Continue)
					next(DeliveryPhase::Approach, entered);
			}
			const FixedVector2 at = transform.position.XY();
			// The current state's conditions.
			if (delivery.phase == DeliveryPhase::ConsiderNewApproach && OffMap(ground, at))
				set(DeliveryPhase::RecoverFromOffMap);
			// Its update.
			Result result = Result::Continue;
			switch (delivery.phase)
			{
			case DeliveryPhase::Approach:
				if (CloseEnough(delivery, at))
					result = Result::Success;
				else if (order.mode == MoveMode::Idle)
					result = Result::Failure;
				break;
			case DeliveryPhase::Delivering:
			{
				// Kick a dude out every so often.
				if (delivery.dropDelayLeft > 0)
				{
					--delivery.dropDelayLeft;
					break;
				}
				delivery.dropDelayLeft = delivery.dropDelay;
				if (!CloseEnough(delivery, at))
				{
					result = Result::Failure;
					break;
				}
				const std::span<const ecs::Entity> aboard = manifest.Aboard(entities[row]);
				// Out of payload to drop and its visible payload all gone (a carrier may have either or both).
				if (aboard.empty() && delivery.visibleDelivered == delivery.visibleBones)
				{
					result = Result::Success;
					break;
				}
				if (!aboard.empty())
				{
					if (delivery.fireWeapon != 0)
					{
						// Its current weapon fired at the target (plus the drop's offset), the rider destroyed.
						fire({delivery.target.x + delivery.dropOffset.x, delivery.target.y + delivery.dropOffset.y, delivery.targetHeight + delivery.dropOffset.z});
						done.push_back({aboard.front()});
					}
					else
					{
						// Where it is, moved by a random share of the variance (each axis that has one) and the offset.
						auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation, 0xD20Bu});
						const auto vary = [&](Fixed variance) {
							return variance > Fixed{} ? Engine::Math::UniformFixed(random, Fixed{} - variance, variance) : Fixed{};
						};
						FixedVector3 offset{vary(delivery.dropVariance.x), vary(delivery.dropVariance.y), vary(delivery.dropVariance.z)};
						offset.x += delivery.dropOffset.x;
						offset.y += delivery.dropOffset.y;
						offset.z += delivery.dropOffset.z;
						drops.push_back({entities[row], offset, delivery.moveTo, delivery.parachuteDirectly == 0, delivery.inheritVelocity != 0, velocity});
					}
				}
				// Its visible payload: so many items a drop, each made as it goes (its bone's item, from 1).
				if (delivery.visibleDelivered < delivery.visibleBones)
				{
					std::int32_t attempts = delivery.visiblePerDrop;
					while (attempts != 0 && delivery.visibleDelivered < delivery.visibleBones)
					{
						if (delivery.visibleRun != Delivery::NoRun)
							visible.push_back({entities[row], transform.position, velocity, {delivery.target.x, delivery.target.y, delivery.targetHeight}, delivery.moveTo,
								transform.facing, delivery.visibleRun, delivery.visibleDelivered + 1});
						--attempts;
						++delivery.visibleDelivered;
					}
				}
				break;
			}
			case DeliveryPhase::ConsiderNewApproach:
				if (order.mode == MoveMode::Idle)
					result = Result::Success;
				break;
			case DeliveryPhase::RecoverFromOffMap:
				if (tick < delivery.reEntryTick)
					break;
				{
					// Back in at the nearest edge (at its height, off the ground), facing where it was going, from a stop.
					const Engine::Math::FixedVector3 edge = ground.ClosestEdgePoint(at);
					transform.position = {edge.x, edge.y, transform.position.z > ground.At(at) ? transform.position.z : edge.z};
					transform.facing = Engine::Math::Heading(delivery.moveTo - edge.XY());
					if (motion != nullptr)
						motion->speed = {};
					result = Result::Success;
				}
				break;
			case DeliveryPhase::HeadOffMap:
				if (OffMap(ground, at))
					result = Result::Success;
				break;
			case DeliveryPhase::CleanUp:
				break;
			}
			if (result != Result::Continue)
				next(delivery.phase, result);
			// The dive, whatever its state: from DiveStartDistance (2D) to DiveEndDistance (3D) of the target, flying at the
			// height of its move's goal (PRECISE_Z_POS), strafing all the way while not climbing fast (z velocity under 5).
			if (delivery.dive == DiveState::PreDive)
			{
				if (Engine::Math::DistanceSquared(transform.position.XY(), delivery.target) <= delivery.diveStart * delivery.diveStart)
				{
					delivery.dive = DiveState::Diving;
					if (motion != nullptr)
					{
						motion->preciseZ = 1;
						motion->preciseHeight = delivery.goalHeight;
					}
					cues.push_back({entities[row], transform.position, definitions.empty() ? 0u : definitions[row].index, Delivery::NoEffect, DeliveryCue::Kind::StartDive});
				}
			}
			else if (delivery.dive == DiveState::Diving)
			{
				const FixedVector3 target{delivery.target.x, delivery.target.y, delivery.targetHeight};
				const FixedVector3 offset = transform.position - target;
				const Fixed distanceSquared = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
				if (distanceSquared <= delivery.diveEnd * delivery.diveEnd)
				{
					delivery.dive = DiveState::PostDive;
					if (motion != nullptr)
						motion->preciseZ = 0;
				}
				if (delivery.strafeSlot != Delivery::NoSlot && velocity.z < Fixed::FromInt(5))
				{
					const FixedVector3 point = StrafePoint(delivery, velocity, Engine::Math::Length(offset), ground);
					// Locked to its strafing weapon just till the weapon is empty (setWeaponLock LOCKED_TEMPORARILY).
					if (!slotSets.empty() && !armaments.empty())
						LockSlotTemporarily(slotSets[row], armaments[row], delivery.strafeSlot);
					fire(point);
					cues.push_back({entities[row], point, definitions.empty() ? 0u : definitions[row].index, delivery.strafeEffect, DeliveryCue::Kind::Strafe});
				}
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::DeliverySystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.delivery";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<engine::gameplay::UnloadingSystem, engine::gameplay::MovementSystem>;
	using After = SystemTypeList<engine::gameplay::BoardingSystem>;
};
}
