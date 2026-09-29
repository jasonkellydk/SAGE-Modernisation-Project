export module engine.gameplay.rts.stealth.components.grant_stealth;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.core.component_registry;

// Giving stealth to allies (the original's GrantStealthBehavior, the GPS
// scrambler): each tick its radius grows by `growRate` up to `finalRadius`,
// and every allied thing of its classes within it that has stealth may
// stealth from then on; after the final scan it is gone.
export namespace engine::gameplay
{
struct GrantStealth
{
	Engine::Math::Fixed radius;
	Engine::Math::Fixed growRate; // per tick
	Engine::Math::Fixed finalRadius;
	std::uint32_t classes{0}; // any of these (none: all)
	std::uint32_t done{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::GrantStealth>
{
	static constexpr std::string_view StableName = "engine.gameplay.grant_stealth";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
