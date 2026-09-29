export module games.generalszh.presentation.interaction.algorithms.selection_rules;
import std;

export import engine.ecs.core.entity;

// The original's left-click selection rules (SelectionTranslator::
// onMouseLeftClick with SelectionInfo's contextCommandForNewSelection), as
// plain decisions over what is selected and what the click or drag picked:
// whether the click is left for the command translator (a context command,
// or nothing to select), deselects what it picked (prefer-selection mode on a
// point whose picks are all selected), or selects: mine first (structures
// only when one of mine is all that was picked, or a building is the only
// selectable thing in a drag), else one enemy, civilian or friend alone.
// Whether a picked object would take a context command (evaluateContextCommand
// with EVALUATE_ONLY) is the caller's to answer.
export namespace generalszh::presentation
{
enum class PickSide : std::uint8_t
{
	Mine,     // isLocallyControlled
	Friend,   // the local player's ALLIES
	Enemy,    // ENEMIES
	Civilian, // NEUTRAL
};

struct PickedObject
{
	ecs::Entity entity;
	PickSide side{PickSide::Mine};
	bool structure{false};    // KINDOF_STRUCTURE
	bool infantry{false};     // KINDOF_INFANTRY
	bool crate{false};        // KINDOF_CRATE
	bool garrisonable{false}; // TheActionManager->canPlayerGarrison
	bool contained{false};    // inside something (never selected itself)
	bool selectable{true};    // Drawable::isSelectable (a drag's lone building test)
};

// SelectionInfo.
struct SelectionInfo
{
	std::uint32_t currentMine{0}, currentMineInfantry{0}, currentMineBuildings{0}, currentFriends{0}, currentEnemies{0}, currentCivilians{0};
	std::uint32_t newMine{0}, newMineBuildings{0}, newFriends{0}, newEnemies{0}, newCivilians{0}, newCrates{0}, newGarrisonable{0};
	// The last of each kind picked (contextCommandForNewSelection's newMine, newFriendly, newEnemy, newCivilian).
	std::optional<std::size_t> lastMine, lastFriend, lastEnemy, lastCivilian;
};

inline SelectionInfo TallySelection(std::span<const PickedObject> current, std::span<const PickedObject> picked) noexcept
{
	SelectionInfo info;
	for (const PickedObject &object : current)
		switch (object.side)
		{
		case PickSide::Mine:
			++info.currentMine;
			if (object.infantry)
				++info.currentMineInfantry;
			else if (object.structure)
				++info.currentMineBuildings;
			break;
		case PickSide::Friend: ++info.currentFriends; break;
		case PickSide::Enemy: ++info.currentEnemies; break;
		case PickSide::Civilian: ++info.currentCivilians; break;
		}
	for (std::size_t index = 0; index < picked.size(); ++index)
	{
		const PickedObject &object = picked[index];
		info.newGarrisonable += object.garrisonable ? 1u : 0u;
		info.newCrates += object.crate ? 1u : 0u;
		switch (object.side)
		{
		case PickSide::Mine:
			++info.newMine;
			info.lastMine = index;
			info.newMineBuildings += object.structure ? 1u : 0u;
			break;
		case PickSide::Friend: ++info.newFriends; info.lastFriend = index; break;
		case PickSide::Enemy: ++info.newEnemies; info.lastEnemy = index; break;
		case PickSide::Civilian: ++info.newCivilians; info.lastCivilian = index; break;
		}
	}
	return info;
}

// contextCommandForNewSelection (default mouse setup): whether the click is a command rather than a selection.
// `hasCommand(i)`: whether picked[i] would take a context command.
template<typename HasCommand>
bool ContextCommandForNewSelection(const SelectionInfo &info, bool isPoint, bool preferSelection, bool forceAttack, bool forceMove,
	HasCommand &&hasCommand)
{
	if (forceAttack || forceMove)
		return false;
	if (info.currentEnemies > 0 || info.currentFriends > 0 || info.currentCivilians > 0)
		return false;
	if (info.currentMine > 0)
	{
		if (info.newEnemies > 0)
		{
			if (info.newEnemies == 1 && isPoint)
				return hasCommand(*info.lastEnemy);
			return isPoint;
		}
		if (info.newMine > 0)
		{
			if (info.newMine == 1 && isPoint && !preferSelection)
				return hasCommand(*info.lastMine);
			return false;
		}
		if (info.newFriends > 0)
		{
			if (info.newFriends == 1 && isPoint)
				return hasCommand(*info.lastFriend);
			return false;
		}
		if (info.currentMineInfantry > 0 && info.newGarrisonable == 1)
			return true;
		if (info.newCivilians > 0)
		{
			if (info.newCivilians == 1 && isPoint)
				return hasCommand(*info.lastCivilian);
			return false;
		}
		if (info.newCrates > 0)
			return info.newCrates == 1 && isPoint;
	}
	if (info.currentMine == 0)
		return false;
	return isPoint;
}

enum class ClickOutcome : std::uint8_t
{
	Pass,     // KEEP_MESSAGE: the command translator acts on the click
	Deselect, // MSG_REMOVE_FROM_SELECTED_GROUP: these leave the selection
	Replace,  // deselect all, then select these
	Add,      // select these as well
};

struct ClickSelection
{
	ClickOutcome outcome{ClickOutcome::Pass};
	std::vector<ecs::Entity> entities;
};

// SelectionTranslator::onMouseLeftClick (no GUI command pending, default mouse setup). `allPickedSelected`:
// every picked object is selected already.
template<typename HasCommand>
ClickSelection LeftClickSelection(std::span<const PickedObject> current, std::span<const PickedObject> picked, bool isPoint, bool preferSelection,
	bool forceAttack, bool forceMove, bool allPickedSelected, HasCommand &&hasCommand)
{
	ClickSelection result;
	if (picked.empty())
		return result;
	const SelectionInfo info = TallySelection(current, picked);
	if (ContextCommandForNewSelection(info, isPoint, preferSelection, forceAttack, forceMove, hasCommand))
		return result;
	bool addToGroup = preferSelection;
	if (info.currentEnemies > 0 || info.currentCivilians > 0 || info.currentFriends > 0 || info.currentMineBuildings > 0)
		addToGroup = false;
	bool selectMine = false, selectMineBuildings = false, selectEnemies = false, selectCivilians = false, selectFriends = false;
	if (info.newMine > 0)
	{
		selectMine = true;
		if (info.newMineBuildings == 1 && info.newMine == 1)
		{
			addToGroup = false;
			selectMineBuildings = true;
		}
		else if (info.newMineBuildings > 0)
		{
			// A drag whose only selectable thing is the one building gets the building.
			bool onlyTheOneBuilding = true;
			std::optional<ecs::Entity> building;
			for (const PickedObject &object : picked)
			{
				if (object.structure)
				{
					if (!building)
						building = object.entity;
					else if (*building != object.entity)
						onlyTheOneBuilding = false;
				}
				else if (object.selectable)
					onlyTheOneBuilding = false;
				if (!onlyTheOneBuilding)
					break;
			}
			if (onlyTheOneBuilding)
			{
				addToGroup = false;
				selectMineBuildings = true;
			}
		}
	}
	else if (info.newEnemies > 0 && info.newCivilians > 0 && info.newFriends > 0)
		return result;
	else if (info.newEnemies == 1)
	{
		addToGroup = false;
		selectEnemies = true;
	}
	else if (info.newCivilians == 1)
	{
		addToGroup = false;
		selectCivilians = true;
	}
	else if (info.newFriends == 1)
	{
		addToGroup = false;
		selectFriends = true;
	}
	if (!(selectMine || selectEnemies || selectCivilians || selectFriends))
		return result;
	if (preferSelection && isPoint && allPickedSelected)
	{
		result.outcome = ClickOutcome::Deselect;
		for (const PickedObject &object : picked)
			result.entities.push_back(object.entity);
		return result;
	}
	result.outcome = addToGroup ? ClickOutcome::Add : ClickOutcome::Replace;
	for (const PickedObject &object : picked)
	{
		if (object.contained)
			continue;
		bool take = false;
		if (selectMine && object.side == PickSide::Mine)
			take = !object.structure || selectMineBuildings;
		else if (object.side != PickSide::Mine)
			take = (selectEnemies && object.side == PickSide::Enemy) || (selectCivilians && object.side == PickSide::Civilian) ||
				(selectFriends && object.side == PickSide::Friend);
		if (take && std::find(result.entities.begin(), result.entities.end(), object.entity) == result.entities.end())
			result.entities.push_back(object.entity);
	}
	return result;
}

// MetaEventTranslator::onMouseEvent: a release within the drag tolerance on both axes (strictly) is a point.
inline bool IsPointClick(float anchorX, float anchorY, float x, float y, float dragTolerance) noexcept
{
	return std::abs(x - anchorX) < dragTolerance && std::abs(y - anchorY) < dragTolerance;
}
}
