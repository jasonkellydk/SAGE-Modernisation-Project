export module games.generalszh.gameplay.containment.components.scripted_evacuation;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// A transport a script sent to `destination` to let its riders out (aiMoveToAndEvacuate / aiMoveToAndEvacuateAndExit,
// CMD_FROM_SCRIPT: doCreateReinforcements' transports), as data: the machine's stage, where it came from, and whether it
// leaves again. Two machines, by its AI:
//   the AIUpdateInterface's (AIMoveAndEvacuateState, locked: no player's order reaches it): Moving to the destination;
//     there (or the move over) its riders with an AI get out (aiEvacuate(FALSE, CMD_FROM_AI)) and its team becomes
//     active; leaving, it goes back where it started (AIMoveAndDeleteState: invalid positions allowed) and is removed
//     once there; else it idles.
//   a ChinookAIUpdate's (retail: MOVE_TO_AND_EVAC / MOVE_TO_AND_EVAC_AND_EXIT, not locked): Moving there, then Landing
//     (ChinookTakeoffOrLandingState); down, everyone aboard is out at once (ChinookEvacuateState: removeAllContained)
//     and its team becomes active; TakingOff back to its height; leaving, HeadingOff: straight for where it was made
//     (ChinookHeadOffMapState: aiMoveToPosition as a temporary state for at most 20 seconds), then removed once outside
//     the map, border included; else it idles.
// ScriptedEvacuationEvents: what the tick's machines did beyond their transports (applied after the step).
// Simulation state: checkpointed.
export namespace generalszh::gameplay
{
enum class EvacuationStage : std::uint8_t
{
	Moving,
	Landing,
	TakingOff,
	Leaving,
	HeadingOff,
};

struct ScriptedEvacuation
{
	Engine::Math::FixedVector2 destination;
	Engine::Math::FixedVector2 origin;
	std::uint64_t moveEnds{0}; // HeadingOff: the tick its temporary move gives up (m_temporaryStateFramEnd)
	EvacuationStage stage{EvacuationStage::Moving};
	std::uint8_t exits{0};   // ...AndExit
	std::uint8_t chinook{0}; // ChinookAIUpdate's machine
	std::uint8_t locked{0};  // the machine is locked (AIMoveAndEvacuateState / AIMoveAndDeleteState)
	std::uint32_t reserved{0};
};

struct ScriptedEvacuationEvent
{
	enum class Kind : std::uint8_t
	{
		ActivateTeam, // obj->getTeam()->setActive()
		Remove,       // TheGameLogic->destroyObject
	};
	ecs::Entity unit;
	Kind kind{Kind::ActivateTeam};
};

struct ScriptedEvacuationEvents : ecs::ChunkOutputs<ScriptedEvacuationEvent>
{
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::ScriptedEvacuation>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.scripted_evacuation";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::ScriptedEvacuation &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.destination.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.destination.y.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.origin.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.origin.y.Raw()));
		hasher.AppendU64(value.moveEnds);
		hasher.AppendU64(static_cast<std::uint64_t>(value.stage) | (std::uint64_t{value.exits} << 8) | (std::uint64_t{value.chinook} << 16) |
			(std::uint64_t{value.locked} << 24));
	}
};

template<>
struct ResourceTraits<generalszh::gameplay::ScriptedEvacuationEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.scripted_evacuation_events";
};
}
