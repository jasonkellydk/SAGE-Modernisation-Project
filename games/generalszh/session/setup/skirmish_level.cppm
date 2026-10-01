export module games.generalszh.session.setup.skirmish_level;
import std;

export import games.generalszh.session.setup.game_setup;
export import games.generalszh.content.global.player_templates;
export import games.generalszh.content.global.multiplayer_settings;
export import engine.level.model.level;
export import engine.level.adapters.generals_map.map_reader;
import games.generalszh.session.setup.script_qualify;
import Engine.Core.Math.FixedRandom;

// A map made ready for a skirmish or LAN game from its setup (the original's
// GameLogic::startNewGame with SidesList::prepareForMP_or_Skirmish): the
// map's own sides go, but for its civilians and its neutral side, with their
// teams; each occupied slot becomes a side "player<slot>" (its faction, its
// start spot, whether human, allied to slots on its team and enemy to the
// rest, the setup's starting cash) with a team "teamplayer<slot>". Random
// factions and start spots are settled here from the setup's seed
// (populateRandomSideAndColor / populateRandomStartPosition). Observers do
// not play.
export namespace generalszh::session::setup
{
struct SkirmishPlayer
{
	int slot{0};
	std::string name;     // player<slot>
	std::string team;     // teamplayer<slot>
	int playerTemplate{0};
	int startPosition{0}; // 0-based: Player_<n+1>_Start
	int color{-1};        // its MultiplayerColor (-1: none given, as without `colors`)
	bool human{false};
};

struct SkirmishLevel
{
	engine::level::Level level;
	std::vector<SkirmishPlayer> players;
};

// With `skirmishScripts` (Data/Scripts/SkirmishScripts.scb, used when the map's own skirmish sides have no scripts:
// SidesList::prepareForMP_or_Skirmish), each computer player takes the scripts and teams of the map's skirmish side
// of its side (its faction's), qualified with its start index (Player::initFromDict), and plays at its level of
// difficulty (skirmishDifficulty: easy 0, normal 1, hard 2); its default team is that side's, qualified. The first
// human takes the civilian side's scripts likewise (qualified "0": its start index is not read yet then).
inline SkirmishLevel PrepareSkirmishLevel(const engine::level::Level &map, const GameSetup &setup, const content::PlayerTemplates &templates,
	int mapStartPositions, const engine::level::generals_map::ScriptFile *skirmishScripts = nullptr, const std::vector<content::MultiplayerColor> *colors = nullptr)
{
	SkirmishLevel out;
	out.level = map;
	// GameInfo's superweapon restriction (the setup's "limit superweapons"), for the match's rules.
	out.level.properties.Set("superweaponRestriction", static_cast<std::int64_t>(setup.superweaponRestriction));
	// GameInfo's starting cash, which each player starts with unless its template says otherwise (Player::init).
	out.level.properties.Set("startingCash", static_cast<std::int64_t>(setup.startingCash));
	auto &scenario = out.level.scenario;
	// Keep the neutral side and civilians, with their teams.
	std::vector<engine::level::Participant> kept;
	std::vector<std::string> keptNames;
	for (const auto &participant : map.scenario.participants)
	{
		const std::string name = participant.properties.Get<std::string>("playerName").value_or("");
		const std::string faction = participant.properties.Get<std::string>("playerFaction").value_or("");
		if (name.empty() || faction == "FactionCivilian")
		{
			kept.push_back(participant);
			keptNames.push_back(name);
		}
	}
	scenario.participants = std::move(kept);
	std::vector<engine::level::Properties> groups;
	for (const auto &group : map.scenario.groups)
	{
		const std::string owner = group.Get<std::string>("teamOwner").value_or("");
		for (const std::string &name : keptNames)
			if (name == owner)
			{
				groups.push_back(group);
				break;
			}
	}
	scenario.groups = std::move(groups);

	auto random = Engine::Math::Stream(static_cast<std::uint64_t>(static_cast<std::uint32_t>(setup.seed)), {0x5C1Au});
	std::vector<int> playable;
	for (std::size_t index = 0; index < templates.templates.size(); ++index)
	{
		const auto &info = templates.templates[index];
		if (info.playable && !info.observer && !info.startsLocked && !info.startingBuilding.empty())
			playable.push_back(static_cast<int>(index));
	}
	std::vector<bool> taken(static_cast<std::size_t>(std::max(mapStartPositions, 0)), false);
	for (const GameSlot &slot : setup.slots)
		if (slot.Occupied() && slot.startPos >= 0 && slot.startPos < mapStartPositions)
			taken[static_cast<std::size_t>(slot.startPos)] = true;

	// populateRandomSideAndColor's colours: each occupied slot's (an observer's too) in slot order, a random one drawn
	// again until no slot has it (GameInfo::isColorTaken). Drawn from a stream of their own, so a setup's sides and
	// spots come out as they did before colours were resolved.
	std::array<int, MaxSlots> slotColors{};
	for (int index = 0; index < MaxSlots; ++index)
		slotColors[static_cast<std::size_t>(index)] = setup.slots[static_cast<std::size_t>(index)].color;
	if (colors != nullptr && !colors->empty())
	{
		auto colorRandom = Engine::Math::Stream(static_cast<std::uint64_t>(static_cast<std::uint32_t>(setup.seed)), {0x5C1Au, 0xC0103u});
		const int count = static_cast<int>(colors->size());
		for (int index = 0; index < MaxSlots; ++index)
		{
			int &color = slotColors[static_cast<std::size_t>(index)];
			if (!setup.slots[static_cast<std::size_t>(index)].Occupied() || (color >= 0 && color < count))
				continue;
			const auto taken = [&](int candidate) { return std::ranges::find(slotColors, candidate) != slotColors.end(); };
			color = -1;
			if (std::ranges::all_of(std::views::iota(0, count), taken))
				continue; // every colour taken: the original would draw forever
			while (color == -1)
			{
				const int candidate = static_cast<int>(Engine::Math::UniformInt(colorRandom, 0, count - 1));
				if (!taken(candidate))
					color = candidate;
			}
		}
	}

	for (int index = 0; index < MaxSlots; ++index)
	{
		const GameSlot &slot = setup.slots[static_cast<std::size_t>(index)];
		if (!slot.Occupied() || slot.Observer())
			continue;
		SkirmishPlayer player;
		player.color = colors != nullptr ? slotColors[static_cast<std::size_t>(index)] : -1;
		player.slot = index;
		player.name = "player" + std::to_string(index);
		player.team = "teamplayer" + std::to_string(index);
		player.human = slot.Human();
		player.playerTemplate = slot.playerTemplate;
		if (player.playerTemplate < 0 && !playable.empty())
			player.playerTemplate = playable[static_cast<std::size_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(playable.size()) - 1))];
		player.startPosition = slot.startPos;
		if (player.startPosition < 0 || player.startPosition >= mapStartPositions)
		{
			std::vector<int> free;
			for (int spot = 0; spot < mapStartPositions; ++spot)
				if (!taken[static_cast<std::size_t>(spot)])
					free.push_back(spot);
			player.startPosition = free.empty() ? 0 : free[static_cast<std::size_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(free.size()) - 1))];
			if (!free.empty())
				taken[static_cast<std::size_t>(player.startPosition)] = true;
		}
		out.players.push_back(player);
	}
	// prepareForMP_or_Skirmish: the standard scripts only when none of the map's skirmish sides (civilians aside) has any.
	bool gotScripts = false;
	for (const auto &mapSide : map.scenario.participants)
		if (mapSide.properties.Get<std::string>("playerFaction").value_or("") != "FactionCivilian" &&
			(!mapSide.scripts.scripts.empty() || !mapSide.scripts.groups.empty()))
			gotScripts = true;
	bool civilianScriptsTaken = false;
	for (const SkirmishPlayer &player : out.players)
	{
		const GameSlot &slot = setup.slots[static_cast<std::size_t>(player.slot)];
		std::string allies, enemies;
		for (const SkirmishPlayer &other : out.players)
		{
			if (other.slot == player.slot)
				continue;
			const GameSlot &theirs = setup.slots[static_cast<std::size_t>(other.slot)];
			std::string &list = slot.team == Random || theirs.team != slot.team ? enemies : allies;
			if (!list.empty())
				list += ' ';
			list += other.name;
		}
		engine::level::Participant side;
		side.properties.Set("playerName", player.name);
		side.properties.Set("playerIsHuman", player.human);
		if (const auto *info = templates.At(player.playerTemplate))
			side.properties.Set("playerFaction", info->name);
		side.properties.Set("playerAllies", allies);
		side.properties.Set("playerEnemies", enemies);
		side.properties.Set("multiplayerStartIndex", static_cast<std::int64_t>(player.startPosition));
		// startNewGame: the side's colours, its MultiplayerColor's day and night RGB (Player::initFromDict: | 0xff000000).
		if (colors != nullptr && player.color >= 0 && player.color < static_cast<int>(colors->size()))
		{
			const content::MultiplayerColor &house = (*colors)[static_cast<std::size_t>(player.color)];
			const auto argb = [](const engine::config::Rgb &rgb) {
				return static_cast<std::int64_t>(0xFF000000u | (std::uint32_t{rgb.r} << 16) | (std::uint32_t{rgb.g} << 8) | std::uint32_t{rgb.b});
			};
			side.properties.Set("playerColor", argb(house.day));
			side.properties.Set("playerNightColor", argb(house.night));
		}
		if (slot.AI())
			side.properties.Set("skirmishDifficulty", static_cast<std::int64_t>(slot.state == SlotState::EasyAI ? 0 : slot.state == SlotState::MediumAI ? 1 : 2));
		// Its side's skirmish scripts and teams (the map's skirmish side of the same side).
		bool ownDefaultTeam = true;
		if (gotScripts || skirmishScripts != nullptr)
		{
			const auto *info = templates.At(player.playerTemplate);
			const std::string mySide = player.human ? std::string("Civilian") : info != nullptr ? info->side : std::string{};
			const engine::level::Participant *skirmishSide = nullptr;
			for (const auto &mapSide : map.scenario.participants)
			{
				const std::string faction = mapSide.properties.Get<std::string>("playerFaction").value_or("");
				for (const auto &candidate : templates.templates)
					if (candidate.name == faction && candidate.side == mySide && !mapSide.properties.Get<std::string>("playerName").value_or("").empty())
					{
						skirmishSide = &mapSide;
						break;
					}
				if (skirmishSide != nullptr)
					break;
			}
			const std::string sideName = skirmishSide != nullptr ? skirmishSide->properties.Get<std::string>("playerName").value_or("") : std::string{};
			const bool civilian = player.human;
			// The side's scripts: the map's own when it has skirmish scripts, else the standard file's.
			const engine::level::ScriptList *source = nullptr;
			if (gotScripts)
				source = skirmishSide != nullptr ? &skirmishSide->scripts : nullptr;
			else
				for (std::size_t index = 0; index < skirmishScripts->players.size() && index < skirmishScripts->lists.size(); ++index)
					if (!sideName.empty() && skirmishScripts->players[index] == sideName)
						source = &skirmishScripts->lists[index];
			if (source != nullptr && (!civilian || !civilianScriptsTaken))
			{
				const std::string qualifier = civilian ? std::string("0") : std::to_string(player.startPosition);
				side.scripts = QualifyScripts(*source, qualifier, sideName + qualifier, player.name);
				civilianScriptsTaken = civilianScriptsTaken || civilian;
			}
			if (!civilian && !sideName.empty())
			{
				const std::string qualifier = std::to_string(player.startPosition);
				// The map's teams of that side, then the file's (the skirmish team records, in their order).
				std::vector<const engine::level::Properties *> teams;
				for (const auto &group : map.scenario.groups)
					if (group.Get<std::string>("teamOwner").value_or("") == sideName)
						teams.push_back(&group);
				if (!gotScripts)
					for (const auto &group : skirmishScripts->teams)
						if (group.Get<std::string>("teamOwner").value_or("") == sideName)
							teams.push_back(&group);
				for (const engine::level::Properties *team : teams)
				{
					const std::string name = team->Get<std::string>("teamName").value_or("") + qualifier;
					if (std::any_of(scenario.groups.begin(), scenario.groups.end(),
							[&](const engine::level::Properties &existing) { return existing.Get<std::string>("teamName").value_or("") == name; }))
						continue;
					scenario.groups.push_back(QualifyTeam(*team, qualifier, player.name));
				}
				// Player::setDefaultTeam: "team" + its (qualified) player name.
				const std::string defaultTeam = "team" + sideName + qualifier;
				if (std::any_of(scenario.groups.begin(), scenario.groups.end(),
						[&](const engine::level::Properties &existing) { return existing.Get<std::string>("teamName").value_or("") == defaultTeam; }))
				{
					for (auto &each : out.players)
						if (each.slot == player.slot)
							each.team = defaultTeam;
					ownDefaultTeam = false;
				}
			}
		}
		scenario.participants.push_back(std::move(side));
		if (ownDefaultTeam)
		{
			engine::level::Properties team;
			team.Set("teamName", player.team);
			team.Set("teamOwner", player.name);
			team.Set("teamIsSingleton", true);
			scenario.groups.push_back(std::move(team));
		}
	}
	return out;
}

// A Generals' Challenge mission (GameLogic::startNewGame with TheChallengeGameInfo: one human slot playing the chosen
// general's PlayerTemplate, no computer slots, so no prepareForMP_or_Skirmish): the map's own sides, teams and scripts
// all stay, and the human joins them as side "player0" (human, no allies or enemies of its own: the session gives it
// "ThePlayer"'s) with the starting cash, on a start spot chosen at random (populateRandomStartPosition), and its team
// "teamplayer0".
inline SkirmishLevel PrepareChallengeLevel(const engine::level::Level &map, int playerTemplate, const content::PlayerTemplates &templates,
	int mapStartPositions, std::int32_t seed, std::uint32_t startingCash)
{
	SkirmishLevel out;
	out.level = map;
	// TheChallengeGameInfo's starting cash (Player::init).
	out.level.properties.Set("startingCash", static_cast<std::int64_t>(startingCash));
	auto random = Engine::Math::Stream(static_cast<std::uint64_t>(static_cast<std::uint32_t>(seed)), {0x5C1Au});
	SkirmishPlayer player;
	player.slot = 0;
	player.name = "player0";
	player.team = "teamplayer0";
	player.human = true;
	player.playerTemplate = playerTemplate;
	player.startPosition = mapStartPositions > 0 ? static_cast<int>(Engine::Math::UniformInt(random, 0, mapStartPositions - 1)) : 0;
	out.players.push_back(player);
	engine::level::Participant side;
	side.properties.Set("playerName", player.name);
	side.properties.Set("playerIsHuman", true);
	if (const auto *info = templates.At(playerTemplate))
		side.properties.Set("playerFaction", info->name);
	side.properties.Set("playerAllies", std::string{});
	side.properties.Set("playerEnemies", std::string{});
	side.properties.Set("multiplayerStartIndex", static_cast<std::int64_t>(player.startPosition));
	out.level.scenario.participants.push_back(std::move(side));
	engine::level::Properties team;
	team.Set("teamName", player.team);
	team.Set("teamOwner", player.name);
	team.Set("teamIsSingleton", true);
	out.level.scenario.groups.push_back(std::move(team));
	return out;
}
}
