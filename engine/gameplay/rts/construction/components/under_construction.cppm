export module engine.gameplay.rts.construction.components.under_construction;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

// A structure still being built (OBJECT_STATUS_UNDER_CONSTRUCTION): how many
// ticks its build takes at full power (BuildTime; low power stretches it),
// whether a builder has got to work on it yet (it awaits construction until
// then), and the last tick one worked on it. Not yet part of its player's
// power. And the builder set to it (Object::getBuilderID), whether or not it is at work on it now.
export namespace engine::gameplay
{
struct UnderConstruction
{
	std::uint64_t buildTicks{1};
	std::uint64_t workedTick{0};
	std::uint8_t started{0};
	std::uint8_t reserved[7]{};
	ecs::Entity builder;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::UnderConstruction>
{
	static constexpr std::string_view StableName = "engine.gameplay.under_construction";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
