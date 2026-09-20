#if defined(RTS_DEBUG)
extern void addIcon(const Coord3D*, Real, Int, RGBColor);
#endif
namespace navigation {
namespace {
struct GroundPathMaterialization {
	PathNode *previous;
	ICoord2D end;
	Bool center;
};
}

Int GroundPathBuilder::visit(Pathfinder *pathfinder,
	PathfindCell *from, PathfindCell *, Int x, Int y, void *userData)
{
	auto& materialization = *static_cast<GroundPathMaterialization *>(userData);
	if (x == materialization.end.x && y == materialization.end.y)
		return 1; // The endpoint already exists; stop before the iterator's extra end probe.
	if (!from)
		return 0; // Preserve the unit's exact starting position.
	Coord3D position;
	pathfinder->adjustCoordToCell(x, y, materialization.center, position, LAYER_GROUND);
	auto *node = newInstance(PathNode);
	node->setPosition(&position);
	node->setLayer(LAYER_GROUND);
	node->setCanOptimize(true);
	materialization.previous->append(node);
	materialization.previous = node;
	pathfinder->recordNavigationWork(1);
	return 0;
}

void GroundPathBuilder::materialize(Path *path, Bool center)
{
	// Searches may use long edges, but moveAllies consumes raw cells,
	// not optimized segments. Keep its nearby occupancy points.
	// Insert after smoothing without changing the optimized links: smoothing
	// needs only the search waypoints, not every intermediate avoidance cell.
	// Hierarchical paths and layer transitions do not use this conversion.
	for (auto *start = path->getFirstNode(); start && start->getNext();) {
		auto *end = start->getNext();
		if (start->getLayer() == LAYER_GROUND && end->getLayer() == LAYER_GROUND) {
			ICoord2D fromCell, toCell;
			world_.worldToCell(start->getPosition(), &fromCell);
			world_.worldToCell(end->getPosition(), &toCell);
			if ((abs(toCell.x - fromCell.x) > 1 || abs(toCell.y - fromCell.y) > 1) &&
				world_.getCell(LAYER_GROUND, fromCell.x, fromCell.y) &&
				world_.getCell(LAYER_GROUND, toCell.x, toCell.y)) {
				GroundPathMaterialization materialization{start, toCell, center};
				world_.iterateCellsAlongLine(fromCell, toCell, LAYER_GROUND,
					&GroundPathBuilder::visit, &materialization);
			}
		}
		start = end;
	}
}


void GroundPathBuilder::coordinate(Int cellX, Int cellY, Bool centerInCell, Coord3D &pos, PathfindLayerEnum layer)
{
	if (centerInCell) {
		pos.x = ((Real)cellX + 0.5f) * PATHFIND_CELL_SIZE_F;
		pos.y = ((Real)cellY + 0.5f) * PATHFIND_CELL_SIZE_F;
	} else {
		pos.x = ((Real)cellX+0.05) * PATHFIND_CELL_SIZE_F;
		pos.y = ((Real)cellY+0.05) * PATHFIND_CELL_SIZE_F;
	}
	pos.z = TheTerrainLogic->getLayerHeight( pos.x, pos.y, layer );
}
}
