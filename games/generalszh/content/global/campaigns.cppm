export module games.generalszh.content.global.campaigns;
import std;

export import engine.config.binding.schema;

// Campaign.ini (CampaignManager): each campaign's missions in the order read, the first one, and how each leads to the
// next. Campaign and mission names are kept lowercased, as the original stores them (newCampaign / newMission); a
// campaign or mission defined again replaces the earlier one (and goes to the end of the list).
export namespace generalszh::content
{
struct Mission
{
	std::string name; // lowercased
	std::string map;  // Maps\...\X.map
	std::string nextMission;
	std::string introMovie;
	std::array<std::string, 5> objectiveLines;
	std::string briefingVoice;
	std::array<std::string, 3> unitNames;
	std::string generalName;
	std::string locationNameLabel;
	std::int32_t voiceLength{0};
};

struct Campaign
{
	std::string name; // lowercased
	std::string firstMission;
	std::string nameLabel;
	std::string finalVictoryMovie;
	bool challenge{false};
	std::string playerFaction;
	std::vector<Mission> missions;

	// Campaign::getMission: the mission of that name as given (not lowercased; none: null).
	const Mission *FindMission(std::string_view mission) const noexcept
	{
		for (const Mission &entry : missions)
			if (!mission.empty() && entry.name == mission)
				return &entry;
		return nullptr;
	}
	// Campaign::getNextMission: the first mission (no `current`) or the one `current` names next, lowercased; none
	// (the end of the campaign, or not found): null.
	const Mission *NextMission(const Mission *current) const
	{
		std::string next = current == nullptr ? firstMission : current->nextMission;
		std::ranges::transform(next, next.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (next.empty())
			return nullptr;
		return FindMission(next);
	}
	// CampaignManager::getCurrentMissionNumber: 0-based, in the order read (not found: -1).
	std::int32_t MissionNumber(const Mission *mission) const noexcept
	{
		for (std::size_t index = 0; index < missions.size(); ++index)
			if (&missions[index] == mission)
				return static_cast<std::int32_t>(index);
		return -1;
	}
};

struct CampaignCatalog
{
	std::vector<Campaign> campaigns;

	// setCampaign / setCampaignAndMission: by name, lowercased.
	const Campaign *Find(std::string_view name) const
	{
		std::string lowered(name);
		std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		for (const Campaign &campaign : campaigns)
			if (campaign.name == lowered)
				return &campaign;
		return nullptr;
	}
};

inline CampaignCatalog BindCampaigns(const engine::config::Document &document, engine::config::BindContext &context)
{
	using namespace engine::config;
	const auto lower = [](std::string_view text) {
		std::string out(text);
		std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	};
	const auto nameOf = [](const Node &node) { return node.values.empty() ? std::string(node.text) : std::string(node.values.front()); };
	CampaignCatalog catalog;
	for (const Node &root : document.Roots())
	{
		if (root.key != "Campaign")
			continue;
		const std::string name = lower(nameOf(root));
		std::erase_if(catalog.campaigns, [&](const Campaign &campaign) { return campaign.name == name; });
		Campaign &campaign = catalog.campaigns.emplace_back();
		campaign.name = name;
		for (const Node &field : root.children)
		{
			const std::string_view key = field.key;
			if (key == "Mission")
			{
				const std::string missionName = lower(nameOf(field));
				std::erase_if(campaign.missions, [&](const Mission &mission) { return mission.name == missionName; });
				Mission &mission = campaign.missions.emplace_back();
				mission.name = missionName;
				for (const Node &part : field.children)
				{
					const std::string_view partKey = part.key;
					if (partKey == "VoiceLength")
					{
						mission.voiceLength = static_cast<std::int32_t>(ReadInt(part, context).value_or(0));
						continue;
					}
					std::string *text = partKey == "Map" ? &mission.map
						: partKey == "NextMission" ? &mission.nextMission
						: partKey == "IntroMovie" ? &mission.introMovie
						: partKey == "BriefingVoice" ? &mission.briefingVoice
						: partKey == "GeneralName" ? &mission.generalName
						: partKey == "LocationNameLabel" ? &mission.locationNameLabel
						: nullptr;
					for (std::size_t line = 0; line < mission.objectiveLines.size() && text == nullptr; ++line)
						if (partKey == "ObjectiveLine" + std::to_string(line))
							text = &mission.objectiveLines[line];
					for (std::size_t unit = 0; unit < mission.unitNames.size() && text == nullptr; ++unit)
						if (partKey == "UnitNames" + std::to_string(unit))
							text = &mission.unitNames[unit];
					if (text != nullptr)
						*text = ReadText(part);
				}
			}
			else if (key == "IsChallengeCampaign")
				campaign.challenge = ReadBool(field, context).value_or(false);
			else
			{
				std::string *text = key == "FirstMission" ? &campaign.firstMission
					: key == "CampaignNameLabel" ? &campaign.nameLabel
					: key == "FinalVictoryMovie" ? &campaign.finalVictoryMovie
					: key == "PlayerFaction" ? &campaign.playerFaction
					: nullptr;
				if (text != nullptr)
					*text = ReadText(field);
			}
		}
	}
	return catalog;
}
}
