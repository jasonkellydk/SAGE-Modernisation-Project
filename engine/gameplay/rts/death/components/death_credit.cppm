export module engine.gameplay.rts.death.components.death_credit;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

// Whom an object's death notices credit (a special power's delivery: the object that fired it, as
// SpecialPowerCompletionDie::setCreator records; none: no one). The death system hands it on with the notice.
export namespace engine::gameplay
{
struct DeathCredit
{
	ecs::Entity credit;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DeathCredit>
{
	static constexpr std::string_view StableName = "engine.gameplay.death_credit";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
