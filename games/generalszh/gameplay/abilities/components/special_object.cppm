export module games.generalszh.gameplay.abilities.components.special_object;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;

// A special object a SpecialAbilityUpdate made (createSpecialObject: a charge, a booby trap): the unit whose module made
// it, which of its modules (its SpecialAbilities slot) and when. The original's m_specialObjectIDList and
// m_specialObjectEntries are these, asked of the world (a special object gone is off the list: validateSpecialObjects).
// `killWithOwner`: onExit(TRUE) as its owner dies or goes deletes it (not SpecialObjectsPersistent, or not
// SpecialObjectsPersistWhenOwnerDies). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct SpecialObject
{
	ecs::Entity owner;
	std::uint32_t sequence{0}; // the order its module made it in
	std::uint8_t slot{0};
	std::uint8_t killWithOwner{0};
	std::uint8_t reserved[2]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::SpecialObject>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.special_object";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
