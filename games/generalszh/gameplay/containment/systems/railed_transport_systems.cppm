export module games.generalszh.gameplay.containment.systems.railed_transport_systems;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import games.generalszh.gameplay.containment.components.railed_transport;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.docking.components.dock;
export import engine.gameplay.rts.docking.components.docking;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.navigation.resources.waypoint_graph;

// The railed transports (the ferry), chunk-parallel:
//   RailedDockerSystem, the docker's side (AIDockProcessDockState::update calling RailedTransportDockUpdate::action once
//     a frame from the frame after it began): a mover docking at a railed transport, at its business, not the one
//     being pulled in and within ToleranceDistance of its centre (in three dimensions) is captured (RailedCaptures).
//   RailedTransportSystem, the transport's (RailedTransportAIUpdate::update, then RailedTransportDockUpdate::update):
//     its dock's points follow it (its model's bones placed with it) and its room is what its container has free; with
//     no path yet it sets off for the end waypoint nearest it (in three dimensions: pickAndMoveToInitialLocation), under
//     way; under way, within 5 of its path's end (three dimensions) or idle, it arrives (its dock open); the mover
//     being pulled in moves its pull a tick toward its centre (the three-dimensional direction, laid flat; its height
//     kept) and inside 6 of it (two dimensions) goes in; the rider being pushed out moves its push a tick toward the
//     dock's exit on the ground and within 3 of it walks on to DockWaiting07. What reaches beyond the transport goes
//     out as RailedTransportEvents.
export namespace generalszh::gameplay
{
namespace railed_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;
using Engine::Math::FixedVector3;

inline FixedVector3 Place(const engine::gameplay::Transform &frame, const FixedVector3 &local)
{
	const Fixed c = Engine::Math::Cos(frame.facing), s = Engine::Math::Sin(frame.facing);
	return {frame.position.x + local.x * c - local.y * s, frame.position.y + local.x * s + local.y * c, frame.position.z + local.z};
}

// Coord3D::normalize then the x and y scaled by `step`: the step along the ground is the flat share of a unit step.
inline FixedVector2 FlatStep(const FixedVector3 &from, const FixedVector3 &to, Fixed step)
{
	const FixedVector3 unit = Engine::Math::Normalize(to - from);
	return {unit.x * step, unit.y * step};
}
}

struct RailedDockerSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Docking>, ecs::Read<engine::gameplay::Transform>>;
	using Lookup = ecs::Lookup<ecs::Read<RailedTransport>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::Dock>,
		ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Write<RailedCaptures>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<RailedCaptures>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const auto lookup = context.Lookup<Lookup>();
		auto &out = context.Write<RailedCaptures>().Slot(context);
		const auto dockings = chunk.Get<gp::Docking>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < dockings.size(); ++row)
		{
			const gp::Docking &docking = dockings[row];
			if (docking.phase != gp::DockPhase::Process || context.Tick() <= docking.nextAction || !lookup.IsAlive(docking.dock))
				continue;
			const RailedTransport *railed = lookup.Get<RailedTransport>(docking.dock);
			const gp::Dock *dock = lookup.Get<gp::Dock>(docking.dock);
			const gp::Transform *at = lookup.Get<gp::Transform>(docking.dock);
			const gp::DefinitionRef *ref = lookup.Get<gp::DefinitionRef>(docking.dock);
			if (railed == nullptr || dock == nullptr || at == nullptr || ref == nullptr || !dock->open || railed->docking == entities[row])
				continue;
			const RailedTransportConfig *config = templates.RailedTransportOf(ref->index);
			if (config == nullptr)
				continue;
			const Engine::Math::Fixed reach = config->content.tolerance;
			if (Engine::Math::DistanceSquared(at->position, transforms[row].position) <= reach * reach)
				out.push_back({RailedEvent::Kind::Capture, docking.dock, entities[row], 0, {}});
		}
	}
};

struct RailedTransportSystem
{
	using Query = ecs::Query<ecs::Write<RailedTransport>, ecs::Write<engine::gameplay::Dock>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::MoveOrder>, ecs::Optional<engine::gameplay::Transport>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::WaypointGraph>, ecs::Read<engine::gameplay::GroundHeight>,
		ecs::Write<RailedTransportEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<RailedTransportEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using namespace railed_detail;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const gp::WaypointGraph &graph = context.Read<gp::WaypointGraph>();
		const gp::GroundHeight &ground = context.Read<gp::GroundHeight>();
		const auto lookup = context.Lookup<Lookup>();
		auto &out = context.Write<RailedTransportEvents>().Slot(context);
		auto &commands = context.Commands();
		auto railed = chunk.Get<RailedTransport>();
		auto docks = chunk.Get<gp::Dock>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto refs = chunk.Get<gp::DefinitionRef>();
		const auto orders = chunk.Get<gp::MoveOrder>();
		const auto transports = chunk.Get<gp::Transport>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < railed.size(); ++row)
		{
			RailedTransport &ferry = railed[row];
			gp::Dock &dock = docks[row];
			const gp::Transform &frame = transforms[row];
			const RailedTransportConfig *config = templates.RailedTransportOf(refs[row].index);
			if (config == nullptr)
				continue;
			// Its dock's points where its bones are now; its room its container's.
			for (std::size_t index = 0; index < config->approach.size() && index < gp::Dock::MaxApproaches; ++index)
				dock.approach[index] = Place(frame, config->approach[index]).XY();
			if (config->enter)
			{
				dock.enter = Place(frame, *config->enter).XY();
				dock.action = Place(frame, config->action).XY();
				dock.exit = Place(frame, config->exit).XY();
			}
			dock.room = transports.empty() ? 0u
				: transports[row].definition.slots - std::min(transports[row].definition.slots, transports[row].occupied);
			// RailedTransportAIUpdate::update: its first path, then its arrival.
			bool issued = false;
			if (ferry.currentPath < 0 && ferry.pathCount > 0)
			{
				std::int32_t closest = -1;
				Fixed best;
				for (std::uint32_t index = 0; index < ferry.pathCount; ++index)
				{
					const Fixed distance = Engine::Math::DistanceSquared(graph.Position(ferry.paths[index].end), frame.position);
					if (closest < 0 || distance < best)
					{
						closest = static_cast<std::int32_t>(index);
						best = distance;
					}
				}
				out.push_back({RailedEvent::Kind::Follow, entities[row], {}, ferry.paths[static_cast<std::size_t>(closest)].end, {}});
				ferry.currentPath = closest;
				ferry.inTransit = 1;
				dock.open = false;
				issued = true;
			}
			if (ferry.inTransit != 0 && !issued && ferry.currentPath >= 0)
			{
				const FixedVector3 end = graph.Position(ferry.paths[static_cast<std::size_t>(ferry.currentPath)].end);
				if (Engine::Math::DistanceSquared(end, frame.position) <= Fixed::FromInt(25) || orders[row].mode == gp::MoveMode::Idle)
				{
					ferry.inTransit = 0;
					dock.open = true;
				}
			}
			// RailedTransportDockUpdate::doPullInDocking.
			if (ferry.docking.IsValid())
			{
				const gp::Transform *docker = lookup.IsAlive(ferry.docking) ? lookup.Get<gp::Transform>(ferry.docking) : nullptr;
				if (docker == nullptr)
					ferry.docking = {};
				else
				{
					const FixedVector2 step = FlatStep(docker->position, frame.position, ferry.pullPerTick);
					const FixedVector3 moved{docker->position.x + step.x, docker->position.y + step.y, docker->position.z};
					commands.Set<gp::Transform>(ferry.docking, gp::Transform{moved, docker->facing});
					if (Engine::Math::DistanceSquared(moved.XY(), frame.position.XY()) <= Fixed::FromInt(36))
					{
						out.push_back({RailedEvent::Kind::Loaded, entities[row], ferry.docking, 0, {}});
						ferry.docking = {};
					}
				}
			}
			// RailedTransportDockUpdate::doPushOutDocking.
			if (ferry.unloading.IsValid())
			{
				const gp::Transform *rider = lookup.IsAlive(ferry.unloading) ? lookup.Get<gp::Transform>(ferry.unloading) : nullptr;
				if (rider == nullptr)
				{
					out.push_back({RailedEvent::Kind::UnloadNext, entities[row], {}, 0, {}});
					ferry.unloading = {};
				}
				else
				{
					FixedVector3 exit = config->enter ? Place(frame, config->exit) : rider->position;
					exit.z = ground.At(exit.XY());
					const FixedVector2 step = FlatStep(rider->position, exit, ferry.pushPerTick);
					const FixedVector3 moved{rider->position.x + step.x, rider->position.y + step.y, exit.z};
					commands.Set<gp::Transform>(ferry.unloading, gp::Transform{moved, rider->facing});
					if (Engine::Math::DistanceSquared(exit, moved) <= Fixed::FromInt(9))
					{
						// DOCKWAITING07, placed with the transport.
						const FixedVector3 onward = config->approach.size() > 6 ? Place(frame, config->approach[6]) : exit;
						out.push_back({RailedEvent::Kind::Unloaded, entities[row], ferry.unloading, 0, onward.XY()});
						ferry.unloading = {};
					}
				}
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::RailedDockerSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.railed_docker";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<generalszh::gameplay::RailedTransportSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.railed_transport";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
