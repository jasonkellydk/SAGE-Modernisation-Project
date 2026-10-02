export module games.generalszh.gameplay.abilities.components.sticky_bomb;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// StickyBombUpdate once a bomb is stuck on (initStickyBomb): what it is on (m_targetID; the bomb's producer too, so its
// victim can find it), the tick its LifetimeUpdate kills it (m_dieFrame; 0: a remote bomb) and its next ping
// (m_nextPingFrame). A bomb not stuck on anything has none. Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct StickyBomb
{
	ecs::Entity target;
	std::uint64_t dieTick{0};
	std::uint64_t nextPingTick{0};
};

// Each definition's StickyBombUpdate module data (present or not): OffsetZ (10), GeometryBasedDamageWeapon (a weapon
// index; none) and GeometryBasedDamageFX (a named effect id; none).
struct StickyBombConfig
{
	static constexpr std::uint32_t None = 0xFFFFFFFFu;
	bool present{false};
	Engine::Math::Fixed offsetZ{Engine::Math::Fixed::FromInt(10)};
	std::uint32_t weapon{None};
	std::uint32_t effect{None};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::StickyBomb>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.sticky_bomb";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
