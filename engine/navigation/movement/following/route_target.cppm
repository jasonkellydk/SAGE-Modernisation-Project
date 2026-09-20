module;
#include <cmath>

export module engine.navigation.movement.following.route_target;
export import engine.navigation.movement.following.route_geometry;

export namespace navigation::following {
// Copied when the optimized route changes. No waypoint or game-object pointers
// survive here; the caller supplies current position, layer and terrain checks.
struct TargetEdge {
    RoutePoint start,end,after;
    float dx=0,dy=0,length=0;
    int startLayer=0,endLayer=0,afterLayer=0;
    bool previousAboveGround=false,optimizable=false,hasAfter=false;
};
struct RouteTarget { RoutePoint point; int layer; };

template<class Clear>
RouteTarget selectRouteTarget(const TargetEdge& edge,RoutePoint position,
    int currentLayer,int initialLayer,float cellSize,Clear clear,int groundLayer=0) {
    int layer=initialLayer;
    if (edge.previousAboveGround || edge.startLayer>groundLayer) layer=edge.startLayer;
    if (edge.endLayer>groundLayer) layer=edge.endLayer;
    const float px=position.x-edge.start.x,py=position.y-edge.start.y;
    float along=edge.dx*px+edge.dy*py;
    if (along<0) along=0;
    const float offsetSquared=px*px+py*py-along*along;
    const float offset=offsetSquared<=0?0:std::sqrt(offsetSquared);
    const float maximumError=3.0f*cellSize;
    const float inverseError=1.0/maximumError;
    float k=offset*inverseError;
    if (k>1) k=1;
    RoutePoint target{};
    bool found=false;
    if (!found && clear(edge.end,layer)) {
        target=edge.end;found=true;
        if (edge.hasAfter) {
            const RoutePoint candidate{(edge.end.x+edge.after.x)*0.5f,
                (edge.end.y+edge.after.y)*0.5f,edge.end.z};
            const bool veryClose=edge.length-along<1.0f;
            const bool tryAhead=veryClose || (along>edge.length*0.5 && edge.optimizable &&
                edge.startLayer==edge.endLayer && currentLayer==groundLayer);
            if (tryAhead && (veryClose || clear(candidate,edge.endLayer))) target=candidate;
        }
    } else if (!found && k>0.5f) {
        const float distance=along+0.5*(edge.length-along);
        target={edge.start.x+distance*edge.dx,edge.start.y+distance*edge.dy,edge.start.z};
        if (clear(target,layer)) { k=0.5f;found=true; }
    }
    along+=(1.0f-k)*(edge.length-along);
    if (!found) {
        if (along>edge.length) target=edge.end;
        else {
            target={edge.start.x+along*edge.dx,edge.start.y+along*edge.dy,edge.start.z};
            if (std::abs(position.x-target.x)<1 && std::abs(position.y-target.y)<1 && edge.hasAfter)
                target=edge.after;
        }
    }
    return {target,layer};
}
}
