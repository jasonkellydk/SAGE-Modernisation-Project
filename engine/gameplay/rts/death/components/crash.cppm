export module engine.gameplay.rts.death.components.crash;
import std;
export import engine.ecs.core.entity;

export import Engine.Core.Math.FixedVector;
import engine.ecs.core.component_registry;

// A crash under way (see CrashDefinition): how the wreck moves (per tick; it
// falls by `fall` per tick squared), when it died and hit the ground (0: not
// yet), and its spin. A spiralling helicopter also keeps its heading and
// speed along the spiral, its spin about itself (and whether that is
// winding down), when its spin last changed and when its blades fly off.
export namespace engine::gameplay
{
namespace crash_flag
{
inline constexpr std::uint32_t SecondaryDone = 1u << 0;
inline constexpr std::uint32_t SpinDown = 1u << 1;
inline constexpr std::uint32_t BladesGone = 1u << 2;
}

struct Crash
{
	Engine::Math::FixedVector3 velocity;
	Engine::Math::Fixed fall;
	Engine::Math::Fixed forwardSpeed;
	std::uint64_t deathTick{0};
	std::uint64_t groundTick{0};
	std::uint64_t bladeTick{0};
	std::uint64_t spinTick{0};
	std::int32_t rollRate{0};
	std::int32_t selfSpin{0};
	std::uint32_t forwardAngle{0}; // turn units
	std::uint32_t flags{0};
	// The last body a spiralling helicopter ran into (PhysicsBehavior::m_lastCollidee; see CrashCollisionSystem).
	ecs::Entity lastCollidee;

	bool Has(std::uint32_t flag) const noexcept { return (flags & flag) != 0; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Crash>
{
	static constexpr std::string_view StableName = "engine.gameplay.crash";
	static constexpr std::uint32_t Version = 3;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
