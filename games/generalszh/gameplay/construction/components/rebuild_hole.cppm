export module games.generalszh.gameplay.construction.components.rebuild_hole;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

// A GLA rebuild hole (RebuildHoleBehavior): the structure it rebuilds (a definition index), the one that left it (the
// spawner), its worker and what that worker is putting up, and the ticks until the next worker; and a worker's hole.
export namespace generalszh::gameplay
{
struct RebuildHole
{
	std::uint32_t rebuild{0xFFFFFFFFu};
	std::uint32_t reserved{0};
	ecs::Entity spawner;
	ecs::Entity worker;
	ecs::Entity reconstructing;
	std::uint64_t workerWait{0};
};

struct RebuildWorker
{
	ecs::Entity hole;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::RebuildHole>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.rebuild_hole";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ComponentTraits<generalszh::gameplay::RebuildWorker>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.rebuild_worker";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
