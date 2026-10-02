export module games.generalszh.gameplay.upgrades.components.command_set_override;
import std;

export import engine.ecs.core.component_registry;

// The command set an upgrade gave an object in place of its own
// (Object::setCommandSetStringOverride, by CommandSetUpgrade): an id of the
// templates' command sets (ObjectTemplates::CommandSet).
export namespace generalszh::gameplay
{
struct CommandSetOverride
{
	std::uint32_t id{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::CommandSetOverride>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.command_set_override";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
