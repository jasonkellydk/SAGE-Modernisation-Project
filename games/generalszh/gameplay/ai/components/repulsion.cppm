export module games.generalszh.gameplay.ai.components.repulsion;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// Civilians running from enemies (AIData EnableRepulsors, KINDOF_CAN_BE_REPULSED), as data:
// - Repulsable: a unit that runs. Its vision range (getVisionRange: how near a repulsor scares it), its wander states'
//   look countdown (m_timer, every m_waitFrames = 10 + (id & 7) frames), its idle look (AIIdleState: at once, then every
//   IDLE_COUNTDOWN_DELAY, the first wait longer by its initial sleep offset), the move mode it was last seen in (a state
//   entered starts its countdown afresh), and whether it is running away (AIMoveAwayFromRepulsorsState).
// - RepulsorMark: a damaged runner scares the others for two seconds (OBJECT_STATUS_REPULSOR, cleared by its
//   ObjectRepulsorHelper): repulsor while the tick is before `until`.
// - RepulsionRules: EnableRepulsors, RepulsedDistance (how much further than its vision it runs), the idle look period.
// - RepulsionEvents: what the tick's runners do, applied after the step in order.
// Simulation state: Repulsable and RepulsorMark are checkpointed; the rules come from AIData.
export namespace generalszh::gameplay
{
struct Repulsable
{
	Engine::Math::Fixed vision;
	std::uint64_t nextIdleLook{0};
	std::int32_t timer{0};
	std::uint8_t waitFrames{10};
	std::uint8_t fleeing{0};
	std::uint8_t lastMode{0xFF};
	std::uint8_t firstIdleLook{1};
};

struct RepulsorMark
{
	std::uint64_t until{0};
};

struct RepulsionRules
{
	bool enabled{false};
	Engine::Math::Fixed repulsedDistance;
	std::uint64_t idleTicks{60};
};

struct RepulsionEvent
{
	enum class Kind : std::uint8_t
	{
		Flee,          // AI_MOVE_AWAY_FROM_REPULSORS, away from `threat`
		WanderInPlace, // its run over: AI_WANDER_IN_PLACE
	};
	ecs::Entity unit;
	ecs::Entity threat;
	Kind kind{Kind::Flee};
};

struct RepulsionEvents : ecs::ChunkOutputs<RepulsionEvent>
{
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::Repulsable>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.repulsable";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ComponentTraits<generalszh::gameplay::RepulsorMark>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.repulsor_mark";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<generalszh::gameplay::RepulsionRules>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.repulsion_rules";
};

template<>
struct ResourceTraits<generalszh::gameplay::RepulsionEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.repulsion_events";
};
}
