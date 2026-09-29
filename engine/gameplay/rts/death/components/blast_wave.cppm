export module engine.gameplay.rts.death.components.blast_wave;
import std;

export import engine.ecs.core.component_registry;

// A dying missile's blast waves under way (the original's NeutronMissileSlowDeathBehavior: m_completedBlasts,
// m_completedScorchBlasts, m_scorchPlaced): which blasts and scorch waves (by their place in its BlastWaveDefinition)
// have gone off, and whether its scorch mark is down. Scorched: burned by a scorch wave (MODELCONDITION_BURNED),
// which nothing takes away. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct BlastWave
{
	std::uint16_t blasted{0};
	std::uint16_t scorched{0};
	std::uint8_t scorchPlaced{0};
	std::uint8_t reserved[3]{};
};

struct Scorched
{
	std::uint8_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::BlastWave>
{
	static constexpr std::string_view StableName = "engine.gameplay.blast_wave";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ComponentTraits<engine::gameplay::Scorched>
{
	static constexpr std::string_view StableName = "engine.gameplay.scorched";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
