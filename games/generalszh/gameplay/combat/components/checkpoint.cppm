export module games.generalszh.gameplay.combat.components.checkpoint;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// A gate that opens for its friends (the original's CheckpointUpdate: AmericaCheckpoint): whether an ally and an enemy
// were near at its last look (m_allyNear, m_enemyNear), its gate's state (the model's DOOR_1_OPENING / DOOR_1_CLOSING,
// neither until it first changes) and its geometry's minor radius now and at most (m_maxMinorRadius: its definition's),
// the gate's width shrinking while open and growing back while shut. Simulation state: checkpointed.
export namespace generalszh::gameplay
{
enum class CheckpointGate : std::uint8_t
{
	None,
	Opening,
	Closing,
};

struct Checkpoint
{
	Engine::Math::Fixed minorRadius;
	Engine::Math::Fixed maxMinorRadius;
	std::uint8_t allyNear{0};
	std::uint8_t enemyNear{0};
	CheckpointGate gate{CheckpointGate::None};
	std::uint8_t padding[5]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::Checkpoint>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.checkpoint";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
