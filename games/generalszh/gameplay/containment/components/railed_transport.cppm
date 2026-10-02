export module games.generalszh.gameplay.containment.components.railed_transport;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedVector;
export import engine.core.serialization.byte_stream;

// A railed transport (the original's RailedTransportAIUpdate, RailedTransportDockUpdate and RailedTransportContain: the
// ferry): the waypoint paths it runs (<prefix>StartNN to <prefix>EndNN, those with both ends; m_path), the path it is
// on (none until its first update sends it to the end nearest it; m_currentPath) and whether it is under way (its dock
// shut; m_inTransit); the mover it is pulling in and how far a tick (m_dockingObjectID, m_pullInsideDistancePerFrame),
// the rider it is pushing out and how far a tick (m_unloadingObjectID, m_pushOutsideDistancePerFrame) and how many more
// riders to let out (m_unloadCount: -1 all).
//   RailedHaul marks a mover being pulled in or pushed out (its MOVING look).
// Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct RailedPath
{
	std::uint32_t start{0}; // waypoint indexes
	std::uint32_t end{0};
};

struct RailedTransport
{
	static constexpr std::size_t MaxPaths = 32; // MAX_WAYPOINT_PATHS
	static constexpr std::int32_t UnloadAll = -1;
	std::array<RailedPath, MaxPaths> paths{};
	std::uint32_t pathCount{0};
	std::int32_t currentPath{-1};
	std::uint32_t inTransit{0};
	std::int32_t unloadCount{UnloadAll};
	ecs::Entity docking;
	ecs::Entity unloading;
	Engine::Math::Fixed pullPerTick;
	Engine::Math::Fixed pushPerTick;
};

struct RailedHaul
{
	ecs::Entity transport;
};

// What the railed transports did this tick, applied after the systems (their riders and paths):
//   Follow: it sets off along the path from `waypoint`;
//   Capture: a docker within reach becomes the one pulled in (RailedTransportDockUpdate::action, from the docker's
//     side: RailedCaptures);
//   Loaded: the one pulled in reached its centre and goes inside (doPullInDocking);
//   Unloaded: the one pushed out reached the exit and walks on to `at` (doPushOutDocking), then the next comes out;
//   UnloadNext: the one being pushed out is gone, the next comes out.
struct RailedEvent
{
	enum class Kind : std::uint8_t
	{
		Follow,
		Capture,
		Loaded,
		Unloaded,
		UnloadNext,
	};
	Kind kind{Kind::Follow};
	ecs::Entity transport;
	ecs::Entity rider;
	std::uint32_t waypoint{0};
	Engine::Math::FixedVector2 at;
};

struct RailedTransportEvents : ecs::ChunkOutputs<RailedEvent>
{
};

struct RailedCaptures : ecs::ChunkOutputs<RailedEvent>
{
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::RailedTransport>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.railed_transport";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ComponentTraits<generalszh::gameplay::RailedHaul>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.railed_haul";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<generalszh::gameplay::RailedTransportEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.railed_transport_events";
};

template<>
struct ResourceTraits<generalszh::gameplay::RailedCaptures>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.railed_captures";
};
}
