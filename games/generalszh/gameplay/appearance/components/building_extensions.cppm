export module games.generalszh.gameplay.appearance.components.building_extensions;
import std;

export import engine.ecs.core.component_registry;

// A building's parts that extend once it is upgraded: its radar dish
// (RadarUpdate: RADAR_EXTENDING, then RADAR_UPGRADED the tick after it is done)
// and its control rods (PowerPlantUpdate: POWER_PLANT_UPGRADING, then
// POWER_PLANT_UPGRADED the tick it is done).
export namespace generalszh::gameplay
{
enum class ExtensionState : std::uint8_t
{
	Retracted,
	Extending,
	Extended,
};

struct RadarDish
{
	std::uint64_t ticks{0};
	std::uint64_t doneTick{0};
	ExtensionState state{ExtensionState::Retracted};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};

struct ControlRods
{
	std::uint64_t ticks{0};
	std::uint64_t doneTick{0};
	ExtensionState state{ExtensionState::Retracted};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::RadarDish>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.radar_dish";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::RadarDish &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.ticks);
		hasher.AppendU64(value.doneTick << 2 | static_cast<std::uint64_t>(value.state));
	}
};
template<>
struct ComponentTraits<generalszh::gameplay::ControlRods>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.control_rods";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::ControlRods &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.ticks);
		hasher.AppendU64(value.doneTick << 2 | static_cast<std::uint64_t>(value.state));
	}
};
}
