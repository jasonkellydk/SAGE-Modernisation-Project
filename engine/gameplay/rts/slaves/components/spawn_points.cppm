export module engine.gameplay.rts.slaves.components.spawn_points;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;

// A spawner that puts its spawns on places of its own (the original's SpawnPointProductionExitUpdate: a Stinger Site's
// soldiers on its SpawnPoint bones): how many places its model has and who stands on each (none: free). A spawn is
// only made while one is free (reserveDoorForExit); a place frees once the one on it is gone (revalidateOccupiers:
// findObjectByID). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct SpawnPoints
{
	static constexpr std::uint32_t Capacity = 10; // MAX_SPAWN_POINTS
	std::uint32_t count{0};
	std::uint32_t reserved{0};
	std::array<ecs::Entity, Capacity> occupier{};

	// The first free place, if any.
	std::optional<std::uint32_t> Free() const noexcept
	{
		for (std::uint32_t index = 0; index < count && index < Capacity; ++index)
			if (occupier[index] == ecs::Entity{})
				return index;
		return std::nullopt;
	}
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::SpawnPoints>
{
	static constexpr std::string_view StableName = "engine.gameplay.spawn_points";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
