export module games.generalszh.gameplay.mines.components.minefield_generator;
import std;

import engine.ecs.core.component_registry;
import engine.ecs.system.system;

// A thing that lays a minefield (the original's GenerateMinefieldBehavior): whether it has laid it (m_generated: once
// only) and whether its mines were swapped for the upgraded ones (m_upgraded). MinefieldEffects: the generation FX laid
// this tick, for presentation. Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct MinefieldGenerator
{
	std::uint8_t generated{0};
	std::uint8_t upgraded{0};
	std::uint8_t reserved[6]{};
};

struct MinefieldEffects
{
	struct Played
	{
		std::string effect;
		std::array<std::int64_t, 3> at{}; // raw fixed-point position
	};
	std::vector<Played> played;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::MinefieldGenerator>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.minefield_generator";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<generalszh::gameplay::MinefieldEffects>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.minefield_effects";
};
}
