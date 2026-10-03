export module engine.gameplay.common.identity.components.level_identity;
import std;
export import engine.ecs.core.component_registry;

export namespace engine::gameplay
{
// A level-local authored identity, independent of runtime/network object IDs.
struct LevelIdentity { std::uint64_t value{}; };
}
export namespace ecs
{
template<> struct ComponentTraits<engine::gameplay::LevelIdentity>
{
	static constexpr std::string_view StableName = "engine.gameplay.level_identity";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
