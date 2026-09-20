namespace navigation {
DestinationQuery makeDestinationQuery(const Object* obj,Int radius,Bool center) {
    DestinationQuery query;
    query.radius=radius;
    query.center=center!=0;
    query.hasMover=obj!=nullptr;
    if (obj && obj->getAIUpdateInterface()) {
        query.ignored=std::uint32_t(obj->getAIUpdateInterface()->getIgnoredObstacleID());
        query.aircraft=obj->getAI()->isAircraftThatAdjustsDestination()!=0;
        query.self=std::uint32_t(obj->getID());
    }
    return query;
}
MovementContext MovementValidator::prepare(
	const Object *obj, Int radius, Bool center)
{
	MovementContext context;
	context.object = obj;
	context.radius = radius;
	context.cellsAbove = radius + (center ? 1 : 0);
	if (obj) {
		context.objectId = obj->getID();
		if (const auto* ai = obj->getAIUpdateInterface())
			context.ignoredId = ai->getIgnoredObstacleID();
#ifdef INFANTRY_MOVES_THROUGH_INFANTRY
		context.infantry = obj->isKindOf(KINDOF_INFANTRY);
#endif
	}
	return context;
}

namespace {
struct NativeCorridorCell {
    const PathfindCell* cell;
    bool valid() const { return cell!=nullptr; }
    TerrainKind kind() const { return static_cast<TerrainKind>(cell->getType()); }
    bool isFence() const { return cell->isObstacleFence()!=0; }
    bool fixed() const { return cell->getFlags()==PathfindCell::UNIT_PRESENT_FIXED; }
    std::uint32_t unit() const { return std::uint32_t(cell->getPosUnit()); }
};
struct NativeCorridorUnits {
    Object* find(std::uint32_t id) const { return TheGameLogic->findObjectByID(static_cast<ObjectID>(id)); }
    unsigned crushableLevel(const Object* unit) const { return unit->getCrushableLevel(); }
};
struct NativeDestinationCell {
    const PathfindCell* cell;
    bool valid() const { return cell!=nullptr; }
    TerrainKind kind() const { return static_cast<TerrainKind>(cell->getType()); }
    std::uint32_t obstacle() const { return std::uint32_t(cell->getObstacleID()); }
    std::uint32_t goal() const { return std::uint32_t(cell->getGoalUnit()); }
    std::uint32_t aircraftGoal() const { return std::uint32_t(cell->getGoalAircraft()); }
    bool empty() const { return cell->getFlags()==PathfindCell::NO_UNITS; }
    bool fixed() const { return cell->getFlags()==PathfindCell::UNIT_PRESENT_FIXED; }
    bool aircraftReserved() const { return cell->isAircraftGoal()!=0; }
};
struct NativeDestinationUnits {
    const Object* mover;
    Object* find(std::uint32_t id) const { return TheGameLogic->findObjectByID(static_cast<ObjectID>(id)); }
    bool allied(const Object* unit) const { return mover->getRelationship(unit)==ALLIES; }
    bool canCrush(Object* unit) const {
        return mover->canCrushOrSquish(unit,TEST_CRUSH_OR_SQUISH)!=0;
    }
};
struct NativeOccupants {
    const Object* mover;
    RelationshipQueryCache& relationships;
    Object* find(std::uint32_t id) const { return TheGameLogic->findObjectByID(static_cast<ObjectID>(id)); }
    Bool allied(const Object* unit) const {
        return relationships.allied(reinterpret_cast<std::uintptr_t>(mover->getTeam()),
            reinterpret_cast<std::uintptr_t>(unit->getTeam()), mover->getIsUndetectedDefector(),
            unit->getIsUndetectedDefector(), [&] { return mover->getRelationship(unit)==ALLIES; });
    }
    Bool infantry(const Object* unit) const { return unit->isKindOf(KINDOF_INFANTRY); }
    Bool canMoveAside(const Object* unit) const {
        const auto* ai = unit->getAIUpdateInterface();
        if (!ai) return false;
        return true;
    }
    Bool canCrush(Object* unit) const { return mover->canCrushOrSquish(unit, TEST_CRUSH_OR_SQUISH); }
};
}

CellSnapshot MovementValidator::captureCells(PathfindLayerEnum layer) {
    if (!world_.m_isMapReady || layer<LAYER_GROUND || layer>LAYER_LAST ||
        (layer!=LAYER_GROUND && world_.m_layers[layer].isUnused())) return {};
    const auto& bounds=world_.m_extent;
    return CellSnapshot(unsigned(layer),bounds.lo.x,bounds.lo.y,bounds.hi.x,bounds.hi.y,[&](int x,int y) {
        return captureCell(layer,x,y);
    });
}

CellState MovementValidator::captureCell(PathfindLayerEnum layer,int x,int y) {
        CellState result;
        const auto* cell=world_.getCell(layer,x,y);
        if (!cell) return result;
        const auto flags=cell->getFlags();
        result.occupancy=resolveOccupant(OccupancyCell{0,true,flags==PathfindCell::NO_UNITS,
            flags==PathfindCell::UNIT_GOAL || flags==PathfindCell::UNIT_GOAL_OTHER_MOVING,
            flags==PathfindCell::UNIT_PRESENT_MOVING || flags==PathfindCell::UNIT_GOAL_OTHER_MOVING,
            flags==PathfindCell::UNIT_PRESENT_FIXED},[&] { return std::uint32_t(cell->getPosUnit()); });
        result.goal=std::uint32_t(cell->getGoalUnit());
        result.obstacle=std::uint32_t(cell->getObstacleID());
        result.terrain=static_cast<unsigned char>(cell->getType());
        result.connection=static_cast<unsigned char>(cell->getConnectLayer());
        result.resolvedLayer=static_cast<unsigned char>(cell->getLayer());
        result.pinched=cell->getPinched()!=0;
        result.fence=cell->isObstacleFence()!=0;
        result.aircraftReserved=cell->isAircraftGoal()!=0;
        if (result.aircraftReserved) result.aircraftGoal=std::uint32_t(cell->getGoalAircraft());
        return result;
}

OccupantSnapshot MovementValidator::captureOccupants(const MovementContext& context,
    const std::span<const std::uint32_t> relevantIds) {
    if (!context.object) return {};
    const NativeOccupants units{context.object,context.relationships};
    std::vector<OccupantState> states;
    const bool dozer=context.object->isKindOf(KINDOF_DOZER);
    if (!relevantIds.empty()) {
        states.reserve(relevantIds.size());
        for (const auto id:relevantIds) {
            auto* unit=TheGameLogic->findObjectByID(static_cast<ObjectID>(id));
            if (!unit) continue;
            states.push_back({static_cast<std::uint32_t>(unit->getID()),units.allied(unit)!=0,
                units.infantry(unit)!=0,units.canMoveAside(unit)!=0,units.canCrush(unit)!=0,unit->getCrushableLevel(),
                dozer && context.object->getRelationship(unit)!=ENEMIES});
        }
        return OccupantSnapshot(states);
    }
    for (auto* unit=TheGameLogic->getFirstObject();unit;unit=unit->getNextObject()) {
        states.push_back({static_cast<std::uint32_t>(unit->getID()),units.allied(unit)!=0,
            units.infantry(unit)!=0,units.canMoveAside(unit)!=0,units.canCrush(unit)!=0,unit->getCrushableLevel(),
            dozer && context.object->getRelationship(unit)!=ENEMIES});
    }
    return OccupantSnapshot(states);
}

Bool MovementValidator::check(const MovementContext& context, TCheckMovementInfo& info, bool collectMovingTraffic)
{
    info.allyFixedCount = 0;
    info.enemyFixed = info.allyMoving = info.allyGoal = false;
    if (!context.object) return true;
    if (!collectMovingTraffic && !info.considerTransient && info.layer==LAYER_GROUND &&
        context.radius==1 && context.cellsAbove==1) {
        const auto x=info.cell.x,y=info.cell.y;
        const auto& extent=world_.m_extent;
        if (x<=extent.lo.x || y<=extent.lo.y || x>extent.hi.x || y>extent.hi.y) return false;
        const auto* left=world_.getCell(LAYER_GROUND,x-1,y-1);
        const auto* right=world_.getCell(LAYER_GROUND,x,y-1);
        const auto a=left[0].getFlags(),b=left[1].getFlags();
        const auto c=right[0].getFlags(),d=right[1].getFlags();
        if (a!=PathfindCell::UNIT_PRESENT_FIXED && b!=PathfindCell::UNIT_PRESENT_FIXED &&
            c!=PathfindCell::UNIT_PRESENT_FIXED && d!=PathfindCell::UNIT_PRESENT_FIXED) {
            const auto goal=[](auto flags) { return flags==PathfindCell::UNIT_GOAL || flags==PathfindCell::UNIT_GOAL_OTHER_MOVING; };
            info.allyGoal=goal(a)||goal(b)||goal(c)||goal(d);
            return true;
        }
    }
    OccupancyQuery query{static_cast<std::uint32_t>(context.objectId),
        static_cast<std::uint32_t>(context.ignoredId), info.cell.x, info.cell.y,
        context.radius, context.cellsAbove, info.considerTransient != 0, false};
    query.collectMovingTraffic=collectMovingTraffic;
#ifdef INFANTRY_MOVES_THROUGH_INFANTRY
    query.infantryPassThrough = context.infantry != 0;
#endif
    OccupancyResult result;
    const bool allowed = checkOccupancy(query, result, [&](Int x, Int y) {
        const auto* cell = world_.getCell(info.layer, x, y);
        if (!cell) {
            OccupancyCell invalid; invalid.valid = false; return invalid;
        }
        const auto flags = cell->getFlags();
        return resolveOccupant(OccupancyCell{0, true,
            flags == PathfindCell::NO_UNITS,
            flags == PathfindCell::UNIT_GOAL || flags == PathfindCell::UNIT_GOAL_OTHER_MOVING,
            flags == PathfindCell::UNIT_PRESENT_MOVING || flags == PathfindCell::UNIT_GOAL_OTHER_MOVING,
            flags == PathfindCell::UNIT_PRESENT_FIXED},
            [&] { return static_cast<std::uint32_t>(cell->getPosUnit()); },
            collectMovingTraffic || query.considerTransient);
    }, NativeOccupants{context.object, context.relationships});
    info.allyFixedCount = result.allyFixedCount;
    info.enemyFixed = result.enemyFixed;
    info.allyMoving = result.allyMoving;
    info.allyGoal = result.allyGoal;
    return allowed;
}

/*static*/ Int MovementValidator::visit(Pathfinder *pathfinder,
	PathfindCell *, PathfindCell *to, Int toX, Int toY, void *userData)
{
	auto *validation = static_cast<GroundSegmentValidation *>(userData);
	pathfinder->recordNavigationWork(1);
	if (!to) {
		validation->legal = false;
		return 1;
	}
	if (validation->checkStaticFootprint) {
		const auto& context = validation->context;
		const Int above = context.radius == 0 ? 1 : context.cellsAbove;
		for (Int y = toY - context.radius; y < toY + above; ++y) {
			for (Int x = toX - context.radius; x < toX + above; ++x) {
				const auto* cell = pathfinder->getCell(LAYER_GROUND, x, y);
				if (!cell || cell->getType() != PathfindCell::CELL_CLEAR) {
					validation->legal = false;
					return 1;
				}
			}
		}
	}
	auto& movement = validation->movement;
	movement.cell = {toX, toY};
	if (!MovementValidator(*pathfinder).check(validation->context, movement) ||
		movement.allyFixedCount != 0 || movement.enemyFixed) {
		validation->legal = false;
		return 1;
	}
    if (to->getPinched() && !validation->allowPinched) {
        validation->legal = false;
        return 1;
	}
	if (!pathfinder->validMovementPosition(validation->isCrusher,
		validation->acceptableSurfaces, to)) {
		validation->legal = false;
		return 1;
	}
	if (validation->pathDiameter > 0 &&
		pathfinder->clearCellForDiameter(validation->isCrusher, toX, toY,
			to->getLayer(), validation->pathDiameter) < validation->pathDiameter) {
		validation->legal = false;
		return 1;
	}
	if (validation->isHuman) {
		ICoord2D cell{toX, toY};
		if (pathfinder->checkCellOutsideExtents(cell)) {
			validation->legal = false;
			return 1;
		}
	}
	// The compatibility iterator also probes beyond its endpoint. That cell is not
	// part of this segment and may be blocked immediately after a valid turn.
	return toX == validation->end.x && toY == validation->end.y ? 2 : 0;
}

Bool MovementValidator::segment(GroundSegmentValidation& validation,
	const Coord3D& start, const Coord3D& end)
{
	validation.legal = true;
	world_.worldToCell(&end, &validation.end);
	const Int result = world_.iterateCellsAlongLine(start, end, LAYER_GROUND,
		&MovementValidator::visit, &validation);
	return result == 2 && validation.legal;
}

}
