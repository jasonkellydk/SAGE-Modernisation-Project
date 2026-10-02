export module games.generalszh.gameplay.abilities.components.command_button_hunt;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// A CommandButtonHuntUpdate: its module data (ScanRate in ticks, ScanRange) and the button it hunts with, by its command
// set (an ObjectTemplates command set id) and slot there (none: it is not hunting); its next update on `nextTick`.
// Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct CommandButtonHunt
{
	static constexpr std::uint32_t NotHunting = 0xFFFFFFFFu;
	Engine::Math::Fixed scanRange{Engine::Math::Fixed::FromInt(9999)};
	std::uint64_t scanTicks{30};
	std::uint64_t nextTick{0};
	std::uint32_t set{NotHunting};
	std::uint32_t slot{0};
	std::uint32_t again{0}; // setCommandButton: updated as it is set, and again the tick after
	std::uint32_t reserved{0};

	bool Hunting() const noexcept { return set != NotHunting; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::CommandButtonHunt>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.command_button_hunt";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
