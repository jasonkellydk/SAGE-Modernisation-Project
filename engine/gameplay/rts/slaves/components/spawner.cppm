export module engine.gameplay.rts.slaves.components.spawner;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

// Something that keeps a number of others in the world (the original's
// SpawnBehavior: a supply centre's free truck, a tunnel's defenders, drones):
// what it makes (its templates in turn), those it made that are still about,
// when each missing one may be made again (made when the tick is past it),
// how long a lost one takes to replace, whether it makes one batch only, and
// whether its spawns go down with it.
export namespace engine::gameplay
{
struct Spawner
{
	static constexpr std::size_t MaxSpawns = 16; // an angry mob keeps ten
	static constexpr std::size_t MaxTemplates = 4;
	static constexpr std::uint32_t NoTemplate = 0xFFFFFFFFu;
	std::array<ecs::Entity, MaxSpawns> spawned{};
	std::array<std::uint64_t, MaxSpawns> due{};
	std::array<std::uint32_t, MaxTemplates> templates{NoTemplate, NoTemplate, NoTemplate, NoTemplate};
	std::uint64_t replaceDelay{0};
	std::uint64_t nextUpdate{0};
	std::int32_t oneShotLeft{-1}; // below zero: it keeps replacing
	std::uint8_t spawnedCount{0};
	std::uint8_t dueCount{0};
	std::uint8_t templateCount{0};
	std::uint8_t cursor{0};
	bool active{true};
	bool requireSpawner{false}; // SpawnedRequireSpawner: its spawns die with it
	bool budding{false};        // ExitByBudding: a new spawn comes out of the spawn nearest it
	bool aggregateHealth{false}; // AggregateHealth: its health is its spawns', and it goes with the last of them
	std::uint8_t number{0};     // SpawnNumber (a whole set, for the aggregate)
	bool spawnsAreWeapons{false}; // SPAWNS_ARE_THE_WEAPONS: its spawns share its timed disables (DisableFollowSystem)
	std::uint8_t reserved[10]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Spawner>
{
	static constexpr std::string_view StableName = "engine.gameplay.spawner";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Spawner &value, StateHasher &hasher) noexcept
	{
		for (std::size_t index = 0; index < value.spawnedCount; ++index)
			hasher.AppendU64((std::uint64_t{value.spawned[index].index} << 32) | value.spawned[index].generation);
		for (std::size_t index = 0; index < value.dueCount; ++index)
			hasher.AppendU64(value.due[index]);
		for (const std::uint32_t id : value.templates)
			hasher.AppendU64(id);
		hasher.AppendU64(value.replaceDelay);
		hasher.AppendU64(value.nextUpdate);
		hasher.AppendU64((std::uint64_t{static_cast<std::uint32_t>(value.oneShotLeft)} << 32) | (std::uint64_t{value.spawnedCount} << 24) |
			(std::uint64_t{value.dueCount} << 16) | (std::uint64_t{value.templateCount} << 8) | value.cursor);
		hasher.AppendU64((value.active ? 1u : 0u) | (value.requireSpawner ? 2u : 0u) | (value.budding ? 4u : 0u) | (value.aggregateHealth ? 8u : 0u) | (value.spawnsAreWeapons ? 16u : 0u) |
			(std::uint64_t{value.number} << 8));
	}
};
}
