export module engine.gameplay.rts.combat.components.contained_definitions;
import std;

export import engine.ecs.core.component_registry;

// What a container holds, by definition (ContainModuleInterface::iterateContained as AI::findClosestEnemy reads it: a
// garrison or transport is as worth attacking as the most wanted thing inside it, by the attacker's priority set). Derived
// each tick from the cargo before targeting (ContainedDefinitionsSystem); kept with checkpoints as it stands.
export namespace engine::gameplay
{
struct ContainedDefinitions
{
	static constexpr std::size_t Capacity = 16;
	std::array<std::uint32_t, Capacity> definitions{};
	std::uint8_t count{0};
	std::uint8_t reserved[3]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::ContainedDefinitions>
{
	static constexpr std::string_view StableName = "engine.gameplay.contained_definitions";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::ContainedDefinitions &value, StateHasher &hasher) noexcept
	{
		for (std::size_t index = 0; index < value.count; ++index)
			hasher.AppendU64(value.definitions[index]);
	}
};
}
