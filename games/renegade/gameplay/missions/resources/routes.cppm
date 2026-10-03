export module games.renegade.gameplay.missions.resources.routes;
import std;
export import games.renegade.gameplay.humans.components.goto_action;
export import engine.gameplay.common.movement.systems.route_traversal_system;
export import engine.ecs.system.chunk_outputs;
export import engine.level.model.level;
import engine.ecs.core.resource_store;
export namespace renegade {
struct MissionRoute {std::uint32_t id{}; engine::gameplay::RouteTraversal traversal; bool supported{};};
struct MissionRoutes {std::vector<MissionRoute> routes;};
struct HumanGotoRequest {ecs::Entity subject; HumanGoto action; engine::gameplay::RouteTraversal traversal;};
struct HumanGotoRequests : ecs::ChunkOutputs<HumanGotoRequest> {};
std::expected<void,std::string> PrepareMissionRoutes(const engine::level::Level& level,
	engine::gameplay::RouteCurves& curves,MissionRoutes& routes) {
	using namespace Engine::Math;
	for(const auto& path:level.navigationPaths) {
		MissionRoute route; route.id=path.id;
		// Loop padding and action portals need their own source-backed traversal
		// before they can be dispatched. Keep their inventory visible and pending.
		if(path.looping || path.markers.empty()) {routes.routes.push_back(route);continue;}
		std::vector<FixedVector3> points;bool actions{};
		for(const auto id:path.markers) {
			const auto marker=std::ranges::find(level.markers,id,&engine::level::Marker::id);
			if(marker==level.markers.end()) return std::unexpected("mission route references missing marker");
			points.push_back(marker->position); actions|=marker->properties.Get<std::int64_t>("renegade.flags").value_or(0)!=0;
		}
		if(actions) {routes.routes.push_back(route);continue;}
		auto curve=engine::level::BuildCardinalPathCurve3(points,Fixed::One());
		if(!curve) return std::unexpected(curve.error());
		// wwphys/Path.cpp: linear Human spline, speed 2.5, assumed 30Hz,
		// eight-frame parameter advance, four-frame look-ahead, radius 0.1.
		route.traversal.curve=std::uint32_t(curves.curves.size()+1); route.traversal.axes=3;
		route.traversal.advance=curve->length>Fixed{} ? Fixed::FromRatio(2,3)/curve->length : Fixed::One();
		route.traversal.reach_distance=Fixed::FromRatio(13,30); route.traversal.enabled=1; route.supported=true;
		curves.curves.push_back(std::move(*curve)); routes.routes.push_back(route);
	}
	return {};
}
}
export namespace ecs {
template<> struct ResourceTraits<renegade::MissionRoutes> {static constexpr std::string_view StableName="renegade.mission_routes";};
template<> struct ResourceTraits<renegade::HumanGotoRequests> {static constexpr std::string_view StableName="renegade.human_goto_requests";};
}
