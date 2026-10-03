export module engine.gameplay.common.movement.components.route_traversal;
import std;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

export namespace engine::gameplay
{
struct RouteTraversal
{
	std::uint32_t curve{}, axes{7}, enabled{}, complete{};
	Engine::Math::Fixed parameter{}, start{}, end{Engine::Math::Fixed::One()};
	Engine::Math::Fixed advance{}, reach_distance{};
	std::uint32_t looping{}, reserved{};
};
struct RouteSteering
{
	Engine::Math::FixedVector3 target;
	std::uint32_t valid{}, complete{};
};
}
export namespace ecs
{
template<> struct ComponentTraits<engine::gameplay::RouteTraversal> {
	static constexpr std::string_view StableName = "engine.gameplay.route_traversal";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
template<> struct ComponentTraits<engine::gameplay::RouteSteering> {
	static constexpr std::string_view StableName = "engine.gameplay.route_steering";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
