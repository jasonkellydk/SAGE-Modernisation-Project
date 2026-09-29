export module games.generalszh.gameplay.abilities.components.special_abilities;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// An object's SpecialAbilityUpdate modules (up to three: the Black Lotus has the most), each its module data and where
// it stands in carrying out its power: approach the target, face it, unpack, prepare, trigger, pack, finish. (Whether
// an order from outside its AI came since is its AiActivity's.) Simulation state: checkpointed.
export namespace generalszh::gameplay
{
// The SpecialPowerType of its power, where the update treats it apart.
enum class AbilityKind : std::uint8_t
{
	Other,
	InfantryCaptureBuilding,   // SPECIAL_INFANTRY_CAPTURE_BUILDING
	BlackLotusCaptureBuilding, // SPECIAL_BLACKLOTUS_CAPTURE_BUILDING
	HackerDisableBuilding,     // SPECIAL_HACKER_DISABLE_BUILDING
	BlackLotusDisableVehicle,  // SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK
	BlackLotusStealCash,       // SPECIAL_BLACKLOTUS_STEAL_CASH_HACK
	BoobyTrap,                 // SPECIAL_BOOBY_TRAP
	RemoteCharges,             // SPECIAL_REMOTE_CHARGES
	TimedCharges,              // SPECIAL_TIMED_CHARGES
	LaserGuidedMissiles,       // SPECIAL_MISSILE_DEFENDER_LASER_GUIDED_MISSILES
	TankHunterTnt,             // SPECIAL_TANKHUNTER_TNT_ATTACK
	HelixNapalmBomb,           // SPECIAL_HELIX_NAPALM_BOMB
	DisguiseAsVehicle,         // SPECIAL_DISGUISE_AS_VEHICLE
};

// SpecialAbilityUpdate::PackingState.
enum class AbilityPacking : std::uint8_t
{
	None,
	Packing,
	Unpacking,
	Packed,
	Unpacked,
};

// The module's switches.
namespace ability_option
{
inline constexpr std::uint8_t SkipPackingWithNoTarget = 1u << 0;
inline constexpr std::uint8_t FlipAfterPacking = 1u << 1;
inline constexpr std::uint8_t FlipAfterUnpacking = 1u << 2;
inline constexpr std::uint8_t DoCaptureFx = 1u << 3;
inline constexpr std::uint8_t LoseStealthOnTrigger = 1u << 4;
inline constexpr std::uint8_t ApproachRequiresLos = 1u << 5;
inline constexpr std::uint8_t NeedToFaceTarget = 1u << 6;
inline constexpr std::uint8_t PersistenceRequiresRecharge = 1u << 7;
}

// Its special objects' switches (SpecialObjectsPersistent, SpecialObjectsPersistWhenOwnerDies, UniqueSpecialObjectTargets,
// AlwaysValidateSpecialObjects).
namespace special_object_option
{
inline constexpr std::uint8_t Persistent = 1u << 0;
inline constexpr std::uint8_t PersistWhenOwnerDies = 1u << 1;
inline constexpr std::uint8_t UniqueTargets = 1u << 2;
inline constexpr std::uint8_t AlwaysValidate = 1u << 3;
}

// Its state's switches (m_active, m_noTargetCommand, m_facingInitiated, m_facingComplete, m_withinStartAbilityRange).
namespace ability_flag
{
inline constexpr std::uint8_t Active = 1u << 0;
inline constexpr std::uint8_t NoTargetCommand = 1u << 1;
inline constexpr std::uint8_t FacingInitiated = 1u << 2;
inline constexpr std::uint8_t FacingComplete = 1u << 3;
inline constexpr std::uint8_t WithinRange = 1u << 4;
}

struct AbilitySlot
{
	// SpecialAbilityUpdateModuleData.
	Engine::Math::Fixed startRange;
	Engine::Math::Fixed abortRange;
	Engine::Math::Fixed variation;
	Engine::Math::Fixed fleeRange;
	// m_targetPos (zero: none), m_targetID.
	Engine::Math::FixedVector3 targetPos;
	ecs::Entity target;
	std::uint32_t preparationTicks{0};
	std::uint32_t persistentPrepTicks{0};
	std::uint32_t packTicks{0};
	std::uint32_t unpackTicks{0};
	std::uint32_t preTriggerUnstealthTicks{0};
	std::uint32_t effectTicks{0};
	std::int32_t awardXp{0};
	std::int32_t skillPoints{-1};
	std::int32_t effectValue{1};
	std::uint32_t power{0}; // its SpecialPower template
	// m_prepFrames, m_animFrames, and what the pack or unpack took in all (the animation's length).
	std::uint32_t prepTicks{0};
	std::uint32_t animTicks{0};
	std::uint32_t animTotal{0};
	AbilityKind kind{AbilityKind::Other};
	std::uint8_t options{0};
	AbilityPacking packing{AbilityPacking::None};
	std::uint8_t flags{0};
	// MaxSpecialObjects, how many special objects it has made (their order), and the special objects' switches.
	std::uint32_t maxSpecialObjects{1};
	std::uint32_t objectsMade{0};
	std::uint8_t objectOptions{0};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
	// A trigger that set off a booby trap on its target (checkAndDetonateBoobyTrap): the effect goes on once the blast
	// has landed, unless it or the target is then dead (the trap's damage lands with the next tick's). None: no such.
	ecs::Entity trapTarget;

	bool Has(std::uint8_t flag) const noexcept { return (flags & flag) != 0; }
	bool Option(std::uint8_t option) const noexcept { return (options & option) != 0; }
	bool ObjectOption(std::uint8_t option) const noexcept { return (objectOptions & option) != 0; }
	void Set(std::uint8_t flag, bool on) noexcept { flags = static_cast<std::uint8_t>(on ? (flags | flag) : (flags & ~flag)); }
};

struct SpecialAbilities
{
	static constexpr std::size_t MaxSlots = 3;
	std::array<AbilitySlot, MaxSlots> slots{};
	std::uint8_t count{0};
	std::uint8_t reserved[7]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::SpecialAbilities>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.special_abilities";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
