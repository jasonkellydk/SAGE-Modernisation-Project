export module games.generalszh.content.objects.model_conditions;
import std;

// Zero Hour's model condition names (ModelConditionFlags), in the original's
// bit order: Draw modules pick condition states by them and the game's
// appearance system sets them on engine Appearance bits.
export namespace generalszh::content
{
inline constexpr std::array<std::string_view, 118> ModelConditionNames{"TOPPLED", "FRONTCRUSHED", "BACKCRUSHED", "DAMAGED",
	"REALLYDAMAGED", "RUBBLE", "SPECIAL_DAMAGED", "NIGHT", "SNOW", "PARACHUTING", "GARRISONED", "ENEMYNEAR", "WEAPONSET_VETERAN",
	"WEAPONSET_ELITE", "WEAPONSET_HERO", "WEAPONSET_CRATEUPGRADE_ONE", "WEAPONSET_CRATEUPGRADE_TWO", "WEAPONSET_PLAYER_UPGRADE",
	"DOOR_1_OPENING", "DOOR_1_CLOSING", "DOOR_1_WAITING_OPEN", "DOOR_1_WAITING_TO_CLOSE", "DOOR_2_OPENING", "DOOR_2_CLOSING",
	"DOOR_2_WAITING_OPEN", "DOOR_2_WAITING_TO_CLOSE", "DOOR_3_OPENING", "DOOR_3_CLOSING", "DOOR_3_WAITING_OPEN",
	"DOOR_3_WAITING_TO_CLOSE", "DOOR_4_OPENING", "DOOR_4_CLOSING", "DOOR_4_WAITING_OPEN", "DOOR_4_WAITING_TO_CLOSE", "ATTACKING",
	"PREATTACK_A", "FIRING_A", "BETWEEN_FIRING_SHOTS_A", "RELOADING_A", "PREATTACK_B", "FIRING_B", "BETWEEN_FIRING_SHOTS_B",
	"RELOADING_B", "PREATTACK_C", "FIRING_C", "BETWEEN_FIRING_SHOTS_C", "RELOADING_C", "TURRET_ROTATE", "POST_COLLAPSE", "MOVING",
	"DYING", "AWAITING_CONSTRUCTION", "PARTIALLY_CONSTRUCTED", "ACTIVELY_BEING_CONSTRUCTED", "PRONE", "FREEFALL",
	"ACTIVELY_CONSTRUCTING", "CONSTRUCTION_COMPLETE", "RADAR_EXTENDING", "RADAR_UPGRADED", "PANICKING", "AFLAME", "SMOLDERING",
	"BURNED", "DOCKING", "DOCKING_BEGINNING", "DOCKING_ACTIVE", "DOCKING_ENDING", "CARRYING", "FLOODED", "LOADED", "JETAFTERBURNER",
	"JETEXHAUST", "PACKING", "UNPACKING", "DEPLOYED", "OVER_WATER", "POWER_PLANT_UPGRADED", "CLIMBING", "SOLD", "SURRENDER",
	"RAPPELLING", "ARMED", "POWER_PLANT_UPGRADING", "SPECIAL_CHEERING", "CONTINUOUS_FIRE_SLOW", "CONTINUOUS_FIRE_MEAN",
	"CONTINUOUS_FIRE_FAST", "RAISING_FLAG", "CAPTURED", "EXPLODED_FLAILING", "EXPLODED_BOUNCING", "SPLATTED", "USING_WEAPON_A",
	"USING_WEAPON_B", "USING_WEAPON_C", "PREORDER", "CENTER_TO_LEFT", "LEFT_TO_CENTER", "CENTER_TO_RIGHT", "RIGHT_TO_CENTER",
	"RIDER1", "RIDER2", "RIDER3", "RIDER4", "RIDER5", "RIDER6", "RIDER7", "RIDER8", "STUNNED_FLAILING", "STUNNED", "SECOND_LIFE",
	"JAMMED", "ARMORSET_CRATEUPGRADE_ONE", "ARMORSET_CRATEUPGRADE_TWO", "USER_1", "USER_2", "DISGUISED"};

inline constexpr std::uint32_t NoCondition = 0xFFFFFFFFu;

// The bit of a condition name (case as in the data); NoCondition when unknown.
constexpr std::uint32_t ModelConditionBit(std::string_view name) noexcept
{
	for (std::uint32_t index = 0; index < ModelConditionNames.size(); ++index)
		if (ModelConditionNames[index] == name)
			return index;
	return NoCondition;
}

// The conditions the simulation sets so far.
namespace model_condition
{
inline constexpr std::uint32_t Damaged = ModelConditionBit("DAMAGED");
inline constexpr std::uint32_t ReallyDamaged = ModelConditionBit("REALLYDAMAGED");
inline constexpr std::uint32_t Parachuting = ModelConditionBit("PARACHUTING");
inline constexpr std::uint32_t Attacking = ModelConditionBit("ATTACKING");
inline constexpr std::uint32_t FiringA = ModelConditionBit("FIRING_A");
inline constexpr std::uint32_t BetweenFiringShotsA = ModelConditionBit("BETWEEN_FIRING_SHOTS_A");
inline constexpr std::uint32_t ReloadingA = ModelConditionBit("RELOADING_A");
inline constexpr std::uint32_t Moving = ModelConditionBit("MOVING");
inline constexpr std::uint32_t Dying = ModelConditionBit("DYING");
inline constexpr std::uint32_t Rubble = ModelConditionBit("RUBBLE");
inline constexpr std::uint32_t PostCollapse = ModelConditionBit("POST_COLLAPSE");
inline constexpr std::uint32_t PartiallyConstructed = ModelConditionBit("PARTIALLY_CONSTRUCTED");
inline constexpr std::uint32_t ActivelyBeingConstructed = ModelConditionBit("ACTIVELY_BEING_CONSTRUCTED");
inline constexpr std::uint32_t Sold = ModelConditionBit("SOLD");
inline constexpr std::uint32_t Captured = ModelConditionBit("CAPTURED");
inline constexpr std::uint32_t PreattackA = ModelConditionBit("PREATTACK_A");
inline constexpr std::uint32_t AwaitingConstruction = ModelConditionBit("AWAITING_CONSTRUCTION");
inline constexpr std::uint32_t ActivelyConstructing = ModelConditionBit("ACTIVELY_CONSTRUCTING");
inline constexpr std::uint32_t Aflame = ModelConditionBit("AFLAME");
inline constexpr std::uint32_t Smoldering = ModelConditionBit("SMOLDERING");
inline constexpr std::uint32_t Burned = ModelConditionBit("BURNED");
inline constexpr std::uint32_t Toppled = ModelConditionBit("TOPPLED");
inline constexpr std::uint32_t SpecialDamaged = ModelConditionBit("SPECIAL_DAMAGED");
inline constexpr std::uint32_t ConstructionComplete = ModelConditionBit("CONSTRUCTION_COMPLETE");
// Door n (from 0) opening, closing, waiting open: DOOR_1_OPENING and on, four apart.
inline constexpr std::uint32_t DoorOpening(std::uint32_t door) noexcept { return ModelConditionBit("DOOR_1_OPENING") + 4 * door; }
inline constexpr std::uint32_t DoorClosing(std::uint32_t door) noexcept { return ModelConditionBit("DOOR_1_CLOSING") + 4 * door; }
inline constexpr std::uint32_t DoorWaitingOpen(std::uint32_t door) noexcept { return ModelConditionBit("DOOR_1_WAITING_OPEN") + 4 * door; }
// Not the original's model conditions: how the object is seen, for the
// presentation (bits past the original's names).
inline constexpr std::uint32_t StealthedLook = 126;
inline constexpr std::uint32_t DetectedLook = 127;
inline constexpr std::uint32_t Loaded = ModelConditionBit("LOADED");
inline constexpr std::uint32_t Carrying = ModelConditionBit("CARRYING");
inline constexpr std::uint32_t ArmorsetCrateUpgradeOne = ModelConditionBit("ARMORSET_CRATEUPGRADE_ONE");
inline constexpr std::uint32_t ArmorsetCrateUpgradeTwo = ModelConditionBit("ARMORSET_CRATEUPGRADE_TWO");
inline constexpr std::uint32_t FrontCrushed = ModelConditionBit("FRONTCRUSHED");
inline constexpr std::uint32_t BackCrushed = ModelConditionBit("BACKCRUSHED");
inline constexpr std::uint32_t ExplodedFlailing = ModelConditionBit("EXPLODED_FLAILING");
inline constexpr std::uint32_t ExplodedBouncing = ModelConditionBit("EXPLODED_BOUNCING");
inline constexpr std::uint32_t Docking = ModelConditionBit("DOCKING");
inline constexpr std::uint32_t Packing = ModelConditionBit("PACKING");
inline constexpr std::uint32_t Unpacking = ModelConditionBit("UNPACKING");
inline constexpr std::uint32_t Deployed = ModelConditionBit("DEPLOYED");
inline constexpr std::uint32_t DockingBeginning = ModelConditionBit("DOCKING_BEGINNING");
inline constexpr std::uint32_t DockingActive = ModelConditionBit("DOCKING_ACTIVE");
inline constexpr std::uint32_t DockingEnding = ModelConditionBit("DOCKING_ENDING");
inline constexpr std::uint32_t OverWater = ModelConditionBit("OVER_WATER");
inline constexpr std::uint32_t Night = ModelConditionBit("NIGHT");
inline constexpr std::uint32_t JetAfterburner = ModelConditionBit("JETAFTERBURNER");
inline constexpr std::uint32_t Snow = ModelConditionBit("SNOW");
inline constexpr std::uint32_t Freefall = ModelConditionBit("FREEFALL");
inline constexpr std::uint32_t UsingWeaponA = ModelConditionBit("USING_WEAPON_A");
}
}
