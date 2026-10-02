export module games.generalszh.gameplay.containment.components.rider_change;
import std;

export import engine.ecs.core.component_registry;

// A RiderChangeContain (the Combat Bike): the riders it may carry (their definitions, and the model condition, weapon
// set flags and object status bit each gives it; None: none), which one it shows (None: no rider), how long it takes to
// scuttle once its rider is out and the model condition it shows meanwhile, and the tick it began scuttling (0: not);
// each rider's command set (an ObjectTemplates command set id; None: none) and locomotor set (the game's numbering).
// Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct RiderChange
{
	static constexpr std::uint32_t None = 0xFFFFFFFFu;
	static constexpr std::size_t MaxRiders = 8;
	std::array<std::uint32_t, MaxRiders> definitions{};
	std::array<std::uint32_t, MaxRiders> conditions{};
	std::array<std::uint32_t, MaxRiders> weaponFlags{};
	std::array<std::uint32_t, MaxRiders> statuses{};
	std::uint32_t count{0};
	std::uint32_t current{None};
	std::uint64_t scuttleTicks{0};
	std::uint64_t scuttledTick{0};
	std::uint32_t scuttleCondition{None};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
	std::array<std::uint32_t, MaxRiders> commandSets{};
	std::array<std::uint8_t, MaxRiders> locomotorSets{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::RiderChange>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.rider_change";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
