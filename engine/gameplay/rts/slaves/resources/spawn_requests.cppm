export module engine.gameplay.rts.slaves.resources.spawn_requests;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// Spawns asked for this tick (made by the game between ticks, which then adds
// them to their spawner's list).
export namespace engine::gameplay
{
struct SpawnRequest
{
	ecs::Entity spawner;
	std::uint32_t definition{0};
};

struct SpawnRequests
{
	std::vector<SpawnRequest> list;
	// Spawners whose health was their spawns' and whose last spawn is gone: removed (destroyObject, not killed).
	std::vector<ecs::Entity> emptied;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::SpawnRequests>
{
	static constexpr std::string_view StableName = "engine.gameplay.spawn_requests";
};
}
