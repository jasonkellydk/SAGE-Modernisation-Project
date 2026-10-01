export module games.generalszh.content.combat.loadout_content;
import std;

export import games.generalszh.content.objects.object_definition;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.spatial.components.targetable;

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
	engine::gameplay::SlotRules rules; // AutoChooseSources, PreferredAgainst
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

// The target classes an object of this KindOf is (what PreferredAgainst can name; others are none).
inline std::uint32_t TargetClassOfKind(std::string_view kind) noexcept
{
	using namespace engine::gameplay;
	constexpr std::pair<std::string_view, std::uint32_t> kinds[] = {{"STRUCTURE", target_class::Structure}, {"INFANTRY", target_class::Infantry},
		{"VEHICLE", target_class::Vehicle}, {"AIRCRAFT", target_class::Aircraft}, {"PROJECTILE", target_class::Projectile}, {"MINE", target_class::Mine},
		{"SMALL_MISSILE", target_class::SmallMissile}, {"BALLISTIC_MISSILE", target_class::BallisticMissile}};
	for (const auto &[name, bit] : kinds)
		if (loadout_detail::Same(name, kind))
			return bit;
	return 0;
}

// WeaponTemplateSet::parseAutoChoose and parsePreferredAgainst: "AutoChooseSources = <slot> <sources...>" (a bit each of
// FROM_PLAYER, FROM_SCRIPT, FROM_AI, FROM_DOZER, DEFAULT_SWITCH_WEAPON; NONE: none) and "PreferredAgainst = <slot>
// <KindOf...>", each replacing the slot's default (every source; nothing); and the set's ShareWeaponReloadTime.
inline engine::gameplay::SlotRules ReadSlotRules(const engine::config::Node &set)
{
	static constexpr std::array<std::string_view, 5> SourceNames{"FROM_PLAYER", "FROM_SCRIPT", "FROM_AI", "FROM_DOZER", "DEFAULT_SWITCH_WEAPON"};
	engine::gameplay::SlotRules rules;
	for (const engine::config::Node &child : set.children)
	{
		if (loadout_detail::Same(child.key, "ShareWeaponReloadTime"))
		{
			rules.sharedReload = !child.values.empty() && loadout_detail::Same(child.Value(), "Yes");
			continue;
		}
		const bool sources = loadout_detail::Same(child.key, "AutoChooseSources"), preferred = loadout_detail::Same(child.key, "PreferredAgainst");
		if ((!sources && !preferred) || child.values.empty())
			continue;
		const std::string_view slot = child.Value(0);
		const std::size_t index = loadout_detail::Same(slot, "PRIMARY") ? 0 : loadout_detail::Same(slot, "SECONDARY") ? 1 : loadout_detail::Same(slot, "TERTIARY") ? 2 : 3;
		if (index >= 3)
			continue;
		std::uint32_t bits = 0;
		for (std::size_t token = 1; token < child.values.size(); ++token)
			if (sources)
			{
				for (std::size_t bit = 0; bit < SourceNames.size(); ++bit)
					if (loadout_detail::Same(child.Value(token), SourceNames[bit]))
						bits |= 1u << bit;
			}
			else
			{
				// isKindOfMulti: all of them; one no target class stands for, nothing is.
				const std::uint32_t bit = TargetClassOfKind(child.Value(token));
				bits |= bit != 0 ? bit : engine::gameplay::target_class::Unmatchable;
			}
		if (sources)
			rules.sources[index] = static_cast<std::uint8_t>(bits);
		else
			rules.preferred[index] = bits;
	}
	return rules;
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
		entry.rules = ReadSlotRules(*set);
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
