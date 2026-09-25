// Modern game-facing implementation unit; included by the navigation module.
navigation::SearchWorkspace<PathfindCell> PathfindCell::s_search;

UnsignedInt PathfindCell::searchEpoch()
{
    return s_search.epoch();
}


UnsignedShort PathfindCell::getXIndex() const { return s_search.x[m_searchIndex]; }
UnsignedShort PathfindCell::getYIndex() const { return s_search.y[m_searchIndex]; }
ObjectID PathfindCell::getGoalUnit() const { return hasInfo() ? static_cast<ObjectID>(s_search.goalUnit[m_searchIndex]) : INVALID_ID; }
ObjectID PathfindCell::getGoalAircraft() const { return hasInfo() ? static_cast<ObjectID>(s_search.goalAircraft[m_searchIndex]) : INVALID_ID; }
ObjectID PathfindCell::getPosUnit() const { return hasInfo() ? static_cast<ObjectID>(s_search.positionUnit[m_searchIndex]) : INVALID_ID; }

/**
 * Constructor
 */
PathfindCell::PathfindCell() :m_searchIndex(NO_SEARCH_INDEX)
{
	reset();
}

/**
 * Destructor
 */
PathfindCell::~PathfindCell()
{
	if (hasInfo()) s_search.release(m_searchIndex);
	m_searchIndex = NO_SEARCH_INDEX;
}

/**
 * Reset the cell to default values
 */
void PathfindCell::reset()
{
	m_type = PathfindCell::CELL_CLEAR;
	m_flags = PathfindCell::NO_UNITS;
	m_aircraftGoal = false;
	m_pinched = false;
	if (hasInfo()) {
		s_search.release(m_searchIndex);
		m_searchIndex = NO_SEARCH_INDEX;
	}
	m_obstacleID = INVALID_ID;
	m_obstacleIsFence = false;
	m_obstacleIsTransparent = false;

	m_connectsToLayer = LAYER_INVALID;
	m_layer = LAYER_GROUND;

}

/**
 * Allocates an info record for a cell.
 */
Bool PathfindCell::allocateInfo(const ICoord2D& pos)
{
    if (!hasInfo()) m_searchIndex = s_search.acquire(this, pos.x, pos.y);
    else s_search.touch(m_searchIndex);
    return true;
}

void PathfindCell::releaseInfo()
{
    if (!hasInfo()) return;
    if (m_flags != NO_UNITS || m_aircraftGoal) return;
    s_search.release(m_searchIndex);
    m_searchIndex = NO_SEARCH_INDEX;
}

/**
 * Sets the goal unit into the info record for a cell.
 */
void PathfindCell::setGoalUnit(ObjectID unitID, const ICoord2D &pos )
{
    if (TheAI && TheAI->pathfinder())
        TheAI->pathfinder()->m_groundPlanner->markDynamicRegion({pos,pos});
	if (unitID==INVALID_ID) {
		// removing goal.
		if (hasInfo()) {
			s_search.goalUnit[m_searchIndex] = INVALID_ID;
			if (s_search.positionUnit[m_searchIndex] == INVALID_ID) {
				// No units here.
				engine::debug::invariant((m_flags==UNIT_GOAL), "m_flags==UNIT_GOAL", __FILE__, __LINE__, "Bad flags.");
				m_flags = NO_UNITS;
				releaseInfo();
			} else{
				m_flags = UNIT_PRESENT_MOVING;
			}
		}	else {
			engine::debug::invariant((m_flags == NO_UNITS), "m_flags == NO_UNITS", __FILE__, __LINE__, "Bad flags.");
		}
	} else {
		// adding goal.
		if (!hasInfo()) {
			engine::debug::invariant((m_flags == NO_UNITS), "m_flags == NO_UNITS", __FILE__, __LINE__, "Bad flags.");
			allocateInfo(pos);
		}
		if (!hasInfo()) {
			engine::debug::invariant(false, "debug invariant", __FILE__, __LINE__, "Ran out of pathfind cells - fatal error!!!!! jba.");
			return;
		}
		s_search.goalUnit[m_searchIndex] = unitID;
		if (unitID==s_search.positionUnit[m_searchIndex]) {
			m_flags = UNIT_PRESENT_FIXED;
		} else if (s_search.positionUnit[m_searchIndex]==INVALID_ID) {
			m_flags = UNIT_GOAL;
		}	else {
			m_flags = UNIT_GOAL_OTHER_MOVING;
		}
	}
}


/**
 * Sets the goal aircraft into the info record for a cell.
 */
void PathfindCell::setGoalAircraft(ObjectID unitID, const ICoord2D &pos )
{
    if (TheAI && TheAI->pathfinder())
        TheAI->pathfinder()->m_groundPlanner->markDynamicRegion({pos,pos});
	if (unitID==INVALID_ID) {
		// removing goal.
		if (hasInfo()) {
			s_search.goalAircraft[m_searchIndex] = INVALID_ID;
			m_aircraftGoal = false;
			releaseInfo();
		}	else {
			engine::debug::invariant((m_aircraftGoal==false), "m_aircraftGoal==false", __FILE__, __LINE__, "Bad flags.");
		}
	} else {
		// adding goal.
		if (!hasInfo()) {
			engine::debug::invariant((m_aircraftGoal==false), "m_aircraftGoal==false", __FILE__, __LINE__, "Bad flags.");
			allocateInfo(pos);
		}
		if (!hasInfo()) {
			engine::debug::invariant(false, "debug invariant", __FILE__, __LINE__, "Ran out of pathfind cells - fatal error!!!!! jba.");
			return;
		}
		s_search.goalAircraft[m_searchIndex] = unitID;
		m_aircraftGoal = true;
	}
}


/**
 * Sets the position unit into the info record for a cell.
 */
void PathfindCell::setPosUnit(ObjectID unitID, const ICoord2D &pos )
{
    if (TheAI && TheAI->pathfinder())
        TheAI->pathfinder()->m_groundPlanner->markDynamicRegion({pos,pos});
	if (unitID==INVALID_ID) {
		// removing position.
		if (hasInfo()) {
			s_search.positionUnit[m_searchIndex] = INVALID_ID;
			if (s_search.goalUnit[m_searchIndex] == INVALID_ID) {
				// No units here.
				engine::debug::invariant((m_flags==UNIT_PRESENT_MOVING), "m_flags==UNIT_PRESENT_MOVING", __FILE__, __LINE__, "Bad flags.");
				m_flags = NO_UNITS;
				releaseInfo();
			}	else {
				m_flags = UNIT_GOAL;
			}
		}	else {
			engine::debug::invariant((m_flags == NO_UNITS), "m_flags == NO_UNITS", __FILE__, __LINE__, "Bad flags.");
		}
	} else {
		// adding goal.
		if (!hasInfo()) {
			engine::debug::invariant((m_flags == NO_UNITS), "m_flags == NO_UNITS", __FILE__, __LINE__, "Bad flags.");
			allocateInfo(pos);
		}
		if (!hasInfo()) {
			engine::debug::invariant(false, "debug invariant", __FILE__, __LINE__, "Ran out of pathfind cells - fatal error!!!!! jba.");
			return;
		}
		if (s_search.goalUnit[m_searchIndex]!=INVALID_ID && (s_search.goalUnit[m_searchIndex]==s_search.positionUnit[m_searchIndex])) {
			// A unit is already occupying this cell.
			return;
		}
		s_search.positionUnit[m_searchIndex] = unitID;
		if (unitID==s_search.goalUnit[m_searchIndex]) {
			m_flags = UNIT_PRESENT_FIXED;
		} else if (s_search.goalUnit[m_searchIndex]==INVALID_ID) {
			m_flags = UNIT_PRESENT_MOVING;
		}	else {
			m_flags = UNIT_GOAL_OTHER_MOVING;
		}
	}
}


/**
 * Return the relevant obstacle ID.
 */
ObjectID PathfindCell::getObstacleID() const
{
	return m_obstacleID;
}


/**
 * Flag this cell as an obstacle, from the given one.
 * Return true if cell was flagged.
 */
Bool PathfindCell::setTypeAsObstacle( Object *obstacle, Bool isFence, const ICoord2D &pos )
{
	if (m_type!=PathfindCell::CELL_CLEAR && m_type != PathfindCell::CELL_IMPASSABLE) {
		return false;
	}

	Bool isRubble = false;
	if (obstacle->getBodyModule() && obstacle->getBodyModule()->getDamageState() == BODY_RUBBLE)
	{
		isRubble = true;
	}

	if (isRubble) {
		m_type = PathfindCell::CELL_RUBBLE;
		m_obstacleID = INVALID_ID;
		m_obstacleIsFence = false;
		m_obstacleIsTransparent = false;
		return true;
	}

	m_type = PathfindCell::CELL_OBSTACLE;
	m_obstacleID = obstacle->getID();
	m_obstacleIsFence = isFence;
	m_obstacleIsTransparent = obstacle->isKindOf(KINDOF_CAN_SEE_THROUGH_STRUCTURE);
	return true;
}

/**
 * Flag this cell as given type.
 */
void PathfindCell::setType( CellType type )
{
	if (m_obstacleID != INVALID_ID) {
		engine::debug::invariant((type == PathfindCell::CELL_OBSTACLE), "type == PathfindCell::CELL_OBSTACLE", __FILE__, __LINE__, "Wrong type.");
		m_type = PathfindCell::CELL_OBSTACLE;
		return;
	}
	m_type = type;
}

/**
 * Unflag this cell as an obstacle, from the given one.
 * Return true if this cell was previously flagged as an obstacle by this object.
 */
Bool PathfindCell::removeObstacle( Object *obstacle )
{
	if (m_type == PathfindCell::CELL_RUBBLE) {
		m_type = PathfindCell::CELL_CLEAR;
	}
	if (m_obstacleID != obstacle->getID()) return false;
	m_type = PathfindCell::CELL_CLEAR;
	m_obstacleID = INVALID_ID;
	m_obstacleIsFence = false;
	m_obstacleIsTransparent = false;
	return true;
}


Bool PathfindCell::isObstaclePresent(ObjectID objID) const
{
	if (objID != INVALID_ID && (getType() == PathfindCell::CELL_OBSTACLE))
	{
		return m_obstacleID == objID;
	}

	return false;
}


/**
 * return true if the obstacle in the cell is KINDOF_CAN_SEE_THROUGHT_STRUCTURE
 */
Bool PathfindCell::isObstacleTransparent() const
{
	return m_obstacleIsTransparent;
}

/**
 * return true if the given obstacle in the cell is a fence.
 */
Bool PathfindCell::isObstacleFence() const
{
	return m_obstacleIsFence;
}


const Int COST_ORTHOGONAL = 10;
const Int COST_DIAGONAL = 14;
const Real COST_TO_DISTANCE_FACTOR = 1.0f/10.0f;
const Real COST_TO_DISTANCE_FACTOR_SQR = COST_TO_DISTANCE_FACTOR*COST_TO_DISTANCE_FACTOR;
