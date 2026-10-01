export module engine.gameplay.rts.containment.systems.unloading_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.rts.movement.systems.movement_system;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.rts.navigation.resources.navigation_grid;
import engine.gameplay.common.spatial.algorithms.find_position;

// Unloads transports, chunk-parallel: an airborne transport first comes
// down to the surface (unless it may unload in the air), then lets one
// passenger out every exit delay until it is empty, then climbs back to its
// cruise height. An idle airborne transport awaited by boarders comes down
// for them the same way (ChinookAIUpdate lands while objects want to enter), as does one told to land.
// Landing, it first flies (coming down as it goes) to the nearest spot of
// clear ground its footprint fits on clear of every other object (within a
// hundred times its radius; none: where it is), then goes straight down;
// taking off (back to within LandedHeight of its cruise height) it goes
// straight up; either way its orders wait. The session takes the
// passengers out between ticks.
export namespace engine::gameplay
{
// Within this height of the surface a transport counts as landed.
inline Engine::Math::Fixed LandedHeight() noexcept { return Engine::Math::Fixed::FromInt(3); }

struct UnloadingSystem
{
	using Query = ecs::Query<ecs::Write<Transport>, ecs::Read<Transform>, ecs::OptionalWrite<Locomotion>, ecs::OptionalWrite<MoveOrder>, ecs::Exclude<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<CargoManifest>, ecs::Read<GroundHeight>, ecs::Read<BoardRequests>, ecs::Read<SpatialIndex>, ecs::Read<NavigationGrid>,
		ecs::Write<ExitRequests>, ecs::Write<IntentExits>>;
	using Lookup = ecs::Lookup<ecs::Read<ExitIntent>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context)
	{
		context.Write<ExitRequests>().Reset(query.PreparedChunkCount());
		context.Write<IntentExits>().Reset(query.PreparedChunkCount());
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const CargoManifest &manifest = context.Read<CargoManifest>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		ExitRequests &exits = context.Write<ExitRequests>();
		auto transports = chunk.Get<Transport>();
		const auto transforms = chunk.Get<Transform>();
		auto motions = chunk.Get<Locomotion>();
		auto orders = chunk.Get<MoveOrder>();
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const NavigationGrid &grid = context.Read<NavigationGrid>();
		const auto entities = chunk.Entities();
		const BoardRequests &boards = context.Read<BoardRequests>();
		auto &out = exits.Slot(context);
		auto &asked = context.Write<IntentExits>().Slot(context);
		const auto lookup = context.Lookup<Lookup>();
		for (std::size_t row = 0; row < transports.size(); ++row)
		{
			Transport &transport = transports[row];
			Locomotion *motion = motions.empty() ? nullptr : &motions[row];
			const auto climb = [&] {
				if (transport.landing && motion != nullptr)
				{
					motion->locomotor.preferredHeight = transport.cruiseHeight;
					transport.takingOff = true;
				}
				transport.landing = false;
			};
			const auto &at = transforms[row].position;
			// Taken off once back at its cruise height.
			if (transport.takingOff && motion != nullptr)
			{
				const Engine::Math::Fixed above = at.z - ground.Surface(at.XY());
				if (above >= motion->locomotor.preferredHeight - LandedHeight())
					transport.takingOff = false;
			}
			// On to its landing spot (straight there), down once over it.
			if (transport.seekingSpot && (!transport.landing || orders.empty() ||
					Engine::Math::DistanceSquared(at.XY(), transport.landingSpot) <= LandedHeight() * LandedHeight()))
			{
				transport.seekingSpot = false;
				if (!orders.empty() && orders[row].mode == MoveMode::Direct)
					orders[row] = MoveOrder{};
			}
			const auto vertical = [&] {
				if (motion != nullptr)
					motion->vertical = (transport.landing && !transport.seekingSpot) || transport.takingOff;
			};
			const bool airborne = motion != nullptr && IsAirborne(motion->locomotor) && at.z - ground.Surface(at.XY()) > LandedHeight();
			// TransportContain::isExitBusy: DelayExitInAir holds every exit while it is above the terrain at all (a Battle
			// Bus in the air off a bump).
			const bool heldInAir = transport.definition.delayExitInAir && at.z > ground.At(at.XY());
			const auto land = [&] {
				if (!transport.landing)
				{
					transport.cruiseHeight = motion->locomotor.preferredHeight;
					transport.landing = true;
					// findPositionAround (maxRadius: its bounding circle a hundred times over): clear ground (the
					// pathfinding cell) with no other object's footprint reaching its own.
					const SpatialEntry *self = spatial.Find(entities[row]);
					const Engine::Math::Fixed radius = self != nullptr ? self->radius : Engine::Math::Fixed{};
					const auto clear = [&](Engine::Math::FixedVector2 point) {
						const auto cellX = static_cast<std::int32_t>((point.x / Engine::Math::Fixed::FromInt(PathfindCellSize)).Floor());
						const auto cellY = static_cast<std::int32_t>((point.y / Engine::Math::Fixed::FromInt(PathfindCellSize)).Floor());
						if (grid.Width() > 0 && (!grid.Contains(cellX, cellY) || grid.Type(cellX, cellY) != PathfindCellType::Clear))
							return false;
						bool free = true;
						spatial.ForEachWithin(point, radius, [&](const SpatialEntry &entry) {
							if (entry.entity == entities[row] || !free)
								return;
							const Engine::Math::Fixed reach = radius + entry.radius;
							if (Engine::Math::DistanceSquared(point, entry.position.XY()) < reach * reach)
								free = false;
						});
						return free;
					};
					const auto spot = FindPositionAround(at.XY(), Engine::Math::Fixed{}, radius * Engine::Math::Fixed::FromInt(100), Engine::Math::TurnAngle{}, clear);
					transport.landingSpot = spot.value_or(at.XY());
					transport.seekingSpot = !orders.empty() &&
						Engine::Math::DistanceSquared(at.XY(), transport.landingSpot) > LandedHeight() * LandedHeight();
					if (transport.seekingSpot)
						orders[row] = MoveStraightTo(transport.landingSpot);
				}
				motion->locomotor.preferredHeight = {};
			};
			if (transport.state != TransportState::Unloading)
			{
				// AIExitState: riders that asked to get out, in the order they got in, one each time its exit is free
				// (isExitBusy); one that must land first comes down for them (getAiFreeToExit: WAIT_TO_EXIT). Those taken
				// out at once (removeAllContained) all go now.
				ecs::Entity leaving;
				for (const ecs::Entity rider : manifest.Aboard(entities[row]))
				{
					const ExitIntent *intent = lookup.template Get<ExitIntent>(rider);
					if (intent == nullptr || intent->transport != entities[row])
						continue;
					if (intent->instant != 0)
						asked.push_back({entities[row], rider});
					else if (leaving == ecs::Entity{})
						leaving = rider;
				}
				if (leaving != ecs::Entity{})
				{
					if (airborne && !transport.definition.unloadInAir)
						land();
					else if (context.Tick() >= transport.nextExitTick && !heldInAir)
					{
						asked.push_back({entities[row], leaving});
						transport.nextExitTick = context.Tick() + std::max<std::uint64_t>(transport.definition.exitDelay, 1);
					}
					vertical();
					continue;
				}
				bool awaited = false;
				if (motion != nullptr && IsAirborne(motion->locomotor) && (orders.empty() || orders[row].mode == MoveMode::Idle || transport.seekingSpot))
					boards.ForEach([&](const BoardRequest &request) { awaited = awaited || (!request.arrived && !request.touchOnly && request.transport == entities[row]); });
				if (awaited || (transport.landRequested && motion != nullptr))
					land();
				else
					climb();
				vertical();
				continue;
			}
			if (manifest.Count(entities[row]) == 0)
			{
				transport.state = TransportState::Idle;
				climb();
				vertical();
				continue;
			}
			if (airborne && !transport.definition.unloadInAir)
			{
				land();
				vertical();
				continue;
			}
			vertical();
			if (context.Tick() < transport.nextExitTick || heldInAir)
				continue;
			out.push_back({entities[row]});
			transport.nextExitTick = context.Tick() + std::max<std::uint64_t>(transport.definition.exitDelay, 1);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::UnloadingSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.unloading";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<engine::gameplay::MovementSystem>;
	using After = SystemTypeList<>;
};
}
