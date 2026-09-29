export module engine.gameplay.rts.production.components.production_doors;
import std;

import engine.ecs.core.component_registry;

// A factory's doors (the original's ProductionUpdate door animations): what
// is done comes out only through an open door. A door opens over
// `openTicks`, stays open `waitTicks` after the last unit went through,
// then closes over `closeTicks`; each phase starts on the tick in its field
// (0: not in that phase). After its first unit the factory shows it has
// finished (`completeTick`) for `completeTicks`.
export namespace engine::gameplay
{
struct ProductionDoor
{
	std::uint64_t opening{0};
	std::uint64_t open{0};
	std::uint64_t closing{0};
};

struct ProductionDoors
{
	static constexpr std::uint32_t MaxDoors = 4;
	std::array<ProductionDoor, MaxDoors> doors{};
	std::uint64_t openTicks{0};
	std::uint64_t waitTicks{0};
	std::uint64_t closeTicks{0};
	std::uint64_t completeTicks{0};
	std::uint64_t completeTick{0};
	std::uint32_t count{0}; // doors that animate (0: units come straight out)
	std::uint32_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::ProductionDoors>
{
	static constexpr std::string_view StableName = "engine.gameplay.production_doors";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
