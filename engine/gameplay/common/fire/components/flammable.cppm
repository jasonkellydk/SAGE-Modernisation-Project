export module engine.gameplay.common.fire.components.flammable;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
import engine.ecs.core.component_registry;

// Something that catches fire (the original's FlammableUpdate): flame
// damage wears its limit down (the limit comes back after `expiration`
// ticks without flame); at zero it is aflame for `aflameDuration` ticks,
// taking `burnAmount` every `burnDelay` ticks; after `burnedDelay` it is
// burned (smoldering) and never catches fire again.
export namespace engine::gameplay
{
enum class FlameState : std::uint32_t
{
	Normal,
	Aflame,
	Burned,
};

struct Flammable
{
	// As authored.
	Engine::Math::Fixed limit;
	Engine::Math::Fixed burnAmount;
	std::uint64_t expiration{0};
	std::uint64_t aflameDuration{0};
	std::uint64_t burnDelay{0};
	std::uint64_t burnedDelay{0};
	// State.
	Engine::Math::Fixed remaining;
	std::uint64_t lastFlameTick{0};
	std::uint64_t aflameEnd{0};
	std::uint64_t burnedAt{0}; // 0: not set
	std::uint64_t nextBurn{0}; // 0: no burn damage
	ecs::Entity source;
	FlameState state{FlameState::Normal};
	std::uint32_t burned{0}; // the burned mark is on (smoldering)
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Flammable>
{
	static constexpr std::string_view StableName = "engine.gameplay.flammable";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
