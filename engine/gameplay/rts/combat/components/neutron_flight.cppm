export module engine.gameplay.rts.combat.components.neutron_flight;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
import engine.ecs.system.system;

// A superweapon missile flying itself (the original's NeutronMissileUpdate): out of its launcher, up along a set
// climb for a while, straight on for a distance, then turning (at most MaxTurnRate a tick) for a point above its
// target and, reached, straight down onto it at half speed; each tick it pushes along its heading by RelativeSpeed
// against ForwardDamping of its speed; it goes off (dies) on reaching the ground. Its effects are the game's
// (DeathEffectKind::Effect ids; none: 0xFFFFFFFF).
export namespace engine::gameplay
{
enum class NeutronState : std::uint8_t
{
	Launch,
	Attack,
	Dead,
};

struct NeutronFlight
{
	Engine::Math::FixedVector3 target;
	Engine::Math::FixedVector3 intermediate; // the point TargetFromDirectlyAbove over its target
	Engine::Math::FixedVector3 velocity;     // per tick
	Engine::Math::FixedVector3 forward;      // its heading (unit)
	Engine::Math::Fixed noTurnLeft;          // m_noTurnDistLeft
	Engine::Math::Fixed heightAtLaunch;
	std::uint64_t launchTick{0};
	ecs::Entity launcher;
	std::uint32_t weapon{0}; // the weapon that fired it (its WeaponDefinition::neutron)
	NeutronState state{NeutronState::Launch};
	std::uint8_t reached{1};  // m_reachedIntermediatePos
	std::uint8_t reserved[2]{}; // no padding: checkpoints hold its bytes
};

// The tick's missile effects (LaunchFX as it leaves, IgnitionFX as it lights), for the presentation.
struct NeutronEffect
{
	std::uint32_t effect{0xFFFFFFFFu};
	ecs::Entity missile;
	Engine::Math::FixedVector3 at;
};

struct NeutronEffects
{
	std::vector<NeutronEffect> played;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::NeutronFlight>
{
	static constexpr std::string_view StableName = "engine.gameplay.neutron_flight";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<engine::gameplay::NeutronEffects>
{
	static constexpr std::string_view StableName = "engine.gameplay.neutron_effects";
};
}
