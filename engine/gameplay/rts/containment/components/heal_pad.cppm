export module engine.gameplay.rts.containment.components.heal_pad;
import std;

export import engine.ecs.core.component_registry;

// A container that heals those inside and lets each out once healed (the original's HealContain): everyone inside
// heals by their maximum over `fullHealTicks` a tick, and is whole and let out once they have been inside that long.
export namespace engine::gameplay
{
struct HealPad
{
	std::uint64_t fullHealTicks{1};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::HealPad>
{
	static constexpr std::string_view StableName = "engine.gameplay.heal_pad";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
