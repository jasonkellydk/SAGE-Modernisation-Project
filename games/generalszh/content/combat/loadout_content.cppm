export module games.generalszh.content.combat.loadout_content;
import std;

export import games.generalszh.content.objects.object_definition;

// An object's WeaponSets and ArmorSets as its loadout: each set's Conditions
// as bits of the original's WeaponSetFlags (VETERAN ... WEAPON_RIDER8) and
// ArmorSetFlags (VETERAN ... CRATE_UPGRADE_TWO), its PRIMARY / SECONDARY /
// TERTIARY weapons and its armor (names; "None" empty). A later set with the
// same conditions replaces an earlier one (the defaults' sets give way to the
// object's own), as the original's set lists.
export namespace generalszh::content
{
inline constexpr std::array<std::string_view, 17> WeaponSetFlagNames{"VETERAN", "ELITE", "HERO", "PLAYER_UPGRADE", "CRATEUPGRADE_ONE",
	"CRATEUPGRADE_TWO", "VEHICLE_HIJACK", "CARBOMB", "MINE_CLEARING_DETAIL", "WEAPON_RIDER1", "WEAPON_RIDER2", "WEAPON_RIDER3", "WEAPON_RIDER4",
	"WEAPON_RIDER5", "WEAPON_RIDER6", "WEAPON_RIDER7", "WEAPON_RIDER8"};
inline constexpr std::array<std::string_view, 8> ArmorSetFlagNames{"VETERAN", "ELITE", "HERO", "PLAYER_UPGRADE", "WEAK_VERSUS_BASEDEFENSES",
	"SECOND_LIFE", "CRATE_UPGRADE_ONE", "CRATE_UPGRADE_TWO"};

template<std::size_t N>
constexpr std::uint32_t SetFlag(const std::array<std::string_view, N> &names, std::string_view name) noexcept
{
	for (std::size_t index = 0; index < N; ++index)
		if (names[index] == name)
			return 1u << index;
	return 0;
}

struct WeaponSetContent
{
	std::uint32_t conditions{0};
	std::array<std::string, 3> weapons;
	bool lockShared{false}; // WeaponLockSharedAcrossSets
};

struct ArmorSetContent
{
	std::uint32_t conditions{0};
	std::string armor;
};

struct ObjectLoadout
{
	std::vector<WeaponSetContent> weaponSets;
	std::vector<ArmorSetContent> armorSets;
};

namespace loadout_detail
{
inline bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y)); });
}

template<std::size_t N>
std::uint32_t Conditions(const engine::config::Node *set, const std::array<std::string_view, N> &names)
{
	std::uint32_t bits = 0;
	if (const engine::config::Node *conditions = set->Find("Conditions"))
		for (const std::string_view name : conditions->values)
			for (std::size_t index = 0; index < N; ++index)
				if (Same(name, names[index]))
					bits |= 1u << index;
	return bits;
}
}

inline ObjectLoadout ReadObjectLoadout(const ObjectDefinition &object)
{
	using namespace loadout_detail;
	ObjectLoadout loadout;
	for (const engine::config::Node *set : object.weaponSets)
	{
		if (set == nullptr)
			continue;
		WeaponSetContent entry;
		entry.conditions = Conditions(set, WeaponSetFlagNames);
		for (const engine::config::Node &child : set->children)
			if (Same(child.key, "Weapon") && child.values.size() >= 2 && !Same(child.Value(1), "None"))
			{
				const std::string_view slot = child.Value(0);
				const std::size_t index = Same(slot, "PRIMARY") ? 0 : Same(slot, "SECONDARY") ? 1 : Same(slot, "TERTIARY") ? 2 : 3;
				if (index < 3)
					entry.weapons[index] = std::string(child.Value(1));
			}
			else if (Same(child.key, "WeaponLockSharedAcrossSets"))
				entry.lockShared = Same(child.Value(), "Yes");
		const auto same = std::find_if(loadout.weaponSets.begin(), loadout.weaponSets.end(), [&](const auto &other) { return other.conditions == entry.conditions; });
		if (same != loadout.weaponSets.end())
			*same = std::move(entry);
		else
			loadout.weaponSets.push_back(std::move(entry));
	}
	for (const engine::config::Node *set : object.armorSets)
	{
		if (set == nullptr)
			continue;
		ArmorSetContent entry;
		entry.conditions = Conditions(set, ArmorSetFlagNames);
		if (const engine::config::Node *armor = set->Find("Armor"); armor != nullptr && !Same(armor->Value(), "None"))
			entry.armor = std::string(armor->Value());
		const auto same = std::find_if(loadout.armorSets.begin(), loadout.armorSets.end(), [&](const auto &other) { return other.conditions == entry.conditions; });
		if (same != loadout.armorSets.end())
			*same = std::move(entry);
		else
			loadout.armorSets.push_back(std::move(entry));
	}
	return loadout;
}
}
