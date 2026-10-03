export module engine.gameplay.rts.movement.components.locomotor_choice;
import std;

export import engine.ecs.core.component_registry;
export import engine.gameplay.rts.movement.definitions.locomotor;

// The locomotors of a unit's current set when it has more than one (AIUpdateInterface::m_locomotorSet: a ground and a
// cliff locomotor, in the set's order), and which of them it moves on now (m_curLocomotor). Units whose set has one
// locomotor carry none.
export namespace engine::gameplay
{
struct LocomotorChoice
{
	static constexpr std::size_t MaxOptions = 2; // no shipped set has more
	std::array<LocomotorDefinition, MaxOptions> options{};
	std::uint8_t count{0};
	std::uint8_t current{0};
	std::uint8_t reserved[6]{}; // no padding: checkpoints hold its bytes
};

// AIUpdateInterface::chooseLocomotorSetExplicit: the set's locomotors (beyond MaxOptions none are kept), the first in use.
inline LocomotorChoice MakeLocomotorChoice(std::span<const LocomotorDefinition *const> set) noexcept
{
	LocomotorChoice choice;
	for (const LocomotorDefinition *definition : set)
		if (definition != nullptr && choice.count < LocomotorChoice::MaxOptions)
			choice.options[choice.count++] = *definition;
	return choice;
}

// LocomotorSet::getValidSurfaces: every surface any of its locomotors moves over.
inline std::uint8_t SetSurfaces(const LocomotorChoice &choice) noexcept
{
	std::uint8_t surfaces = 0;
	for (std::size_t index = 0; index < choice.count; ++index)
		surfaces = static_cast<std::uint8_t>(surfaces | choice.options[index].surfaces);
	return surfaces;
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::LocomotorChoice>
{
	static constexpr std::string_view StableName = "engine.gameplay.locomotor_choice";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::LocomotorChoice &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.count) | static_cast<std::uint64_t>(value.current) << 8);
		for (std::size_t index = 0; index < value.count; ++index)
			hasher.AppendU64(static_cast<std::uint64_t>(value.options[index].maxSpeed.Raw()) ^
				(static_cast<std::uint64_t>(value.options[index].surfaces) << 56));
	}
};
}
