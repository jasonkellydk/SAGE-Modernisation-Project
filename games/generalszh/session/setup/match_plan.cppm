export module games.generalszh.session.setup.match_plan;
import std;

export import games.generalszh.session.setup.game_setup;
export import engine.core.serialization.byte_stream;

// What a match was started from, enough to make its level again (a saved game's, and a restart's: GameLogic's
// m_gameMode, TheGlobalData->m_mapName, TheSkirmishGameInfo, TheCampaignManager's campaign and mission, the difficulty
// and TheChallengeGenerals' player template): a skirmish from its setup, or a campaign's or Generals' Challenge's
// mission; or a LAN game from its setup, this machine's slot and its host (never saved: GAME_LAN has no Save).
// Serialized for save files.
export namespace generalszh::session::setup
{
enum class MatchKind : std::uint8_t
{
	Skirmish,
	Campaign,
	Challenge,
	Lan,
};

struct MatchPlan
{
	MatchKind kind{MatchKind::Skirmish};
	GameSetup setup;            // a skirmish's (its map and seed with it)
	std::string campaign;       // a campaign's or challenge's
	std::string mission;        // its mission (lowercased)
	std::uint8_t difficulty{1}; // 0 easy, 1 normal, 2 hard
	std::string playerTemplate; // a challenge's general
	// A campaign's or challenge's rank points carried from the mission before (CampaignManager::m_currentRankPoints: the
	// local player's skill points as the last mission ended; 0 as a campaign starts).
	std::int32_t rankPoints{0};
	int localSlot{0};            // a LAN game's: this machine's slot
	std::uint32_t hostAddress{0}; // a LAN game's host (not this machine's: `hosting` false)
	bool hosting{false};
	// The scenery it was started with (ScenerySetup: trees on, fluff forced to props), once started: a replay places
	// the map's objects as its game did, whatever this machine's detail now.
	bool sceneryKnown{false};
	bool useTrees{true};
	bool forceFluffToProp{false};

	bool SinglePlayer() const noexcept { return kind == MatchKind::Campaign || kind == MatchKind::Challenge; }
	// Made from a setup (GAME_SKIRMISH, GAME_LAN).
	bool FromSetup() const noexcept { return kind == MatchKind::Skirmish || kind == MatchKind::Lan; }
};

inline void WriteMatchPlan(engine::core::serialization::ByteWriter &writer, const MatchPlan &plan)
{
	writer.U8(static_cast<std::uint8_t>(plan.kind));
	const GameSetup &setup = plan.setup;
	writer.Text(setup.map);
	writer.U32(setup.mapCrc);
	writer.U32(setup.mapSize);
	writer.I64(setup.mapContentsMask);
	writer.I64(setup.seed);
	writer.I64(setup.crcInterval);
	writer.Flag(setup.useStats);
	writer.U32(setup.superweaponRestriction);
	writer.U32(setup.startingCash);
	writer.Flag(setup.oldFactionsOnly);
	for (const GameSlot &slot : setup.slots)
	{
		writer.U8(static_cast<std::uint8_t>(slot.state));
		for (const char16_t c : slot.name)
		{
			writer.U8(static_cast<std::uint8_t>(c & 0xFF));
			writer.U8(static_cast<std::uint8_t>(c >> 8));
		}
		writer.U8(0);
		writer.U8(0);
		writer.Flag(slot.accepted);
		writer.Flag(slot.hasMap);
		writer.I64(slot.color);
		writer.I64(slot.startPos);
		writer.I64(slot.playerTemplate);
		writer.I64(slot.team);
		writer.U32(slot.ip);
		writer.U32(slot.port);
		writer.I64(slot.natBehavior);
	}
	writer.Text(plan.campaign);
	writer.Text(plan.mission);
	writer.U8(plan.difficulty);
	writer.Text(plan.playerTemplate);
	writer.I64(plan.rankPoints);
	writer.Flag(plan.sceneryKnown);
	writer.Flag(plan.useTrees);
	writer.Flag(plan.forceFluffToProp);
}

inline std::optional<MatchPlan> ReadMatchPlan(engine::core::serialization::ByteReader &reader)
{
	MatchPlan plan;
	const auto kind = reader.U8();
	if (!kind || *kind > static_cast<std::uint8_t>(MatchKind::Challenge))
		return std::nullopt;
	plan.kind = static_cast<MatchKind>(*kind);
	GameSetup &setup = plan.setup;
	setup.map = reader.Text().value_or("");
	setup.mapCrc = reader.U32().value_or(0);
	setup.mapSize = reader.U32().value_or(0);
	setup.mapContentsMask = static_cast<int>(reader.I64().value_or(0));
	setup.seed = static_cast<std::int32_t>(reader.I64().value_or(0));
	setup.crcInterval = static_cast<int>(reader.I64().value_or(DefaultCrcInterval));
	setup.useStats = reader.Flag().value_or(true);
	setup.superweaponRestriction = static_cast<std::uint16_t>(reader.U32().value_or(0));
	setup.startingCash = reader.U32().value_or(10000);
	setup.oldFactionsOnly = reader.Flag().value_or(false);
	for (GameSlot &slot : setup.slots)
	{
		slot.state = static_cast<SlotState>(reader.U8().value_or(0));
		slot.name.clear();
		for (;;)
		{
			const auto low = reader.U8();
			const auto high = reader.U8();
			if (!low || !high || (*low == 0 && *high == 0))
				break;
			slot.name.push_back(static_cast<char16_t>(*low | (*high << 8)));
		}
		slot.accepted = reader.Flag().value_or(false);
		slot.hasMap = reader.Flag().value_or(true);
		slot.color = static_cast<int>(reader.I64().value_or(Random));
		slot.startPos = static_cast<int>(reader.I64().value_or(Random));
		slot.playerTemplate = static_cast<int>(reader.I64().value_or(Random));
		slot.team = static_cast<int>(reader.I64().value_or(Random));
		slot.ip = reader.U32().value_or(0);
		slot.port = static_cast<std::uint16_t>(reader.U32().value_or(0));
		slot.natBehavior = static_cast<int>(reader.I64().value_or(1));
	}
	plan.campaign = reader.Text().value_or("");
	plan.mission = reader.Text().value_or("");
	plan.difficulty = reader.U8().value_or(1);
	plan.playerTemplate = reader.Text().value_or("");
	plan.rankPoints = static_cast<std::int32_t>(reader.I64().value_or(0));
	plan.sceneryKnown = reader.Flag().value_or(false);
	plan.useTrees = reader.Flag().value_or(true);
	plan.forceFluffToProp = reader.Flag().value_or(false);
	if (reader.Failed() || plan.difficulty > 2)
		return std::nullopt;
	return plan;
}
}
