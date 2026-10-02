export module engine.gameplay.common.poison.components.poison;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;

// Something that poison stays in (the original's PoisonedBehavior): hurt by
// damage of `catchType`, it takes the damage it actually took again every
// `interval` ticks for `duration` ticks from the last such hit, as `dealType`
// damage (one that does not poison again) showing `fxType`'s effects, from the
// same source and to the same death; any healing ends it. While poisoned:
// when it is hurt next and when the poison stops (both 0: not poisoned), how
// much, from whom, to what death, and the health it had last tick (a rise is
// healing).
export namespace engine::gameplay
{
struct Poison
{
	std::uint64_t interval{1};
	std::uint64_t duration{0};
	std::uint32_t catchType{0};
	std::uint32_t dealType{0};
	std::uint32_t fxType{0};
	std::uint32_t deathType{0};
	std::uint64_t nextDamageTick{0};
	std::uint64_t stopTick{0};
	Engine::Math::Fixed amount;
	Engine::Math::Fixed seenHealth;
	ecs::Entity source;

	bool Active() const noexcept { return stopTick != 0; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Poison>
{
	static constexpr std::string_view StableName = "engine.gameplay.poison";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Poison &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.interval);
		hasher.AppendU64(value.duration);
		hasher.AppendU64((std::uint64_t{value.catchType} << 32) | value.dealType);
		hasher.AppendU64((std::uint64_t{value.fxType} << 32) | value.deathType);
		hasher.AppendU64(value.nextDamageTick);
		hasher.AppendU64(value.stopTick);
		hasher.AppendU64(static_cast<std::uint64_t>(value.amount.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.seenHealth.Raw()));
		hasher.AppendU64((std::uint64_t{value.source.index} << 32) | value.source.generation);
	}
};
}
