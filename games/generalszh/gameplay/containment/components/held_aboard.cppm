export module games.generalszh.gameplay.containment.components.held_aboard;
import std;

import engine.ecs.core.component_registry;

// A rider the container it is in holds (HelixContain::onContaining: DISABLED_HELD, and WEAPONBONUSCONDITION_GARRISONED),
// until it gets out (onRemoving clears both): which of its Held bits the container set. Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct HeldAboard
{
	std::uint8_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::HeldAboard>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.held_aboard";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
