export module games.generalszh.presentation.hud.resources.in_game_messages;
import std;

import engine.ecs.system.system;

// The messages at the top of the screen (the original's InGameUI m_uiMessages, MAX_UI_MESSAGES 6): newest first, each
// with the tick it came and its colour (RGBA, its alpha fading); the colours new ones alternate between and how many
// ticks one stays before fading (InGameUI.ini MessageColor1 / MessageColor2 / MessageDelayMS). And what the messages
// say about players: their names as shown (a human's own, an AI's GUI:EasyAI / MediumAI / HardAI) and the text for
// one defeated (GUI:PlayerHasBeenDefeated); and about upgrades finished.
export namespace generalszh::presentation
{
struct InGameMessage
{
	std::u16string text;
	std::uint64_t tick{0};
	std::array<std::uint8_t, 4> color{};
	bool shown{false};
};

struct InGameMessages
{
	static constexpr std::size_t Capacity = 6;
	std::array<InGameMessage, Capacity> slots{};
	std::array<std::uint8_t, 4> color1{255, 255, 255, 255};
	std::array<std::uint8_t, 4> color2{180, 180, 180, 255};
	// m_messageDelayMS / LOGICFRAMES_PER_SECOND / 1000, whole numbers as the original divides (75000 ms: 2).
	std::uint64_t timeoutTicks{0};
	std::vector<std::u16string> playerNames; // by player index
	std::u16string defeatedText{u"%ls has been defeated."};
	// An upgrade finished (UPGRADE:UpgradeComplete) and each upgrade's name as shown (its DisplayName), by upgrade index.
	std::u16string upgradeCompleteText{u"Upgrade complete: %ls"};
	std::vector<std::u16string> upgradeNames;
	// An overcharge that ran out (GUI:OverchargeExhausted).
	std::u16string overchargeExhaustedText{u"Overcharge exhausted"};
	// A rally point set (GUI:RallyPointSet, with the factory's name) or not, no path (GUI:RallyPointNoPath), and the
	// objects' names as shown, by their DisplayName label.
	std::u16string rallySetText{u"Rally point set for %ls"};
	std::u16string rallyNoPathText{u"No path to rally point"};
	std::map<std::string, std::u16string, std::less<>> displayNames;
	// Multiplayer beacons: one placed (GUI:BeaconPlaced, with its player's name), too many up (GUI:TooManyBeacons), none
	// to place (GUI:BeaconPlacementFailed).
	std::u16string beaconPlacedText{u"%ls has placed a beacon."};
	std::u16string tooManyBeaconsText{u"Too many beacons"};
	std::u16string beaconFailedText{u"Beacon placement failed"};
	// A builder's structure finished (DOZER:ConstructionComplete, with its name; INI:MissingDisplayName with the template
	// name for one without) or repaired (DOZER:RepairComplete).
	std::u16string constructionCompleteText{u"Construction complete: %ls"};
	std::u16string missingDisplayNameText{u"MISSING: '%hs'"};
	std::u16string repairCompleteText{u"Repair complete"};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::InGameMessages>
{
	static constexpr std::string_view StableName = "generalszh.presentation.in_game_messages";
};
}
