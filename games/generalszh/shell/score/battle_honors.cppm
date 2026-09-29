export module games.generalszh.shell.score.battle_honors;
import std;

export import engine.config.adapters.preferences.preferences_file;

// The local player's record in SkirmishStats.ini (the original's SkirmishBattleHonors over UserPreferences), as the
// score screen writes it after a game:
// - A skirmish (ScoreScreen populatePlayerInfo, SCORESCREEN_SKIRMISH, the local player): not recorded while the local
//   player is still in a game that is a sandbox (every other seat on its team) or that neither its alliance won nor
//   lost (quit early); else a win (Wins, WinStreak, BestWinStreak, then updateSkirmishBattleHonors and
//   updateChallengeMedals) or a loss (Losses, the streak broken); then LastHouse and LoyalGames.
// - A campaign or challenge won to its end (finishSinglePlayerInit): <SIDE>Campaign_<difficulty> or
//   ChallengeCampaign<n>_<difficulty>, and its honour bit.
export namespace generalszh::shell
{
namespace battle_honor
{
// BattleHonors.h.
inline constexpr std::uint32_t Streak = 0x2, LoyaltyUsa = 0x20, LoyaltyChina = 0x40, BattleTank = 0x80, AirWing = 0x100, LoyaltyGla = 0x200,
							   CampaignUsa = 0x800, CampaignChina = 0x1000, CampaignGla = 0x2000, Blitz5 = 0x4000, Blitz10 = 0x8000,
							   Apocalypse = 0x20000, ChallengeMode = 0x100000;
inline constexpr int GeneralTypes = 9; // MAX_GLOBAL_GENERAL_TYPES
}

// A seat other than the local player's: a computer's level (SLOT_EASY_AI 2, SLOT_MED_AI 3, SLOT_BRUTAL_AI 4; a
// human: 0) and whether it is on the local player's team (isSlotLocalAlly).
struct RecordSeat
{
	std::uint8_t aiLevel{0};
	bool ally{false};
	bool occupied{true};
};

// What the score screen knows of the game just played, for the local player.
struct SkirmishGameRecord
{
	bool won{false};    // isLocalAlliedVictory
	bool lost{false};   // isLocalAlliedDefeat
	bool active{true};  // isPlayerActive: not defeated
	std::string side;   // its PlayerTemplate's side (America, China, GLA, ...)
	std::string map;    // TheGameInfo->getMap()
	std::uint64_t ticks{0}; // TheGameLogic->getFrame()
	bool builtScud{false}, builtParticleCannon{false}, builtNuke{false};
	std::int64_t vehiclesBuilt{0}; // VEHICLE and not AIRCRAFT (getTotalUnitsBuilt)
	std::int64_t aircraftBuilt{0};
	std::vector<RecordSeat> others; // every other seat
};

namespace battle_honors_detail
{
inline std::int64_t Get(const engine::config::Preferences &stats, const std::string &key) { return stats.Number(key, 0); }
inline void Put(engine::config::Preferences &stats, const std::string &key, std::int64_t value) { stats.Set(key, std::to_string(value)); }
inline void Honor(engine::config::Preferences &stats, std::uint32_t which)
{
	Put(stats, "Honors", static_cast<std::int64_t>(static_cast<std::uint32_t>(Get(stats, "Honors")) | which));
}
// UserPreferences::setBool / getBool.
inline void SetFlag(engine::config::Preferences &stats, const std::string &key) { stats.SetBool(key, true); }
inline bool Flag(const engine::config::Preferences &stats, const std::string &key) { return stats.Flag(key, false); }

// updateSkirmishBattleHonors.
inline void UpdateHonors(engine::config::Preferences &stats, const SkirmishGameRecord &game)
{
	namespace bh = battle_honor;
	if (Get(stats, "WinStreak") >= 5)
		Honor(stats, bh::Streak);
	if (game.builtScud)
		SetFlag(stats, "SCUD");
	if (game.builtParticleCannon)
		SetFlag(stats, "PPC");
	if (game.builtNuke)
		SetFlag(stats, "Nuke");
	if (Flag(stats, "Nuke") && Flag(stats, "PPC") && Flag(stats, "SCUD"))
		Honor(stats, bh::Apocalypse);
	if (game.vehiclesBuilt >= 50)
		Honor(stats, bh::BattleTank);
	if (game.aircraftBuilt >= 20)
		Honor(stats, bh::AirWing);
	const std::uint64_t minutes = game.ticks / 30 / 60;
	if (minutes < 5)
		Honor(stats, bh::Blitz5);
	if (minutes < 10)
		Honor(stats, bh::Blitz10);
	const bool loyal = Get(stats, "LoyalGames") >= 20;
	if (loyal && game.side == "America")
		Honor(stats, bh::LoyaltyUsa);
	if (loyal && game.side == "China")
		Honor(stats, bh::LoyaltyChina);
	if (loyal && game.side == "GLA")
		Honor(stats, bh::LoyaltyGla);
	// Endurance medals: the most enemy computers beaten on the map at each level (a harder one counts for easier levels).
	std::int64_t easy = 0, medium = 0, brutal = 0;
	for (const RecordSeat &seat : game.others)
		if (seat.occupied && seat.aiLevel != 0 && !seat.ally)
		{
			easy += seat.aiLevel == 2;
			medium += seat.aiLevel == 3;
			brutal += seat.aiLevel == 4;
		}
	if (easy != 0 || medium != 0 || brutal != 0)
	{
		const std::string key = game.map + "_";
		const std::int64_t oldEasy = Get(stats, key + "2"), oldMedium = Get(stats, key + "3"), oldBrutal = Get(stats, key + "4");
		if (easy != 0)
			Put(stats, key + "2", std::max(oldEasy, easy + medium + brutal));
		if (medium != 0)
			Put(stats, key + "3", std::max(oldMedium, medium + brutal));
		if (brutal != 0)
			Put(stats, key + "4", std::max(oldBrutal, brutal));
	}
}

// updateChallengeMedals: with no computer on the local player's side, a medal for beating that many brutal computers.
inline void UpdateChallengeMedals(engine::config::Preferences &stats, const SkirmishGameRecord &game)
{
	int computers = 0, brutal = 0;
	for (const RecordSeat &seat : game.others)
	{
		if (!seat.occupied || seat.aiLevel == 0)
			continue;
		if (seat.ally)
			return;
		++computers;
		brutal += seat.aiLevel == 4;
	}
	if (computers != 0 && brutal >= 1 && brutal <= 7)
		Put(stats, "Challenge", Get(stats, "Challenge") | (std::int64_t{1} << (brutal - 1)));
}
}

// The skirmish's record, as populatePlayerInfo keeps it. False when nothing was recorded.
inline bool RecordSkirmishGame(engine::config::Preferences &stats, const SkirmishGameRecord &game, bool sandbox)
{
	using namespace battle_honors_detail;
	if ((sandbox || !(game.lost || game.won)) && game.active)
		return false;
	if (game.won)
	{
		Put(stats, "Wins", Get(stats, "Wins") + 1);
		Put(stats, "WinStreak", Get(stats, "WinStreak") + 1);
		Put(stats, "BestWinStreak", std::max(Get(stats, "BestWinStreak"), Get(stats, "WinStreak")));
		UpdateHonors(stats, game);
		UpdateChallengeMedals(stats, game);
	}
	else
	{
		Put(stats, "Losses", Get(stats, "Losses") + 1);
		Put(stats, "WinStreak", 0);
	}
	const std::string last(stats.Find("LastHouse").value_or(""));
	stats.Set("LastHouse", game.side);
	Put(stats, "LoyalGames", last != game.side ? 0 : Get(stats, "LoyalGames") + 1);
	return true;
}

// finishSinglePlayerInit, the campaign over: its completion at `difficulty` (0 easy, 1 normal, 2 hard) and honour. The
// campaign's name (case-insensitive): USA, China, GLA or CHALLENGE_<n>; others record nothing.
inline void RecordCampaignComplete(engine::config::Preferences &stats, std::string_view campaign, int difficulty)
{
	using namespace battle_honors_detail;
	namespace bh = battle_honor;
	std::string name(campaign);
	for (char &c : name)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	const std::string level = std::to_string(difficulty);
	if (name == "USA")
	{
		Put(stats, "USACampaign_" + level, 1);
		Honor(stats, bh::CampaignUsa);
	}
	if (name == "CHINA")
	{
		Put(stats, "CHINACampaign_" + level, 1);
		Honor(stats, bh::CampaignChina);
	}
	if (name == "GLA")
	{
		Put(stats, "GLACampaign_" + level, 1);
		Honor(stats, bh::CampaignGla);
	}
	for (int general = 0; general < bh::GeneralTypes; ++general)
		if (name == "CHALLENGE_" + std::to_string(general))
		{
			Put(stats, "ChallengeCampaign" + std::to_string(general) + "_" + level, 1);
			Honor(stats, bh::ChallengeMode);
		}
}
}
