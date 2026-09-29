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
}

struct Targetable
{
	Engine::Math::Fixed radius;
	std::uint32_t classes{0};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Targetable>
{
	static constexpr std::string_view StableName = "engine.gameplay.targetable";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Targetable &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.radius.Raw()));
		hasher.AppendU64(value.classes);
	}
};
}
