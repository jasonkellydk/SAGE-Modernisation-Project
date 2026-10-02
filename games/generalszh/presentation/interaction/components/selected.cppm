export module games.generalszh.presentation.interaction.components.selected;
import std;

export import engine.ecs.core.component_registry;

// The local player's selection (the original's Drawable::isSelected), as a
// side table on the simulation's own entities: its lifetime is the entity's,
// and it never enters the simulation's state.
export namespace generalszh::presentation
{
struct Selected
{
	std::uint8_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::Selected>
{
	static constexpr std::string_view StableName = "generalszh.presentation.selected";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
