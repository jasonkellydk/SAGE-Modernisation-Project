export module games.generalszh.content.global.player_templates;
import std;

export import engine.config.binding.schema;

// PlayerTemplate.ini's factions the port uses: each faction's preferred
// colour, which a side takes when its map entry sets none (as the
// original's Player::initFromDict). Sorted by faction name.
export namespace generalszh::content
{
struct FactionColors
{
	std::vector<std::pair<std::string, engine::config::Rgb>> byFaction; // sorted

	std::optional<engine::config::Rgb> Of(std::string_view faction) const noexcept
	{
		for (const auto &[name, color] : byFaction)
			if (name == faction)
				return color;
		return std::nullopt;
	}
};

// A side's colour: its own if the map sets one, else its faction's, else the
// neutral player's white. Red, green and blue in the low 24 bits.
inline std::uint32_t SideColor(std::optional<std::int64_t> own, const FactionColors &factions, std::string_view faction) noexcept
{
	if (own)
		return static_cast<std::uint32_t>(*own) & 0xFFFFFFu;
	if (const auto color = factions.Of(faction))
		return (std::uint32_t{color->r} << 16) | (std::uint32_t{color->g} << 8) | color->b;
	return 0xFFFFFFu;
}

FactionColors BindFactionColors(const engine::config::Document &document, engine::config::BindContext &context)
{
	FactionColors colors;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "PlayerTemplate")
			continue;
		for (const engine::config::Node &field : root.children)
			if (field.key == "PreferredColor")
				if (const auto color = engine::config::ReadRgb(field, context))
				{
					std::string name(root.values.empty() ? root.text : root.values.front());
					auto at = colors.byFaction.begin();
					while (at != colors.byFaction.end() && at->first < name)
						++at;
					if (at != colors.byFaction.end() && at->first == name)
						at->second = *color;
					else
						colors.byFaction.insert(at, {std::move(name), *color});
				}
	}
	return colors;
}

// PlayerTemplate.ini's factions in the original's store order (a game setup names one by its
// index): first definition sets the place, later ones update it.
struct PlayerTemplateInfo
{
	std::string name;             // FactionAmerica
	std::string side;             // America (SIDE:<side> is its name)
	std::string baseSide;         // BaseSide: USA, China or GLA (the score screen's sides)
	std::string scoreScreenImage; // ScoreScreenImage: the single-player score screen's backdrop
	std::string sideIconImage;    // SideIconImage
	std::string beaconName;       // BeaconName: the object its players' beacons are
	std::string startingBuilding; // empty: not a faction a player can pick
	std::string displayName;      // DisplayName: a label (INI:FactionAmerica)
	std::string features;         // Features: the load screen's general features (a label)
	std::string loadScreenMusic;  // LoadScreenMusic: the multiplayer load screen's music
	std::array<std::string, 10> startingUnits; // StartingUnit0..9 (MAX_MP_STARTING_UNITS)
	bool playable{false};
	bool oldFaction{false};
	bool observer{false};
	bool startsLocked{false};     // ChallengeGenerals: a general that starts disabled
	std::vector<std::string> intrinsicSciences; // IntrinsicSciences: what its side knows from the start
	std::int32_t intrinsicSciencePurchasePoints{0}; // IntrinsicSciencePurchasePoints
	std::int64_t startMoney{0};   // StartMoney (Player::init: 0 takes the game's starting cash)
	// The General's Powers screen's buttons by rank (PurchaseScienceCommandSetRank1 / 3 / 8).
	std::array<std::string, 3> purchaseScienceCommandSets{};
	// The general's powers shortcut bar: its command set, its layout (a Window/ file) and how many of its buttons it uses
	// (SpecialPowerShortcutCommandSet / WinName / ButtonCount).
	std::string specialPowerShortcutCommandSet;
	std::string specialPowerShortcutWinName;
	std::int32_t specialPowerShortcutButtonCount{0};
};

struct PlayerTemplates
{
	std::vector<PlayerTemplateInfo> templates;

	const PlayerTemplateInfo *At(int index) const noexcept
	{
		return index >= 0 && static_cast<std::size_t>(index) < templates.size() ? &templates[static_cast<std::size_t>(index)] : nullptr;
	}
};

// `challenge`: ChallengeMode.ini (its GeneralPersonas' PlayerTemplate and StartsEnabled).
inline PlayerTemplates BindPlayerTemplates(const engine::config::Document &document, const engine::config::Document *challenge,
	engine::config::BindContext &context)
{
	using namespace engine::config;
	PlayerTemplates store;
	for (const Node &root : document.Roots())
	{
		if (root.key != "PlayerTemplate")
			continue;
		const std::string name(root.values.empty() ? root.text : root.values.front());
		PlayerTemplateInfo *info = nullptr;
		for (PlayerTemplateInfo &existing : store.templates)
			if (existing.name == name)
				info = &existing;
		if (info == nullptr)
		{
			store.templates.push_back({});
			info = &store.templates.back();
			info->name = name;
		}
		for (const Node &field : root.children)
			if (field.key == "Side")
				info->side = ReadText(field);
			else if (field.key == "StartMoney")
				info->startMoney = engine::config::values::ParseInt(field.Value()).value_or(0);
			else if (field.key == "BaseSide")
				info->baseSide = ReadText(field);
			else if (field.key == "ScoreScreenImage")
				info->scoreScreenImage = ReadText(field);
			else if (field.key == "SideIconImage")
				info->sideIconImage = ReadText(field);
			else if (field.key == "BeaconName")
				info->beaconName = ReadText(field);
			else if (field.key == "StartingBuilding")
				info->startingBuilding = ReadText(field);
			else if (field.key == "DisplayName")
				info->displayName = ReadText(field);
			else if (field.key == "Features")
				info->features = ReadText(field);
			else if (field.key == "LoadScreenMusic")
				info->loadScreenMusic = ReadText(field);
			else if (field.key.starts_with("StartingUnit") && field.key.size() == 13 && field.key[12] >= '0' && field.key[12] <= '9')
				info->startingUnits[static_cast<std::size_t>(field.key[12] - '0')] = ReadText(field);
			else if (field.key == "PlayableSide")
				info->playable = ReadBool(field, context).value_or(false);
			else if (field.key == "OldFaction")
				info->oldFaction = ReadBool(field, context).value_or(false);
			else if (field.key == "IntrinsicSciences")
			{
				info->intrinsicSciences.clear();
				for (const std::string_view science : field.values)
					if (science != "None")
						info->intrinsicSciences.emplace_back(science);
			}
			else if (field.key == "IntrinsicSciencePurchasePoints")
				info->intrinsicSciencePurchasePoints = static_cast<std::int32_t>(engine::config::ReadInt(field, context).value_or(0));
			else if (field.key == "PurchaseScienceCommandSetRank1")
				info->purchaseScienceCommandSets[0] = ReadText(field);
			else if (field.key == "PurchaseScienceCommandSetRank3")
				info->purchaseScienceCommandSets[1] = ReadText(field);
			else if (field.key == "PurchaseScienceCommandSetRank8")
				info->purchaseScienceCommandSets[2] = ReadText(field);
			else if (field.key == "SpecialPowerShortcutCommandSet")
				info->specialPowerShortcutCommandSet = ReadText(field);
			else if (field.key == "SpecialPowerShortcutWinName")
				info->specialPowerShortcutWinName = ReadText(field);
			else if (field.key == "SpecialPowerShortcutButtonCount")
				info->specialPowerShortcutButtonCount = static_cast<std::int32_t>(engine::config::ReadInt(field, context).value_or(0));
			else if (field.key == "IsObserver")
				info->observer = ReadBool(field, context).value_or(false);
	}
	if (challenge != nullptr)
		for (const Node &root : challenge->Roots())
			if (root.key == "ChallengeGenerals")
				for (const Node &persona : root.children)
				{
					if (!persona.block)
						continue;
					std::string templateName;
					bool enabled = true;
					for (const Node &field : persona.children)
						if (field.key == "PlayerTemplate")
							templateName = ReadText(field);
						else if (field.key == "StartsEnabled")
							enabled = ReadBool(field, context).value_or(true);
					for (PlayerTemplateInfo &info : store.templates)
						if (info.name == templateName)
							info.startsLocked = !enabled;
				}
	return store;
}
}
