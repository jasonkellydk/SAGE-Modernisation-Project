export module engine.gameplay.common.status.components.status_flags;
import std;

export import engine.ecs.core.component_registry;

// Status an entity carries that nothing else in the simulation records (the
// original's ObjectStatusMaskType bits a game sets from its own rules, such as
// which rider a vehicle carries). Which bit means what is the game's data;
// behaviours filtered by status (a die module's RequiredStatus and
// ExemptStatus) test these bits.
export namespace engine::gameplay
{
struct StatusFlags
{
	std::uint64_t bits{0};

	bool HasAll(std::uint64_t mask) const noexcept { return (bits & mask) == mask; }
	bool HasNone(std::uint64_t mask) const noexcept { return (bits & mask) == 0; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::StatusFlags>
{
	static constexpr std::string_view StableName = "engine.gameplay.status_flags";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
