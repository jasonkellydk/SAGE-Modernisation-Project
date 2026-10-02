export module games.generalszh.gameplay.containment.algorithms.railed_transports;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.containment.components.railed_transport;
import games.generalszh.gameplay.containment.algorithms.garrisons;
import games.generalszh.gameplay.objects.resources.object_templates;
import games.generalszh.content.objects.object_status;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.status.components.status_flags;
import engine.gameplay.rts.containment.components.cargo_size;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.docking.components.dock;
import engine.gameplay.rts.docking.components.docking;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.movement.systems.movement_system;
import engine.gameplay.rts.navigation.components.navigation;

// The railed transports' side effects and orders (RailedTransportAIUpdate, RailedTransportDockUpdate,
// RailedTransportContain):
//   ApplyRailedTransportEvents, after the systems: a captured docker is held, unselectable, turned to face the transport
//     and pulled in at its distance over PullInsideDuration a tick (action); one pulled in stops docking, goes idle and
//     goes inside (still held and unselectable); a transport setting off follows its path exactly (aiFollowWaypointPath,
//     ultra accurate); a rider pushed out goes idle, free and selectable again and walks on, then the next comes out.
//   RailedUnloadNext (unloadNext): with riders still to let out, its first rider out at its centre, facing as it does,
//     held, pushed to the dock's exit over PushOutsideDuration; the container emptied, its dock opens
//     (RailedTransportContain::onRemoving).
//   ExecuteRailedTransport (privateExecuteRailedTransport): not while loading or unloading, on along its next path from
//     its start, under way.
//   RailedEvacuate (privateEvacuate): not under way nor loading or unloading, all its riders out in turn.
//   RailedExitOne (RailedTransportContain::exitObjectViaDoor): one rider out (the first, whoever was named).
export namespace generalszh::gameplay
{
namespace railed_apply_detail
{
namespace gp = engine::gameplay;

inline std::uint64_t UnselectableBit() { return std::uint64_t{1} << content::ObjectStatusBit("UNSELECTABLE"); }

inline void SetHeld(ecs::World &world, ecs::Entity entity, bool held)
{
	if (!world.Has<gp::Disabled>(entity))
		world.Add<gp::Disabled>(entity);
	auto &mask = world.Get<gp::Disabled>(entity)->mask;
	mask = held ? mask | gp::disabled_type::Held : mask & ~gp::disabled_type::Held;
}

inline void SetUnselectable(ecs::World &world, ecs::Entity entity, bool on)
{
	if (!world.Has<gp::StatusFlags>(entity))
		world.Add<gp::StatusFlags>(entity);
	auto &bits = world.Get<gp::StatusFlags>(entity)->bits;
	bits = on ? bits | UnselectableBit() : bits & ~UnselectableBit();
}

inline void Order(ecs::World &world, ecs::Entity entity, const gp::MoveOrder &order)
{
	if (auto *move = world.Get<gp::MoveOrder>(entity))
		*move = order;
	if (auto *route = world.Get<gp::Route>(entity))
		route->planned = false;
}

inline Engine::Math::FixedVector3 Place(const gp::Transform &frame, const Engine::Math::FixedVector3 &local)
{
	using Engine::Math::Fixed;
	const Fixed c = Engine::Math::Cos(frame.facing), s = Engine::Math::Sin(frame.facing);
	return {frame.position.x + local.x * c - local.y * s, frame.position.y + local.x * s + local.y * c, frame.position.z + local.z};
}

inline bool Busy(const RailedTransport &ferry) noexcept { return ferry.docking.IsValid() || ferry.unloading.IsValid(); }
}

inline void RailedUnloadNext(GameWorld &game, ecs::Entity transport)
{
	namespace gp = engine::gameplay;
	using namespace railed_apply_detail;
	auto &world = game.world;
	RailedTransport *ferry = world.IsAlive(transport) ? world.Get<RailedTransport>(transport) : nullptr;
	if (ferry == nullptr)
		return;
	ferry->unloading = {};
	if (ferry->unloadCount == 0)
		return;
	const auto aboard = world.Resource<gp::CargoManifest>().Aboard(transport);
	if (aboard.empty())
		return;
	const ecs::Entity rider = aboard.front();
	const gp::Transform frame = *world.Get<gp::Transform>(transport);
	TakeOutNow(game, transport, rider);
	if (auto *at = world.Get<gp::Transform>(rider))
		*at = gp::Transform{frame.position, frame.facing};
	SetHeld(world, rider, true);
	// DockUpdate::getExitPosition: DockEnd placed with it (no DockStart: where the rider is).
	const RailedTransportConfig *config = game.templates.RailedTransportOf(world.Get<gp::DefinitionRef>(transport)->index);
	const Engine::Math::FixedVector3 exit = config != nullptr && config->enter ? Place(frame, config->exit) : frame.position;
	const Engine::Math::Fixed distance = Engine::Math::Distance(exit, frame.position);
	const std::uint64_t ticks = config != nullptr ? config->content.pushOutsideTicks : 0;
	ferry->pushPerTick = ticks > 0 ? distance / Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(ticks)) : distance;
	ferry->unloading = rider;
	if (!world.Has<RailedHaul>(rider))
		world.Add<RailedHaul>(rider);
	world.Get<RailedHaul>(rider)->transport = transport;
	if (ferry->unloadCount != RailedTransport::UnloadAll)
		--ferry->unloadCount;
	// RailedTransportContain::onRemoving: emptied, it may be docked with again.
	if (const auto *carrier = world.Get<gp::Transport>(transport); carrier != nullptr && carrier->occupied == 0)
		if (auto *dock = world.Get<gp::Dock>(transport))
			dock->open = true;
}

// RailedTransportDockUpdate::unloadAll: not over one already coming out.
inline void RailedUnloadAll(GameWorld &game, ecs::Entity transport)
{
	RailedTransport *ferry = game.world.IsAlive(transport) ? game.world.Get<RailedTransport>(transport) : nullptr;
	if (ferry == nullptr || ferry->unloading.IsValid())
		return;
	ferry->unloadCount = RailedTransport::UnloadAll;
	RailedUnloadNext(game, transport);
}

inline void RailedExitOne(GameWorld &game, ecs::Entity transport)
{
	RailedTransport *ferry = game.world.IsAlive(transport) ? game.world.Get<RailedTransport>(transport) : nullptr;
	if (ferry == nullptr)
		return;
	ferry->unloadCount = 1;
	RailedUnloadNext(game, transport);
}

inline void RailedEvacuate(GameWorld &game, ecs::Entity transport)
{
	const RailedTransport *ferry = game.world.IsAlive(transport) ? game.world.Get<RailedTransport>(transport) : nullptr;
	if (ferry == nullptr || ferry->inTransit != 0 || railed_apply_detail::Busy(*ferry))
		return;
	RailedUnloadAll(game, transport);
}

inline void ExecuteRailedTransport(GameWorld &game, ecs::Entity transport)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	RailedTransport *ferry = world.IsAlive(transport) ? world.Get<RailedTransport>(transport) : nullptr;
	if (ferry == nullptr || railed_apply_detail::Busy(*ferry) || ferry->pathCount == 0)
		return;
	if (++ferry->currentPath >= static_cast<std::int32_t>(ferry->pathCount))
		ferry->currentPath = 0;
	railed_apply_detail::Order(world, transport, gp::FollowPath(game.waypoints, ferry->paths[static_cast<std::size_t>(ferry->currentPath)].start, true));
	ferry->inTransit = 1;
	if (auto *dock = world.Get<gp::Dock>(transport))
		dock->open = false;
}

inline void ApplyRailedTransportEvents(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using namespace railed_apply_detail;
	auto &world = game.world;
	std::vector<RailedEvent> events;
	if (const auto *captures = world.FindResource<RailedCaptures>())
		captures->AppendTo(events);
	if (const auto *transports = world.FindResource<RailedTransportEvents>())
		transports->AppendTo(events);
	for (const RailedEvent &event : events)
	{
		RailedTransport *ferry = world.IsAlive(event.transport) ? world.Get<RailedTransport>(event.transport) : nullptr;
		if (ferry == nullptr)
			continue;
		const gp::Transform frame = *world.Get<gp::Transform>(event.transport);
		switch (event.kind)
		{
		case RailedEvent::Kind::Follow:
			Order(world, event.transport, gp::FollowPath(game.waypoints, event.waypoint, true));
			break;
		case RailedEvent::Kind::Capture:
		{
			gp::Transform *docker = world.IsAlive(event.rider) ? world.Get<gp::Transform>(event.rider) : nullptr;
			if (docker == nullptr || ferry->docking == event.rider)
				break;
			const RailedTransportConfig *config = game.templates.RailedTransportOf(world.Get<gp::DefinitionRef>(event.transport)->index);
			const Engine::Math::Fixed distance = Engine::Math::Distance(frame.position, docker->position);
			const std::uint64_t ticks = config != nullptr ? config->content.pullInsideTicks : 0;
			ferry->docking = event.rider;
			ferry->pullPerTick = ticks > 0 ? distance / Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(ticks)) : distance;
			const Engine::Math::FixedVector2 toward = frame.position.XY() - docker->position.XY();
			docker->facing = Engine::Math::Heading(toward);
			// (Adding components moves the entity: no component pointer is held across these.)
			SetUnselectable(world, event.rider, true);
			SetHeld(world, event.rider, true);
			if (!world.Has<RailedHaul>(event.rider))
				world.Add<RailedHaul>(event.rider);
			world.Get<RailedHaul>(event.rider)->transport = event.transport;
			break;
		}
		case RailedEvent::Kind::Loaded:
		{
			if (!world.IsAlive(event.rider))
				break;
			world.Remove<RailedHaul>(event.rider);
			if (auto *docking = world.Get<gp::Docking>(event.rider))
				*docking = {};
			Order(world, event.rider, gp::MoveOrder{});
			const auto *size = world.Get<gp::CargoSize>(event.rider);
			PutInside(game, event.transport, event.rider, size != nullptr ? size->slots : 1u);
			break;
		}
		case RailedEvent::Kind::Unloaded:
			if (world.IsAlive(event.rider))
			{
				world.Remove<RailedHaul>(event.rider);
				SetHeld(world, event.rider, false);
				SetUnselectable(world, event.rider, false);
				Order(world, event.rider, gp::MoveToPoint(event.at));
			}
			RailedUnloadNext(game, event.transport);
			break;
		case RailedEvent::Kind::UnloadNext:
			RailedUnloadNext(game, event.transport);
			break;
		}
	}
}
}
