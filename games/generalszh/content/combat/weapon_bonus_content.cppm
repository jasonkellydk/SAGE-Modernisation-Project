export module games.generalszh.content.combat.weapon_bonus_content;
import std;

export import engine.config.binding.schema;
export import engine.gameplay.common.weapons.definitions.weapon_bonus;

// A `WeaponBonus = <CONDITION> <FIELD> <PERCENT>` line (GameData's global
// table, or a Weapon block's own: WeaponBonusSet::parseWeaponBonusSet) into
// that row of the table.
export namespace generalszh::content
{
// Zero Hour's WeaponBonusConditionType, in the original's bit order (bits of WeaponBonusConditions).
namespace weapon_bonus
{
inline constexpr std::uint32_t Garrisoned = 1u << 0;
inline constexpr std::uint32_t Horde = 1u << 1;
inline constexpr std::uint32_t ContinuousFireMean = 1u << 2;
inline constexpr std::uint32_t ContinuousFireFast = 1u << 3;
inline constexpr std::uint32_t Nationalism = 1u << 4;
inline constexpr std::uint32_t PlayerUpgrade = 1u << 5;
inline constexpr std::uint32_t DroneSpotting = 1u << 6;
inline constexpr std::uint32_t DemoralizedObsolete = 1u << 7;
inline constexpr std::uint32_t Enthusiastic = 1u << 8;
inline constexpr std::uint32_t Veteran = 1u << 9;
inline constexpr std::uint32_t Elite = 1u << 10;
inline constexpr std::uint32_t Hero = 1u << 11;
inline constexpr std::uint32_t BattleplanBombardment = 1u << 12;
inline constexpr std::uint32_t BattleplanHoldTheLine = 1u << 13;
inline constexpr std::uint32_t BattleplanSearchAndDestroy = 1u << 14;
inline constexpr std::uint32_t Subliminal = 1u << 15;
inline constexpr std::uint32_t SoloHumanEasy = 1u << 16;
inline constexpr std::uint32_t SoloHumanNormal = 1u << 17;
inline constexpr std::uint32_t SoloHumanHard = 1u << 18;
inline constexpr std::uint32_t SoloAiEasy = 1u << 19;
inline constexpr std::uint32_t SoloAiNormal = 1u << 20;
inline constexpr std::uint32_t SoloAiHard = 1u << 21;
inline constexpr std::uint32_t TargetFaerieFire = 1u << 22;
inline constexpr std::uint32_t Fanaticism = 1u << 23;
inline constexpr std::uint32_t FrenzyOne = 1u << 24;
inline constexpr std::uint32_t FrenzyTwo = 1u << 25;
inline constexpr std::uint32_t FrenzyThree = 1u << 26;
inline constexpr std::size_t Count = 27;

// TheWeaponBonusNames: the INI names, by bit.
inline constexpr std::array<std::string_view, Count> Names{"GARRISONED", "HORDE", "CONTINUOUS_FIRE_MEAN", "CONTINUOUS_FIRE_FAST",
	"NATIONALISM", "PLAYER_UPGRADE", "DRONE_SPOTTING", "DEMORALIZED_OBSOLETE", "ENTHUSIASTIC", "VETERAN", "ELITE", "HERO",
	"BATTLEPLAN_BOMBARDMENT", "BATTLEPLAN_HOLDTHELINE", "BATTLEPLAN_SEARCHANDDESTROY", "SUBLIMINAL", "SOLO_HUMAN_EASY",
	"SOLO_HUMAN_NORMAL", "SOLO_HUMAN_HARD", "SOLO_AI_EASY", "SOLO_AI_NORMAL", "SOLO_AI_HARD", "TARGET_FAERIE_FIRE", "FANATICISM",
	"FRENZY_ONE", "FRENZY_TWO", "FRENZY_THREE"};
}


inline bool ReadWeaponBonus(const engine::config::Node &node, engine::gameplay::WeaponBonusSet &set, engine::config::BindContext &context)
{
	const auto same = [](std::string_view a, std::string_view b) {
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
			return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
		});
	};
	namespace wbc = weapon_bonus;
	std::optional<std::size_t> condition, field;
	for (std::size_t index = 0; index < wbc::Names.size(); ++index)
		if (same(node.Value(0), wbc::Names[index]))
			condition = index;
	// The original's name when ALLOW_DEMORALIZE is off is DEMORALIZED_OBSOLETE; DEMORALIZED reads as that.
	if (!condition && same(node.Value(0), "DEMORALIZED"))
		condition = 7;
	for (std::size_t index = 0; index < engine::gameplay::WeaponBonusFieldNames.size(); ++index)
		if (same(node.Value(1), engine::gameplay::WeaponBonusFieldNames[index]))
			field = index;
	std::string_view percent = node.Value(2);
	if (!percent.empty() && percent.back() == '%')
		percent.remove_suffix(1);
	const auto value = engine::config::values::ParseFixed(percent);
	if (!condition || !field || !value)
	{
		context.diagnostics.Warning(node.location, "WeaponBonus needs a condition, a field and a percentage");
		return false;
	}
	set.byCondition[*condition].fields[*field] = *value / Engine::Math::Fixed::FromInt(100);
	return true;
}
}
