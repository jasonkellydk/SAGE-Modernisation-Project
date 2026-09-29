export module engine.gameplay.rts.delivery.systems.delivery_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.delivery.components.delivery;
export import engine.gameplay.rts.containment.systems.unloading_system;
export import engine.gameplay.rts.containment.systems.boarding_system;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.random.resources.random_seed;
import Engine.Core.Math.FixedRandom;

// Flies payload carriers through their runs (DeliverPayloadAIUpdate::update -> DeliverPayloadStateMachine), each tick
// before it moves, chunk-parallel. As the original's state machine: the current state's conditions first (considering a
// new approach off the map: recover), then its update; a success or failure enters the next state at once (its
// onEnter), whose update waits for the next tick. The riders it drops are taken out after the step (DropExits).
export namespace engine::gameplay
{
struct DeliverySystem
{
	using Query = ecs::Query<ecs::Write<Delivery>, ecs::Write<Transform>, ecs::Write<MoveOrder>, ecs::OptionalWrite<Locomotion>,
		ecs::OptionalWrite<Appearance>, ecs::Exclude<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<CargoManifest>, ecs::Read<GroundHeight>, ecs::Read<RandomSeed>, ecs::Write<DeliveriesDone>,
		ecs::Write<DropExits>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context)
	{
		context.Write<DeliveriesDone>().Reset(query.PreparedChunkCount());
		context.Write<DropExits>().Reset(query.PreparedChunkCount());
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

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		using Engine::Math::FixedVector2;
		const CargoManifest &manifest = context.Read<CargoManifest>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const std::uint64_t seed = context.Read<RandomSeed>().value;
		auto &done = context.Write<DeliveriesDone>().Slot(context);
		auto &drops = context.Write<DropExits>().Slot(context);
		auto deliveries = chunk.Get<Delivery>();
		auto transforms = chunk.Get<Transform>();
		auto orders = chunk.Get<MoveOrder>();
		auto motions = chunk.Get<Locomotion>();
		auto appearances = chunk.Get<Appearance>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		for (std::size_t row = 0; row < deliveries.size(); ++row)
		{
			Delivery &delivery = deliveries[row];
			Transform &transform = transforms[row];
			MoveOrder &order = orders[row];
			Locomotion *motion = motions.empty() ? nullptr : &motions[row];
			Appearance *appearance = appearances.empty() ? nullptr : &appearances[row];
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
					order = MoveToPoint(delivery.moveTo);
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
					// On past, twice its turning circle and a bit (DIST_FUDGE 2.2), to come round again.
					const Fixed reach = MinTurnRadius(motion).first * Fixed::FromRatio(22, 10);
					order = MoveToPoint(at + facing * reach);
					return Result::Continue;
				}
				case DeliveryPhase::RecoverFromOffMap:
				{
					// Hold there while it would have turned (its turning circle's travel time), its motion stopped.
					order = MoveToPoint(at);
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
					// Straight on along its heading, well past the map (1.2 its diagonal).
					const auto [low, high] = ground.Extent();
					const Fixed diagonal = Engine::Math::Length(high - low) * Fixed::FromRatio(12, 10);
					delivery.exit = at + facing * diagonal;
					delivery.deliveredFacing = facing;
					order = MoveToPoint(delivery.exit);
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
				if (manifest.Count(entities[row]) == 0)
				{
					result = Result::Success;
					break;
				}
				// Where it is, moved by a random share of the variance (each axis that has one) and the offset.
				auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation, 0xD20Bu});
				const auto vary = [&](Fixed variance) {
					return variance > Fixed{} ? Engine::Math::UniformFixed(random, Fixed{} - variance, variance) : Fixed{};
				};
				Engine::Math::FixedVector3 offset{vary(delivery.dropVariance.x), vary(delivery.dropVariance.y), vary(delivery.dropVariance.z)};
				offset.x += delivery.dropOffset.x;
				offset.y += delivery.dropOffset.y;
				offset.z += delivery.dropOffset.z;
				drops.push_back({entities[row], offset, delivery.moveTo, delivery.parachuteDirectly == 0});
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
