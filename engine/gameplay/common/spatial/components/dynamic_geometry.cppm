export module engine.gameplay.common.spatial.components.dynamic_geometry;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// A body whose size changes over its life (the original's DynamicGeometryInfoUpdate: a firestorm): after its initial
// delay (at least a tick) its height and radii go from its initial to its final ones over TransitionTime ticks, set each
// tick (setGeometryInfo), and once past it either stop or (ReverseAtTransitionTime, once) go back the other way. The
// current size is `height`, `major`, `minor` (its shape is kept: `shape` 0 sphere, 1 cylinder, 2 box). Simulation
// state: checkpointed.
export namespace engine::gameplay
{
namespace geometry_shape
{
inline constexpr std::uint8_t Sphere = 0;
inline constexpr std::uint8_t Cylinder = 1;
inline constexpr std::uint8_t Box = 2;
}

struct DynamicGeometry
{
	std::uint32_t delayLeft{1};      // m_startingDelayCountdown
	std::uint32_t timeActive{0};     // m_timeActive
	std::uint32_t transitionTime{1}; // TransitionTime (ticks)
	std::uint8_t started{0};
	std::uint8_t finished{0};
	std::uint8_t reverse{0};  // m_reverseAtTransitionTime (still to happen)
	std::uint8_t switched{0}; // m_switchedDirections
	std::uint8_t shape{geometry_shape::Cylinder};
	std::uint8_t reserved[7]{};
	Engine::Math::Fixed initialHeight, initialMajor, initialMinor;
	Engine::Math::Fixed finalHeight, finalMajor, finalMinor;
	Engine::Math::Fixed height, major, minor;

	// GeometryInfo::getBoundingCircleRadius / getBoundingSphereRadius of its current size.
	Engine::Math::Fixed CircleRadius() const noexcept;
	Engine::Math::Fixed SphereRadius() const noexcept;
};

inline Engine::Math::Fixed DynamicGeometry::CircleRadius() const noexcept
{
	if (shape == geometry_shape::Box)
		return Engine::Math::Sqrt(major * major + minor * minor);
	return major;
}

inline Engine::Math::Fixed DynamicGeometry::SphereRadius() const noexcept
{
	const Engine::Math::Fixed half = height / Engine::Math::Fixed::FromInt(2);
	if (shape == geometry_shape::Cylinder)
		return half > major ? half : major;
	if (shape == geometry_shape::Box)
		return Engine::Math::Sqrt(major * major + minor * minor + half * half);
	return major;
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DynamicGeometry>
{
	static constexpr std::string_view StableName = "engine.gameplay.dynamic_geometry";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
