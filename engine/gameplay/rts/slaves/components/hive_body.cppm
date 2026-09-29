export module engine.gameplay.rts.slaves.components.hive_body;
import std;

export import engine.ecs.core.component_registry;

// A spawner whose body passes damage to its spawns (the original's HiveStructureBody): damage of a `propagate` type
// (a bit per damage type) goes instead to its spawn nearest the one who dealt it; with no spawns, damage of a
// `swallow` type does nothing.
export namespace engine::gameplay
{
struct HiveBody
{
	std::uint64_t propagate{0};
	std::uint64_t swallow{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::HiveBody>
{
	static constexpr std::string_view StableName = "engine.gameplay.hive_body";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
