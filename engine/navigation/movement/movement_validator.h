#pragma once
#include <span>
#include "engine/navigation/pathfinder_api.h"
// Native occupancy policy and per-query movement context.
struct TCheckMovementInfo
{
	// Input
	ICoord2D					cell;
	PathfindLayerEnum layer;
	Int								radius;
	Bool							centerInCell;
	Bool							considerTransient;
	LocomotorSurfaceTypeMask acceptableSurfaces;
	// Output
	Int								allyFixedCount;
	Bool							enemyFixed;
	Bool							allyMoving;
	Bool							allyGoal;
};

namespace navigation {
struct MovementContext
{
	const Object *object = nullptr;
	ObjectID objectId = INVALID_ID;
	ObjectID ignoredId = INVALID_ID;
	Int radius = 0;
	Int cellsAbove = 0;
    mutable RelationshipQueryCache relationships;
#ifdef INFANTRY_MOVES_THROUGH_INFANTRY
	Bool infantry = false;
#endif
};

struct GroundSegmentValidation
{
	MovementContext context;
	TCheckMovementInfo movement{};
	ICoord2D end{};
	LocomotorSurfaceTypeMask acceptableSurfaces = LOCOMOTORSURFACE_GROUND;
	Bool isCrusher = false;
	Int pathDiameter = 0;
    Bool isHuman = true;
    Bool legal = true;
    Bool checkStaticFootprint = false;
    Bool allowPinched = false;
};

class MovementValidator {
    Pathfinder& world_;
    static Int visit(Pathfinder*, PathfindCell*, PathfindCell*, Int, Int, void*);
public:
    explicit MovementValidator(Pathfinder& world) : world_(world) {}
    static MovementContext prepare(const Object*, Int radius, Bool center);
    static OccupantSnapshot captureOccupants(const MovementContext&,
        std::span<const std::uint32_t> relevantIds={});
    CellSnapshot captureCells(PathfindLayerEnum);
    CellState captureCell(PathfindLayerEnum,int x,int y);
    // Disable moving-traffic hints only when the caller consumes blockers.
    // Explicit transient checks still evaluate moving occupants completely.
    Bool check(const MovementContext&, TCheckMovementInfo&, bool collectMovingTraffic=true);
    Bool segment(GroundSegmentValidation&, const Coord3D&, const Coord3D&);
};
}
