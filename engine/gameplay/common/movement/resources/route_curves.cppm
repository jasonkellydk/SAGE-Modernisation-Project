export module engine.gameplay.common.movement.resources.route_curves;
import std;
export import engine.level.model.path_curve;
import engine.ecs.core.resource_store;
export namespace engine::gameplay {struct RouteCurves {std::vector<engine::level::PathCurve3> curves;};}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::RouteCurves> {static constexpr std::string_view StableName = "engine.gameplay.route_curves";};
}
