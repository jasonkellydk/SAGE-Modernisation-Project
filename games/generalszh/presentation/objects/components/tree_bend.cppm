export module games.generalszh.presentation.objects.components.tree_bend;
import std;

export import engine.ecs.core.component_registry;

// A map tree knocked about on the client (the tree buffer's TTree topple and
// push-aside state): whether it stands, is falling, lies down or has sunk
// away; the rotation its fall has built up (row-major 3x3, applied about its
// base), the way it falls, its angular speed, acceleration and how far it
// has turned; how far it leans out of a passing unit's way (0..1), which way
// and how fast that changes, who pushed it and on which tick; and how far it
// has sunk and how many logic frames of sinking are left.
export namespace generalszh::presentation
{
namespace tree_bend_state
{
inline constexpr std::uint8_t Upright = 0;
inline constexpr std::uint8_t Falling = 1;
inline constexpr std::uint8_t Down = 2;
inline constexpr std::uint8_t Gone = 3; // sunk away: no longer drawn
}

struct TreeBend
{
	std::array<float, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
	std::array<float, 2> direction{};
	float angularVelocity{0.0f};
	float angularAcceleration{0.0f};
	float accumulation{0.0f};
	float pushAside{0.0f};
	float pushDelta{0.0f};
	float pushCos{1.0f};
	float pushSin{1.0f};
	float sunk{0.0f};
	float sinkFramesLeft{0.0f};
	std::uint64_t pushSource{~std::uint64_t{0}};
	std::uint32_t lastPushTick{0};
	std::uint8_t state{tree_bend_state::Upright};
	std::uint8_t reserved[3]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::TreeBend>
{
	static constexpr std::string_view StableName = "generalszh.presentation.tree_bend";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
