#pragma once
#include "engine/navigation/pathfinder_api.h"
namespace navigation {
// Restores raw avoidance/rejoin cells without changing smoothed path links.
class GroundPathBuilder {
    Pathfinder& world_;
    static Int visit(Pathfinder*, PathfindCell*, PathfindCell*, Int, Int, void*);
public:
    explicit GroundPathBuilder(Pathfinder& world) : world_(world) {}
    void materialize(Path*, Bool center);
    void coordinate(Int cellX, Int cellY, Bool centerInCell, Coord3D& pos, PathfindLayerEnum layer);
};
}
