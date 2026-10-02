export module engine.gameplay.common.health.components.health;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;

// Hit points, the armor that filters incoming damage (an index into the
// ArmorCatalog) and who hurt the entity last.
export namespace engine::gameplay
{
struct Health
{
	Engine::Math::Fixed current;
	Engine::Math::Fixed maximum;
	std::uint32_t armor{0};
	ecs::Entity lastAttacker;
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
	std::uint64_t lastDamageTick{0};
	std::uint32_t lastDamageType{0}; // the last damage's type (DamageInfo's default: EXPLOSION, 0)
	// The last damage's attacker as it was when it hit (DamageInfo m_sourcePlayerMask and m_sourceTemplate: its player
	// and its definition, kept after it is gone); None: not known.
	static constexpr std::uint32_t None = 0xFFFFFFFFu;
	std::uint32_t lastAttackerPlayer{None};
	std::uint32_t lastAttackerDefinition{None};
	// ActiveBody::m_indestructible (setIndestructible: a script or map property): no damage at all, not even a kill.
	bool indestructible{false};
	std::uint8_t reservedTail[3]{};
	// ActiveBody::m_lastHealingTimestamp: the tick healing last added to it (0: never).
	std::uint64_t lastHealingTick{0};
};

inline bool IsDead(const Health &health) noexcept { return health.current <= Engine::Math::Fixed{}; }

// ActiveBody::attemptHealing's own part (after its armour): never the dead; a positive amount adds up to its maximum
// and stamps the tick (m_lastHealingTimestamp), whether or not health rose. Returns what it gained.
inline Engine::Math::Fixed Heal(Health &health, Engine::Math::Fixed amount, std::uint64_t tick) noexcept
{
	if (IsDead(health) || amount <= Engine::Math::Fixed{})
		return {};
	const Engine::Math::Fixed before = health.current;
	health.current = health.current + amount < health.maximum ? health.current + amount : health.maximum;
	health.lastHealingTick = tick;
	return health.current - before;
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Health>
{
	static constexpr std::string_view StableName = "engine.gameplay.health";
	static constexpr std::uint32_t Version = 4;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Health &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.current.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.maximum.Raw()));
		hasher.AppendU64(value.armor);
		hasher.AppendU64(value.lastAttacker.index);
		hasher.AppendU64(value.lastAttacker.generation);
		hasher.AppendU64(value.lastDamageTick);
		hasher.AppendU64(value.lastDamageType);
		hasher.AppendU64((std::uint64_t{value.lastAttackerPlayer} << 32) | value.lastAttackerDefinition);
		hasher.AppendU64(value.indestructible ? 1u : 0u);
		hasher.AppendU64(value.lastHealingTick);
	}
};
}
