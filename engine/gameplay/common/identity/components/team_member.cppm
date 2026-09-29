export module engine.gameplay.common.identity.components.team_member;
import std;

export import engine.ecs.core.component_registry;

// The team an entity belongs to: an index into the session's TeamRoster
// (the owning player is the entity's Owner).
export namespace engine::gameplay
{
struct TeamMember
{
	std::uint32_t team{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::TeamMember>
{
	static constexpr std::string_view StableName = "engine.gameplay.team_member";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
