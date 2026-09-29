export module engine.gameplay.common.fire.components.fire_spread;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.core.component_registry;

// Fire that spreads (the original's FireSpreadUpdate): while its owner is
// aflame, every `minDelay`..`maxDelay` ticks (at random) it throws embers
// and sets the closest thing within `range` that would catch fire alight.
export namespace engine::gameplay
{
struct FireSpread
{
	std::uint64_t minDelay{0};
	std::uint64_t maxDelay{0};
	std::uint64_t nextTry{0}; // 0: not burning, no try due
	Engine::Math::Fixed range;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::FireSpread>
{
	static constexpr std::string_view StableName = "engine.gameplay.fire_spread";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
