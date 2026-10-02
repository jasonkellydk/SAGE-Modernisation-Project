export module games.generalszh.content.global.challenge_generals;
import std;

export import engine.config.binding.schema;

// ChallengeMode.ini (ChallengeGenerals): the Generals' Challenge's twelve
// personas, one per ChallengeMenu.wnd GeneralPosition button, and the
// medallions their PlayerTemplate.ini factions show them with.
export namespace generalszh::content
{
inline constexpr std::size_t ChallengeGeneralCount = 12; // NUM_GENERALS (ChallengeMenu.wnd dependent)

struct GeneralPersona
{
	bool startsEnabled{false};
	std::string playerTemplate; // FactionAmericaAirForceGeneral
	std::string campaign;       // CHALLENGE_0
	// Localized labels.
	std::string bioName, bioDOB, bioBirthplace, bioStrategy, bioRank, bioBranch, bioClassNumber;
	std::string defeatedString, victoriousString;
	// Mapped images.
	std::string bioPortraitSmall, bioPortraitLarge, defeatedImage, victoriousImage;
	std::string portraitMovieLeft, portraitMovieRight;
	// Audio events.
	std::string selectionSound, previewSound, nameSound, winSound, lossSound;
	std::array<std::string, 3> tauntSounds;
	// The faction's medallions (PlayerTemplate MedallionRegular / MedallionHilite / MedallionSelect).
	std::string medallionNormal, medallionHilite, medallionSelected;
};

using ChallengeGenerals = std::array<GeneralPersona, ChallengeGeneralCount>;

// `templates`: PlayerTemplate.ini, for the medallions (the last definition of a faction wins).
inline ChallengeGenerals BindChallengeGenerals(const engine::config::Document &challenge, const engine::config::Document *templates,
	engine::config::BindContext &context)
{
	using namespace engine::config;
	ChallengeGenerals generals;
	for (const Node &root : challenge.Roots())
	{
		if (root.key != "ChallengeGenerals")
			continue;
		for (const Node &block : root.children)
		{
			constexpr std::string_view Prefix = "GeneralPersona";
			if (!block.block || !block.key.starts_with(Prefix))
				continue;
			std::size_t index = 0;
			bool number = block.key.size() > Prefix.size();
			for (const char c : block.key.substr(Prefix.size()))
				if (c >= '0' && c <= '9')
					index = index * 10 + static_cast<std::size_t>(c - '0');
				else
					number = false;
			if (!number || index >= ChallengeGeneralCount)
				continue;
			GeneralPersona &persona = generals[index]; // INI_LOAD_OVERWRITE: later fields replace earlier ones
			for (const Node &field : block.children)
			{
				const std::string_view key = field.key;
				if (key == "StartsEnabled")
					persona.startsEnabled = ReadBool(field, context).value_or(false);
				else
				{
					std::string *text = key == "PlayerTemplate" ? &persona.playerTemplate
						: key == "Campaign" ? &persona.campaign
						: key == "BioNameString" ? &persona.bioName
						: key == "BioDOBString" ? &persona.bioDOB
						: key == "BioBirthplaceString" ? &persona.bioBirthplace
						: key == "BioStrategyString" ? &persona.bioStrategy
						: key == "BioRankString" ? &persona.bioRank
						: key == "BioBranchString" ? &persona.bioBranch
						: key == "BioClassNumberString" ? &persona.bioClassNumber
						: key == "BioPortraitSmall" ? &persona.bioPortraitSmall
						: key == "BioPortraitLarge" ? &persona.bioPortraitLarge
						: key == "PortraitMovieLeftName" ? &persona.portraitMovieLeft
						: key == "PortraitMovieRightName" ? &persona.portraitMovieRight
						: key == "DefeatedImage" ? &persona.defeatedImage
						: key == "VictoriousImage" ? &persona.victoriousImage
						: key == "DefeatedString" ? &persona.defeatedString
						: key == "VictoriousString" ? &persona.victoriousString
						: key == "SelectionSound" ? &persona.selectionSound
						: key == "PreviewSound" ? &persona.previewSound
						: key == "NameSound" ? &persona.nameSound
						: key == "WinSound" ? &persona.winSound
						: key == "LossSound" ? &persona.lossSound
						: key == "TauntSound1" ? &persona.tauntSounds[0]
						: key == "TauntSound2" ? &persona.tauntSounds[1]
						: key == "TauntSound3" ? &persona.tauntSounds[2]
						: nullptr;
					if (text != nullptr)
						*text = ReadText(field);
				}
			}
		}
	}
	if (templates != nullptr)
		for (const Node &root : templates->Roots())
		{
			if (root.key != "PlayerTemplate")
				continue;
			const std::string_view name = root.values.empty() ? std::string_view(root.text) : std::string_view(root.values.front());
			for (GeneralPersona &persona : generals)
			{
				if (persona.playerTemplate.empty() || persona.playerTemplate != name)
					continue;
				for (const Node &field : root.children)
					if (field.key == "MedallionRegular")
						persona.medallionNormal = ReadText(field);
					else if (field.key == "MedallionHilite")
						persona.medallionHilite = ReadText(field);
					else if (field.key == "MedallionSelect")
						persona.medallionSelected = ReadText(field);
			}
		}
	return generals;
}

namespace detail
{
inline bool SameIgnoringCase(std::string_view a, std::string_view b) noexcept
{
	const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
	return a.size() == b.size() && std::ranges::equal(a, b, [&](char x, char y) { return lower(x) == lower(y); });
}
}

// ChallengeGenerals::getGeneralByGeneralName: the first persona whose BioNameString is `name` (a mission's GeneralName),
// ignoring case; none: null.
inline const GeneralPersona *GeneralByGeneralName(const ChallengeGenerals &generals, std::string_view name) noexcept
{
	for (const GeneralPersona &persona : generals)
		if (detail::SameIgnoringCase(persona.bioName, name))
			return &persona;
	return nullptr;
}

// ChallengeGenerals::getPlayerGeneralByCampaignName: the first persona whose Campaign is `name`, ignoring case; none: null.
inline const GeneralPersona *PlayerGeneralByCampaignName(const ChallengeGenerals &generals, std::string_view name) noexcept
{
	for (const GeneralPersona &persona : generals)
		if (detail::SameIgnoringCase(persona.campaign, name))
			return &persona;
	return nullptr;
}

// GeneralPersona::getRandomTauntSound: rand() % 3 picks TauntSound1 (0), TauntSound2 (1), else TauntSound3.
inline const std::string &TauntSoundFor(const GeneralPersona &persona, int random) noexcept
{
	switch (random % 3)
	{
	case 0: return persona.tauntSounds[0];
	case 1: return persona.tauntSounds[1];
	}
	return persona.tauntSounds[2];
}
}
