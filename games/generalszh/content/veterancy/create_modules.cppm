export module games.generalszh.content.veterancy.create_modules;
import std;

export import games.generalszh.content.objects.object_definition;

// Create modules that set an object up as it is made:
//   VeterancyGainCreate: at least StartingLevel (VETERAN ...) when its
//   player has ScienceRequired (or none is required);
//   LockWeaponCreate: its SlotToLock (PRIMARY ...) locked in hand for good;
//   GrantUpgradeCreate: its UpgradeToGrant given it (an object upgrade) or its
//   player (a player upgrade) as it is made complete.
export namespace generalszh::content
{
struct VeterancyGain
{
	std::uint32_t level{0}; // REGULAR 0 .. HEROIC 3
	std::string science;    // empty: none required
};

struct CreateModules
{
	std::vector<VeterancyGain> veterancyGains;
	std::optional<std::uint8_t> lockedSlot;
	std::vector<std::string> grantedUpgrades;
};

inline CreateModules ReadCreateModules(const ObjectDefinition &object)
{
	CreateModules result;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr)
			continue;
		if (module.type == "VeterancyGainCreate")
		{
			VeterancyGain gain;
			if (const auto *level = module.block->Find("StartingLevel"))
			{
				constexpr std::array<std::string_view, 4> levels{"REGULAR", "VETERAN", "ELITE", "HEROIC"};
				for (std::uint32_t index = 0; index < levels.size(); ++index)
					if (level->Value() == levels[index])
						gain.level = index;
			}
			if (const auto *science = module.block->Find("ScienceRequired"); science != nullptr && science->Value() != "None" &&
				science->Value() != "SCIENCE_INVALID")
				gain.science = std::string(science->Value());
			result.veterancyGains.push_back(std::move(gain));
		}
		else if (module.type == "GrantUpgradeCreate")
		{
			if (const auto *grant = module.block->Find("UpgradeToGrant"))
				result.grantedUpgrades.emplace_back(grant->Value());
		}
		else if (module.type == "LockWeaponCreate")
		{
			const auto *slot = module.block->Find("SlotToLock");
			const std::string_view name = slot != nullptr ? slot->Value() : std::string_view("PRIMARY");
			result.lockedSlot = static_cast<std::uint8_t>(name == "SECONDARY" ? 1 : name == "TERTIARY" ? 2 : 0);
		}
	}
	return result;
}
}
