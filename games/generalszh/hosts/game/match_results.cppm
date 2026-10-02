export module games.generalszh.hosts.game.match_results;
import std;

export import games.generalszh.session.session_view;
export import games.generalszh.session.setup.match_plan;
export import games.generalszh.shell.score.battle_honors;
export import games.generalszh.shell.score.score_screen_view_model;
import games.generalszh.hud.match_record;
import games.generalszh.gameplay.academy.resources.academy_stats;
import games.generalszh.presentation.interaction.resources.academy_client_records;

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

// populatePlayerInfo for the local player in a skirmish: its AcademyStats as the game ended (the simulation's record,
// with this machine's own drag selections and double-click attack moves), for calculateAcademyAdvice: now the frame
// the game ended on, its template's base side, the local player's own mines and the neutral player's sniped vehicles,
// drawing from this machine's client random stream. The alternate mouse is the options' (the front end's).
inline std::optional<shell::ScoreAcademy> ReadScoreAcademy(session::SessionView &view, std::uint32_t local, std::string baseSide)
{
	const auto *stats = view.World().FindResource<gameplay::AcademyStats>();
	const gameplay::PlayerAcademy *record = stats != nullptr ? stats->Find(local) : nullptr;
	if (record == nullptr)
		return std::nullopt;
	shell::ScoreAcademy academy;
	shell::AcademyAdviceRecord &advice = academy.record;
	advice.unknownSide = record->unknownSide;
	advice.spentCashBeforeBuildingSupplyCenter = record->spentCashBeforeBuildingSupplyCenter;
	advice.supplyCentersBuilt = record->supplyCentersBuilt;
	advice.researchedRadar = record->researchedRadar;
	advice.peonsBuilt = record->peonsBuilt;
	advice.structuresCaptured = record->structuresCaptured;
	advice.generalsPointsSpent = record->generalsPointsSpent;
	advice.specialPowersUsed = record->specialPowersUsed;
	advice.structuresGarrisoned = record->structuresGarrisoned;
	advice.idleBuildingUnitsMaxFrames = record->idleBuildingUnitsMaxFrames;
	advice.lastUnitBuiltFrame = record->lastUnitBuiltFrame;
	advice.upgradesPurchased = record->upgradesPurchased;
	advice.powerOutMaxFrames = record->powerOutMaxFrames;
	advice.oldestPowerOutFrame = record->oldestPowerOutFrame;
	advice.hadPowerLastCheck = record->hadPowerLastCheck;
	advice.gatherersBuilt = record->gatherersBuilt;
	advice.heroesBuilt = record->heroesBuilt;
	advice.hadAStrategyCenter = record->hadAStrategyCenter;
	advice.choseAStrategyForCenter = record->choseAStrategyForCenter;
	advice.unitsEnteredTunnelNetwork = record->unitsEnteredTunnelNetwork;
	advice.hadATunnelNetwork = record->hadATunnelNetwork;
	advice.controlGroupsUsed = record->controlGroupsUsed;
	advice.secondaryIncomeUnitsBuilt = record->secondaryIncomeUnitsBuilt;
	advice.clearedGarrisonedBuildings = record->clearedGarrisonedBuildings;
	advice.salvageCollected = record->salvageCollected;
	advice.guardAbilityUsedCount = record->guardAbilityUsedCount;
	advice.builtBarracksWithinFiveMinutes = record->builtBarracksWithinFiveMinutes;
	advice.builtWarFactoryWithinTenMinutes = record->builtWarFactoryWithinTenMinutes;
	advice.builtTechStructureWithinFifteenMinutes = record->builtTechStructureWithinFifteenMinutes;
	advice.lastIncomeFrame = record->lastIncomeFrame;
	advice.maxFramesBetweenIncome = record->maxFramesBetweenIncome;
	advice.minesCleared = record->minesCleared;
	advice.vehiclesRecovered = record->vehiclesRecovered;
	advice.disguisableVehiclesBuilt = record->disguisableVehiclesBuilt;
	advice.vehiclesDisguised = record->vehiclesDisguised;
	advice.firestormsCreated = record->firestormsCreated;
	if (const auto *client = view.World().FindResource<presentation::AcademyClientRecords>())
	{
		advice.dragSelectUnits = client->dragSelections;
		advice.doubleClickAttackMoveOrdersGiven = client->doubleClickAttackMoves;
	}
	academy.context.now = static_cast<std::uint32_t>(view.CurrentTick());
	academy.context.baseSide = std::move(baseSide);
	academy.context.localPlayerMines = record->mines;
	const std::vector<session::PlayerScore> scores = view.Scores();
	for (std::uint32_t player = 0; player < scores.size(); ++player)
		if (scores[player].name.empty()) // ThePlayerList->getNeutralPlayer()
		{
			if (const gameplay::PlayerAcademy *neutral = stats->Find(player))
				academy.context.neutralVehiclesSniped = neutral->vehiclesSniped;
			break;
		}
	// theGameClientSeed: this machine's own stream (seeded from the clock, as InitRandom's client seed).
	auto stream = std::make_shared<std::minstd_rand>(static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
	academy.draw = [stream] { return static_cast<std::uint32_t>((*stream)()); };
	return academy;
}
}
