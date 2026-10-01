export module games.generalszh.hosts.game.match_results;
import std;

export import games.generalszh.session.session_view;
export import games.generalszh.session.setup.match_plan;
export import games.generalszh.shell.score.battle_honors;
export import games.generalszh.shell.score.score_screen_view_model;
import games.generalszh.hud.match_record;

// What a match leaves for the score screen and the battle honours once it ends (GameLogic::exitGame, ScoreScreen's
// populatePlayerInfo and grabMultiPlayerInfo).
export namespace generalszh::host
{
// populatePlayerInfo, SCORESCREEN_SKIRMISH: the local player's record, with the other seats as they were set up (a
// computer's level; on the local player's team: isSlotLocalAlly), and whether it was a sandbox (every other seat on the
// local player's team: GameInfo::isSandbox).
struct SkirmishResult
{
	shell::SkirmishGameRecord record;
	bool sandbox{true};
};

inline SkirmishResult ReadSkirmishResult(session::SessionView &view, std::uint32_t local, const session::setup::MatchPlan &plan)
{
	SkirmishResult result{hud::ReadGameRecord(view, local), true};
	result.record.map = plan.setup.map;
	const auto &slots = plan.setup.slots;
	const int localSlot = plan.localSlot;
	const int localTeam = slots[static_cast<std::size_t>(localSlot)].team;
	for (int slot = 0; slot < session::setup::MaxSlots; ++slot)
	{
		if (slot == localSlot)
			continue;
		const auto &seat = slots[static_cast<std::size_t>(slot)];
		const bool ally = seat.team >= 0 && seat.team == localTeam;
		if (seat.Occupied() && !ally)
			result.sandbox = false;
		result.record.others.push_back({seat.AI() ? static_cast<std::uint8_t>(seat.state) : std::uint8_t{0}, ally, seat.Occupied()});
	}
	return result;
}

// grabMultiPlayerInfo: the seats' players (player<slot>), in slot order; none without a plan.
inline std::vector<shell::ScorePlayer> SeatScorePlayers(const session::setup::MatchPlan *plan, const std::vector<shell::ScorePlayer> &players)
{
	std::vector<shell::ScorePlayer> seats;
	if (plan == nullptr)
		return seats;
	for (int slot = 0; slot < session::setup::MaxSlots; ++slot)
		if (plan->setup.slots[static_cast<std::size_t>(slot)].Occupied())
		{
			const std::string name = "player" + std::to_string(slot);
			for (const auto &player : players)
				if (player.playerName == name)
					seats.push_back(player);
		}
	return seats;
}
}
