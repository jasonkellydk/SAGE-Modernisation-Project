export module engine.gameplay.rts.combat.components.aggression;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// How an armed entity picks its own targets. Idle entities that auto-
// acquire look around them every `scanInterval` ticks; guards defend a
// point; hunters seek the nearest enemy anywhere; held entities only shoot
// what comes into weapon range. `area` (a TriggerAreas index; none: 0xFFFFFFFF) keeps a guard's or hunter's
// victims to those inside it (AIGuardArea, AIAttackAreaState: PartitionFilterPolygonTrigger). `attitude` is its AI's
// mood (AIUpdateInterface::m_attitude, AttitudeType: sleep -2 ... aggressive 2), which shapes a computer player's idle
// look around (getNextMoodTarget); `vision` its vision range, which that look starts from.
export namespace engine::gameplay
{
namespace attitude
{
inline constexpr std::int8_t Sleep = -2;
inline constexpr std::int8_t Passive = -1;
inline constexpr std::int8_t Normal = 0;
inline constexpr std::int8_t Alert = 1;
inline constexpr std::int8_t Aggressive = 2;
}

enum class Stance : std::uint8_t
{
	Idle,
	Guard,
	Hunt,
	Hold,
};

struct Aggression
{
	Engine::Math::FixedVector2 guardCenter;
	Engine::Math::Fixed guardRadius;
	Engine::Math::Fixed scanRange;
	std::uint64_t scanInterval{60};
	std::uint64_t nextScan{0};
	Engine::Math::Fixed vision;
	std::uint32_t area{0xFFFFFFFFu};
	std::uint16_t prioritySet{0}; // its attack priority set (AttackPriorities: index + 1; 0: none, the nearest wins)
	Stance stance{Stance::Idle};
	bool autoAcquire{false};
	bool attackBuildings{false};
	std::int8_t attitude{attitude::Normal};
	bool acquireStealthed{false}; // AutoAcquireEnemiesWhenIdle Stealthed: it looks for victims while stealthed too
	std::uint8_t reserved[5]{};   // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Aggression>
{
	static constexpr std::string_view StableName = "engine.gameplay.aggression";
	static constexpr std::uint32_t Version = 6;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Aggression &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.guardCenter.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.guardCenter.y.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.guardRadius.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.scanRange.Raw()));
		hasher.AppendU64(value.scanInterval);
		hasher.AppendU64(value.nextScan);
		hasher.AppendU64(static_cast<std::uint64_t>(value.stance) | (value.autoAcquire ? 0x100u : 0u) | (value.attackBuildings ? 0x200u : 0u) | (value.acquireStealthed ? 0x400u : 0u) |
			(std::uint64_t{value.area} << 32) | (std::uint64_t{value.prioritySet} << 16));
		hasher.AppendU64(static_cast<std::uint64_t>(value.vision.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(static_cast<std::int64_t>(value.attitude)));
	}
};
}
