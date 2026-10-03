export module engine.gameplay.common.areas.components.volume_probe;
import std;
export import engine.ecs.core.component_registry;

export namespace engine::gameplay
{
// A game's composition assigns categories and an authored iteration order.
// The sensor mechanism does not interpret categories as teams/player roles.
struct VolumeProbe { std::uint64_t categories{~std::uint64_t{}}, order{}; };
}
export namespace ecs
{
template<> struct ComponentTraits<engine::gameplay::VolumeProbe>
{
	static constexpr std::string_view StableName = "engine.gameplay.volume_probe";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
