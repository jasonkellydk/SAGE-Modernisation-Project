export module games.generalszh.presentation.objects.components.vehicle_motion;
import std;

import engine.ecs.core.component_registry;

// A vehicle's look in motion, kept on the simulation's own entities as
// side-table components (presentation state: never in the archetypes, the
// state hash or checkpoints): how it moved between the last two ticks, how
// far its treads have rolled and its tires turned (on real frame time), and
// the emitters kicking up its debris and dust.
export namespace generalszh::presentation
{
struct MotionSample
{
	std::array<float, 3> position{}; // at the last tick
	float facing{0.0f};              // radians, at the last tick
	float speed{0.0f};               // distance a tick, between the last two ticks
	float along{0.0f};               // the same along its facing (negative: reversing)
	std::int32_t turn{0};            // signed turn units between the last two ticks (positive: left)
	std::uint32_t facingUnits{0};
	std::uint32_t dying{0};
	// PhysicsBehavior::isMotive: the ticks its locomotor's push still counts (MOTIVE_FRAMES, 10, from the last tick it
	// moved it); and getTurning: its locomotor turning it at full rate (+1 left, -1 right, 0 not).
	std::uint16_t motiveTicks{0};
	std::int8_t turning{0};
	std::uint8_t reserved{0};
};

struct TreadRoll
{
	std::array<float, 2> offsets{}; // left, right texture U offsets in [0, 1)
};

struct WheelRoll
{
	float angle{0.0f};   // how far the tires have turned (radians, in [-pi, pi)): the front (steered) ones
	float rearAngle{0.0f}; // the rear ones (faster while it powerslides)
	float steer{0.0f};   // the front wheels' steering (radians; positive: left)
	float cab{0.0f};     // the cab's swing
	float trailer{0.0f}; // the trailer's swing
};

struct MotionEmission
{
	static constexpr std::uint32_t MaxSystems = 4;
	std::array<std::uint64_t, MaxSystems> systems{}; // particle systems emitting
	std::array<std::uint8_t, MaxSystems> roles{};     // each one's content::MotionEmitterRole
	std::array<std::uint8_t, MaxSystems> sources{};   // each one's place in its look's motion systems
	std::uint32_t count{0};
	std::uint32_t powersliding{0};                    // its powerslide spray runs (W3DTruckDraw m_isPowersliding)
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::MotionSample>
{
	static constexpr std::string_view StableName = "generalszh.presentation.motion_sample";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::TreadRoll>
{
	static constexpr std::string_view StableName = "generalszh.presentation.tread_roll";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::WheelRoll>
{
	static constexpr std::string_view StableName = "generalszh.presentation.wheel_roll";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::MotionEmission>
{
	static constexpr std::string_view StableName = "generalszh.presentation.motion_emission";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
