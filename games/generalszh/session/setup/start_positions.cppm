export module games.generalszh.session.setup.start_positions;
import std;

export import games.generalszh.session.setup.game_setup;
export import Engine.Core.Math.FixedVector;

// GameLogic.cpp populateRandomStartPosition ("the new way puts teammates next to each other"): the start spots of the
// players who left theirs random. The first of them, when no player has picked one, takes a random free spot; after
// that a player whose team has no spot yet (or who has no team) takes the free spot farthest, summed, from every spot
// taken, and a teammate the free spot nearest its team's first. Distances are between the map cache's
// Player_<n>_Start waypoints (a missing one counts as 1000000 away).
export namespace generalszh::session::setup
{
inline constexpr std::int64_t MissingStartDistance = 1000000; // "couldn't find a waypoint. must be kinda far away."

// `numPlayers`: the map's start spots (MapMetaData m_numPlayers). `spots[n]`: Player_<n+1>_Start (none: not on the map).
// `draw(high)`: GameLogicRandomValue(0, high). Returns each slot's start spot: its own when it picked one, the chosen one
// for a random player, -1 for an empty slot or an observer (who does not play here).
inline std::array<int, MaxSlots> PopulateRandomStartPositions(const GameSetup &setup, int numPlayers,
	std::span<const std::optional<Engine::Math::FixedVector2>> spots, const std::function<int(int)> &draw)
{
	using Engine::Math::Fixed;
	std::array<int, MaxSlots> chosen{};
	for (int index = 0; index < MaxSlots; ++index)
		chosen[static_cast<std::size_t>(index)] = setup.slots[static_cast<std::size_t>(index)].startPos;
	const int count = std::clamp(numPlayers, 0, MaxSlots);

	// The start spots' distances (numPlayers by numPlayers).
	std::array<std::array<Fixed, MaxSlots>, MaxSlots> distance{};
	for (int i = 0; i < count; ++i)
		for (int j = 0; j < count; ++j)
		{
			if (i == j)
				continue;
			const auto &a = static_cast<std::size_t>(i) < spots.size() ? spots[static_cast<std::size_t>(i)] : std::nullopt;
			const auto &b = static_cast<std::size_t>(j) < spots.size() ? spots[static_cast<std::size_t>(j)] : std::nullopt;
			distance[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
				a && b ? Engine::Math::Distance(*a, *b) : Fixed::FromInt(MissingStartDistance);
		}

	const auto playing = [&](int slot) {
		const GameSlot &each = setup.slots[static_cast<std::size_t>(slot)];
		return each.Occupied() && !each.Observer();
	};
	// GameInfo::isStartPositionTaken: any slot holding it.
	const auto heldBySlot = [&](int spot) { return std::ranges::find(chosen, spot) != chosen.end(); };

	bool picked = false;
	std::array<bool, MaxSlots> taken{};
	for (int spot = 0; spot < MaxSlots; ++spot)
		taken[static_cast<std::size_t>(spot)] = spot >= count;
	for (int slot = 0; slot < MaxSlots; ++slot)
	{
		const int spot = chosen[static_cast<std::size_t>(slot)];
		if (!playing(slot) || spot < 0)
			continue;
		picked = true;
		if (spot < MaxSlots)
			taken[static_cast<std::size_t>(spot)] = true;
	}
	if (count == 0)
	{
		for (int slot = 0; slot < MaxSlots; ++slot)
			if (!playing(slot))
				chosen[static_cast<std::size_t>(slot)] = -1;
		return chosen;
	}

	std::array<int, MaxSlots> teamSpot{};
	teamSpot.fill(-1);
	for (int slot = 0; slot < MaxSlots; ++slot)
	{
		if (!playing(slot))
			continue;
		int &spot = chosen[static_cast<std::size_t>(slot)];
		if (spot >= 0 && spot < count)
			continue; // already assigned
		spot = -1;
		const int team = setup.slots[static_cast<std::size_t>(slot)].team;
		const bool hasTeam = team >= 0 && team < MaxSlots;
		if (!picked)
		{
			// The first real spot: at random, drawn again while taken.
			int candidate = -1;
			while (candidate == -1)
			{
				candidate = draw(count - 1);
				if (heldBySlot(candidate))
					candidate = -1;
			}
			picked = true;
			spot = candidate;
			taken[static_cast<std::size_t>(candidate)] = true;
			if (hasTeam)
				teamSpot[static_cast<std::size_t>(team)] = candidate;
		}
		else if (!hasTeam || teamSpot[static_cast<std::size_t>(team)] == -1)
		{
			// Farthest from all the spots taken (the first free one unless another is strictly farther).
			Fixed farthestDistance{};
			int farthest = -1;
			for (int candidate = 0; candidate < count; ++candidate)
			{
				if (taken[static_cast<std::size_t>(candidate)])
					continue;
				Fixed sum{};
				for (int other = 0; other < count; ++other)
					if (taken[static_cast<std::size_t>(other)] && other != candidate)
						sum = sum + distance[static_cast<std::size_t>(candidate)][static_cast<std::size_t>(other)];
				if (farthest < 0 || sum > farthestDistance)
				{
					farthestDistance = sum;
					farthest = candidate;
				}
			}
			if (farthest < 0)
				continue; // no free spot (the original asserts)
			spot = farthest;
			taken[static_cast<std::size_t>(farthest)] = true;
			if (hasTeam)
				teamSpot[static_cast<std::size_t>(team)] = farthest;
		}
		else
		{
			// Nearest to the team's spot (0 when every spot is taken, as the original's default).
			Fixed closestDistance = Fixed::Max();
			int closest = 0;
			const int anchor = teamSpot[static_cast<std::size_t>(team)];
			for (int candidate = 0; candidate < count; ++candidate)
			{
				const Fixed between = distance[static_cast<std::size_t>(anchor)][static_cast<std::size_t>(candidate)];
				if (!taken[static_cast<std::size_t>(candidate)] && between < closestDistance)
				{
					closestDistance = between;
					closest = candidate;
				}
			}
			spot = closest;
			taken[static_cast<std::size_t>(closest)] = true;
		}
	}
	for (int slot = 0; slot < MaxSlots; ++slot)
		if (!playing(slot))
			chosen[static_cast<std::size_t>(slot)] = -1;
	return chosen;
}
}
