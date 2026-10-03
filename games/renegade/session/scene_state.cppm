export module games.renegade.session.scene_state;
import std;
export import engine.ecs;
export import games.renegade.content.levels.level_scene;

export namespace renegade
{
struct PhysicsIdentity
{
	std::uint32_t factory{}, token{}, instance{}, flags{}, render_factory{};
};
struct ActorIdentity
{
	std::uint32_t factory{}, token{}, instance{}, innate_observer{};
};
struct SceneState
{
	std::vector<std::string> models{std::string{}}; // ModelOverride reserves zero.
	std::vector<ecs::Entity> statics;
	std::vector<ecs::Entity> actors;
	std::map<std::uint64_t, ecs::Entity> authored_entities;
	content::PlayerStart start;
	ecs::Entity player;
	std::string start_script;
};
}
export namespace ecs
{
template<> struct ComponentTraits<renegade::PhysicsIdentity>
{
	static constexpr std::string_view StableName = "renegade.physics_identity";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
template<> struct ComponentTraits<renegade::ActorIdentity>
{
	static constexpr std::string_view StableName = "renegade.actor_identity";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
template<> struct ResourceTraits<renegade::SceneState>
{ static constexpr std::string_view StableName = "renegade.scene_state"; };
}
