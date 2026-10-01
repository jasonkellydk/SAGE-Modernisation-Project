export module engine.gameplay.common.spatial.components.targetable;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// Something other entities can find and aim at: its footprint radius and
// what kind of target it is. Whether it is airborne is decided each tick
// from its height above ground.
export namespace engine::gameplay
{
namespace target_class
{
inline constexpr std::uint32_t Ground = 1u << 0;
inline constexpr std::uint32_t AirborneVehicle = 1u << 1;
inline constexpr std::uint32_t AirborneInfantry = 1u << 2;
inline constexpr std::uint32_t Structure = 1u << 3;
inline constexpr std::uint32_t Infantry = 1u << 4;
inline constexpr std::uint32_t Vehicle = 1u << 5;
inline constexpr std::uint32_t Aircraft = 1u << 6;
inline constexpr std::uint32_t Projectile = 1u << 7;
inline constexpr std::uint32_t Mine = 1u << 8;
// Found by radius damage but never picked as a target (e.g. scenery).
inline constexpr std::uint32_t Unattackable = 1u << 9;
// Hidden from enemies (stealthed and not detected): never picked as a target.
inline constexpr std::uint32_t Hidden = 1u << 10;
// Stealthed, detected or not: what stealth detectors look for.
inline constexpr std::uint32_t Stealthed = 1u << 11;
// Missiles in flight (KindOf SMALL_MISSILE, BALLISTIC_MISSILE): only weapons against them aim at them.
inline constexpr std::uint32_t SmallMissile = 1u << 12;
inline constexpr std::uint32_t BallisticMissile = 1u << 13;
// An undetected defector (Object::getRelationship): everyone's friend, never picked as a target, and seen as neutral
// by radius damage.
inline constexpr std::uint32_t Undetected = 1u << 14;
// Never picked by a scan for something to attack (OBJECT_STATUS_NO_ATTACK_FROM_AI: WeaponSet::isAbleToAttack refuses
// CMD_FROM_AI): a mine; an order still may.
inline constexpr std::uint32_t NoAttackFromAi = 1u << 15;
// KINDOF_SHRUBBERY (only a burning weapon is reckoned to hurt it), KINDOF_BOOBY_TRAP or KINDOF_DEMOTRAP (disarmed like a
// mine), KINDOF_HERO (its kill-pilot weapon waits for its own mode).
inline constexpr std::uint32_t Shrubbery = 1u << 16;
inline constexpr std::uint32_t Trap = 1u << 17;
inline constexpr std::uint32_t Hero = 1u << 18;
// A container holding nobody; a garrison that can be cleared holding someone (ContainerClassesSystem keeps them).
inline constexpr std::uint32_t Emptied = 1u << 19;
inline constexpr std::uint32_t Clearable = 1u << 20;
// A disguiser stealthed, undetected and disguised (StealthUpdate::isDisguised: the bomb truck): hidden only from those who
// do not count the player it is disguised as their enemy (PartitionFilterStealthedAndUndetected,
// WeaponSet::getAbleToAttackSpecificObject); see HiddenFrom.
inline constexpr std::uint32_t Disguised = 1u << 21;
// KINDOF_FS_BASE_DEFENSE; a container able to attack (Object::getContain() and isAbleToAttack: CAN_ATTACK, armed, or
// letting its riders fire with someone aboard; ContainerClassesSystem keeps it). PartitionFilterRejectBuildings lets
// both through.
inline constexpr std::uint32_t BaseDefense = 1u << 22;
inline constexpr std::uint32_t ArmedContainer = 1u << 23;
// Held by nothing: what a rule naming a kind no class stands for requires.
inline constexpr std::uint32_t Unmatchable = 1u << 31;
}

struct Targetable
{
	Engine::Math::Fixed radius;
	std::uint32_t classes{0};
	// Disguised: the player it is disguised as and that player's default team (for HiddenFrom).
	std::uint32_t disguiseTeam{0xFFFFFFFFu};
	std::int32_t disguisePlayer{-1};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
};

// Whether a viewer of `viewerPlayer` may not pick it as a target: hidden (stealthed and undetected), or disguised as a
// player the viewer's player does not count its enemy (ourPlayer->getRelationship(otherPlayer->getDefaultTeam()) !=
// ENEMIES). `Relationships` is the identity domain's (Between(fromTeam, fromPlayer, toTeam, toPlayer)).
template<class Relationships>
bool HiddenFrom(std::uint32_t classes, std::int32_t disguisePlayer, std::uint32_t disguiseTeam, std::uint32_t viewerPlayer,
	const Relationships &relationships) noexcept
{
	if ((classes & target_class::Hidden) != 0)
		return true;
	if ((classes & target_class::Disguised) == 0)
		return false;
	return disguisePlayer < 0 ||
		!relationships.Enemies(0xFFFFFFFFu, viewerPlayer, disguiseTeam, static_cast<std::uint32_t>(disguisePlayer));
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Targetable>
{
	static constexpr std::string_view StableName = "engine.gameplay.targetable";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Targetable &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.radius.Raw()));
		hasher.AppendU64(value.classes | (std::uint64_t{value.disguiseTeam} << 32));
		hasher.AppendU64(static_cast<std::uint64_t>(static_cast<std::int64_t>(value.disguisePlayer)));
	}
};
}
