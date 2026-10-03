export module engine.gameplay.common.movement.systems.route_traversal_system;
import std;
export import engine.gameplay.common.movement.components.route_traversal;
export import engine.gameplay.common.movement.resources.route_curves;
export import engine.gameplay.common.spatial.components.transform;
export import engine.ecs.system.system;

export namespace engine::gameplay
{
// Target generation only: locomotion, collision and action vocabulary are
// composed separately. Progress uses the actual actor position, so a blocked
// actor does not advance on a timer or jump to later points.
struct RouteTraversalSystem
{
	using Query = ecs::Query<ecs::Read<Transform>, ecs::Write<RouteTraversal>, ecs::Write<RouteSteering>>;
	using Resources = ecs::Resources<ecs::Read<RouteCurves>>;
	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const {
		using namespace Engine::Math;
		const auto positions = chunk.Get<Transform>(); const auto routes = chunk.Get<RouteTraversal>(); const auto targets = chunk.Get<RouteSteering>();
		const auto &library = context.Read<RouteCurves>().curves;
		for (std::size_t row = 0; row < routes.size(); ++row) {
			auto &route = routes[row]; auto &target = targets[row]; target = {};
			if (!route.enabled) continue;
			if (!route.curve || route.curve > library.size()) throw std::out_of_range("route curve definition");
			const auto &curve = library[route.curve - 1];
			if (route.start > route.end || route.advance < Fixed{} || route.reach_distance < Fixed{} || !route.axes || (route.axes & ~7u) ||
				!curve.IsValid() || route.start < curve.keys.front() || route.end > (curve.keys.size() == 1 ? Fixed::One() : curve.keys.back()))
				throw std::invalid_argument("invalid route traversal parameters");
			route.parameter = std::clamp(route.parameter, route.start, route.end);
			auto expected = curve.Sample(route.parameter); auto delta = expected - positions[row].position;
			if (!(route.axes & 1)) delta.x = {}; if (!(route.axes & 2)) delta.y = {}; if (!(route.axes & 4)) delta.z = {};
			if (!route.complete && Length(delta) < route.reach_distance) {
				auto next = route.parameter + route.advance;
				if (next > route.end - route.advance * Fixed::Half()) next = route.end;
				if (next >= route.end) {
					if (route.looping && route.end > route.start) next = route.start;
					else {next = route.end; route.complete = 1;}
				}
				route.parameter = next; expected = curve.Sample(next);
			}
			target = {expected, 1, route.complete};
		}
	}
};
}
export namespace ecs
{
template<> struct SystemTraits<engine::gameplay::RouteTraversalSystem> {
	static constexpr std::string_view StableName = "engine.gameplay.route_traversal";
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>; using After = SystemTypeList<>;
};
}
