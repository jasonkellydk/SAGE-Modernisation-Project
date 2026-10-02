export module games.generalszh.shell.skirmish.skirmish_tooltips;
import std;

export import games.generalszh.shell.skirmish.skirmish_view_model;

// The skirmish screens' tooltip callbacks (winSetTooltipFunc): what each would set as the mouse's tooltip
// (Mouse::setCursorTooltip): a string label to fetch, its delay (-1: the mouse's own) and width share; or nothing set
// (the tooltip stays cleared). Labels only: the host fetches them (TheGameText).
export namespace generalszh::shell
{
struct TooltipCall
{
	std::string label;
	int delay{-1};
	float width{1.0f};
	std::vector<std::u16string> arguments; // the fetched text's %s arguments in order (UnicodeString::format)
	bool operator==(const TooltipCall &) const = default;
};

// LobbyUtils.cpp playerTemplateComboBoxTooltip: the faction box's chosen row's ArmyTooltip ("Random", the first row:
// TOOLTIP:BioStrategyLong_Random); an unknown template: an empty tooltip. playerTemplateListBoxTooltip: its open list's
// row under the pointer the same, without delay (0); no row: nothing set.
inline TooltipCall FactionTooltip(const SetupCatalog &catalog, int row)
{
	if (row <= 0)
		return {"TOOLTIP:BioStrategyLong_Random"};
	const auto index = static_cast<std::size_t>(row - 1);
	return {index < catalog.factions.size() ? catalog.factions[index].armyTooltip : std::string{}};
}

inline std::optional<TooltipCall> FactionListTooltip(const SetupCatalog &catalog, int row)
{
	if (row < 0)
		return std::nullopt;
	TooltipCall call = FactionTooltip(catalog, row);
	call.delay = 0;
	return call;
}

// SkirmishMapSelectMenu.cpp mapListTooltipFunc: over a cell of the map list, the row's medal column item data
// (populateMapListboxNoReset: 0 none .. 4 every brutal opponent beaten) as TOOLTIP:MapNoSuccess / MapEasySuccess /
// MapMediumSuccess / MapHardSuccess / MapMaxBrutalSuccess; any other (not a multiplayer map), and off any cell, an empty
// tooltip.
inline TooltipCall MapListTooltip(const std::vector<int> &levels, int row, int column)
{
	if (row < 0 || column < 0 || static_cast<std::size_t>(row) >= levels.size())
		return {};
	static constexpr std::array<std::string_view, 5> Labels{"TOOLTIP:MapNoSuccess", "TOOLTIP:MapEasySuccess", "TOOLTIP:MapMediumSuccess",
		"TOOLTIP:MapHardSuccess", "TOOLTIP:MapMaxBrutalSuccess"};
	const int level = levels[static_cast<std::size_t>(row)];
	return {level >= 0 && level < 5 ? std::string(Labels[static_cast<std::size_t>(level)]) : std::string{}};
}

// The honours list's item data at a cell (InsertBattleHonor): an honour's bit (with BATTLE_HONOR_NOT_GAINED until it
// is gained); a spacer row's cell the extra value of the honour under it (0 for most).
inline std::int64_t BattleHonorItem(const std::vector<std::vector<BattleHonor>> &rows, int row, int column)
{
	if (row < 0 || column < 0 || static_cast<std::size_t>(row) >= rows.size())
		return 0;
	const auto at = static_cast<std::size_t>(column);
	const auto &cells = rows[static_cast<std::size_t>(row)];
	if (!cells.empty())
		return at < cells.size() ? static_cast<std::int64_t>(cells[at].item | (cells[at].gained ? 0u : honor::NotGained)) : 0;
	const auto below = static_cast<std::size_t>(row) + 1;
	return below < rows.size() && at < rows[below].size() ? rows[below][at].extra : 0;
}

// PopupPlayerInfo.cpp BattleHonorTooltip: off any cell, or over one without an honour, TOOLTIP:BattleHonors; over an
// honour, its tooltip at 1.5 the width, the first of its bits that has one in the original's order (not gained: the
// "Disabled" ones; the streak and domination by the count kept with them); a value with none of them sets nothing.
inline std::optional<TooltipCall> BattleHonorTooltip(const std::vector<std::vector<BattleHonor>> &rows, int row, int column)
{
	if (row == -1 || column == -1)
		return TooltipCall{"TOOLTIP:BattleHonors"};
	const auto value = static_cast<std::uint32_t>(BattleHonorItem(rows, row, column));
	const std::int64_t extra = BattleHonorItem(rows, row - 1, column);
	if (value == 0)
		return TooltipCall{"TOOLTIP:BattleHonors"};
	const auto set = [&](std::uint32_t bit) { return (value & bit) != 0; };
	const auto call = [](std::string label) { return std::optional<TooltipCall>{TooltipCall{std::move(label), -1, 1.5f}}; };
	if (set(honor::NotGained))
	{
		static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 20> Disabled{{{honor::LoyaltyUsa, "LoyaltyUSADisabled"},
			{honor::LoyaltyChina, "LoyaltyChinaDisabled"}, {honor::LoyaltyGla, "LoyaltyGLADisabled"}, {honor::BattleTank, "BattleTankDisabled"},
			{honor::AirWing, "AirWingDisabled"}, {honor::Endurance, "EnduranceDisabled"}, {honor::CampaignUsa, "CampaignUSADisabled"},
			{honor::CampaignChina, "CampaignChinaDisabled"}, {honor::CampaignGla, "CampaignGLADisabled"}, {honor::Blitz10, "BlitzDisabled"},
			{honor::FairPlay, "FairPlayDisabled"}, {honor::Apocalypse, "ApocalypseDisabled"}, {honor::ChallengeMode, "CampaignChallengeDisabled"},
			{honor::Ultimate, "UltimateDisabled"}, {honor::GlobalGeneral, "GlobalGeneralDisabled"}, {honor::Challenge, "ChallengeDisabled"},
			{honor::Streak, "StreakDisabled"}, {honor::StreakOnline, "StreakOnlineDisabled"}, {honor::Domination, "DominationDisabled"},
			{honor::DominationOnline, "DominationOnlineDisabled"}}};
		for (const auto &[bit, name] : Disabled)
			if (set(bit))
				return call("TOOLTIP:BattleHonor" + std::string(name));
		return std::nullopt;
	}
	static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 18> Gained{{{honor::LoyaltyUsa, "LoyaltyUSA"},
		{honor::LoyaltyChina, "LoyaltyChina"}, {honor::LoyaltyGla, "LoyaltyGLA"}, {honor::BattleTank, "BattleTank"}, {honor::AirWing, "AirWing"},
		{honor::Endurance, "Endurance"}, {honor::CampaignUsa, "CampaignUSA"}, {honor::CampaignChina, "CampaignChina"},
		{honor::CampaignGla, "CampaignGLA"}, {honor::Blitz5, "Blitz5"}, {honor::Blitz10, "Blitz10"}, {honor::FairPlay, "FairPlay"},
		{honor::Apocalypse, "Apocalypse"}, {honor::OfficersClub, "OfficersClub"}, {honor::ChallengeMode, "CampaignChallenge"},
		{honor::Ultimate, "Ultimate"}, {honor::GlobalGeneral, "GlobalGeneral"}, {honor::Challenge, "Challenge"}}};
	for (const auto &[bit, name] : Gained)
		if (set(bit))
			return call("TOOLTIP:BattleHonor" + std::string(name));
	const auto counted = [&](std::span<const std::pair<std::int64_t, std::string_view>> steps, std::string_view none) {
		for (const auto &[at, name] : steps)
			if (extra >= at)
				return call("TOOLTIP:BattleHonor" + std::string(name));
		return call("TOOLTIP:BattleHonor" + std::string(none));
	};
	if (set(honor::Streak))
	{
		static constexpr std::array<std::pair<std::int64_t, std::string_view>, 6> Steps{
			{{1000, "Streak1000"}, {500, "Streak500"}, {100, "Streak100"}, {25, "Streak25"}, {10, "Streak10"}, {3, "Streak3"}}};
		return counted(Steps, "StreakDisabled");
	}
	if (set(honor::StreakOnline))
	{
		static constexpr std::array<std::pair<std::int64_t, std::string_view>, 6> Steps{{{1000, "Streak1000Online"}, {500, "Streak500Online"},
			{100, "Streak100Online"}, {25, "Streak25Online"}, {10, "Streak10Online"}, {3, "Streak3Online"}}};
		return counted(Steps, "StreakOnlineDisabled");
	}
	if (set(honor::Domination))
	{
		static constexpr std::array<std::pair<std::int64_t, std::string_view>, 4> Steps{
			{{10000, "Domination10000"}, {1000, "Domination1000"}, {500, "Domination500"}, {100, "Domination100"}}};
		return counted(Steps, "DominationDisabled");
	}
	if (set(honor::DominationOnline))
	{
		static constexpr std::array<std::pair<std::int64_t, std::string_view>, 4> Steps{{{10000, "Domination10000Online"},
			{1000, "Domination1000Online"}, {500, "Domination500Online"}, {100, "Domination100Online"}}};
		return counted(Steps, "DominationOnlineDisabled");
	}
	return std::nullopt;
}

// SkirmishGameOptionsMenu.cpp MapSelectorTooltip over the map preview: a tech building's marker under the pointer
// (strictly inside its SUPPLY_TECH_SIZE square) gives TOOLTIP:TechBuilding, else a supply dock's TOOLTIP:SupplyDock;
// else nothing set. The markers as positionAdditionalImages places them in the window: (Int)(share of the map across
// the letterboxed picture - SUPPLY_TECH_SIZE / 2 + the picture's offset), from the window's corner; `size` the marker's
// square in the pointer's units.
inline std::optional<TooltipCall> MapSelectorTooltip(const MapPreview &preview, int windowLeft, int windowTop, int fitLeft, int fitTop, int fitWidth,
	int fitHeight, int size, int x, int y)
{
	const auto over = [&](const MapPoint &point) {
		const int markerX = static_cast<int>(static_cast<float>(point.x) / 10000.0f * static_cast<float>(fitWidth) - static_cast<float>(size / 2) +
			static_cast<float>(fitLeft - windowLeft));
		const int markerY = static_cast<int>(static_cast<float>(point.y) / 10000.0f * static_cast<float>(fitHeight) - static_cast<float>(size / 2) +
			static_cast<float>(fitTop - windowTop));
		return x > windowLeft + markerX && x < windowLeft + markerX + size && y > windowTop + markerY && y < windowTop + markerY + size;
	};
	for (const MapPoint &tech : preview.techs)
		if (over(tech))
			return TooltipCall{"TOOLTIP:TechBuilding"};
	for (const MapPoint &supply : preview.supplies)
		if (over(supply))
			return TooltipCall{"TOOLTIP:SupplyDock"};
	return std::nullopt;
}
}
