export module engine.gameplay.rts.collision.components.body_collision;
import std;
export import engine.ecs.core.entity;

export import engine.ecs.core.component_registry;
import engine.ecs.system.system;

// How a body with physics answers running into others (PhysicsBehavior::onCollide): whether it has an AI (a unit's AI
// weighs in on its collisions; only a dead or parachuting one gets pushed, and only by what stands firm), whether it is
// pushed at all (AllowCollideForce), whether it is a vehicle (it fires its crash weapon falling into things: VehicleCrashesInto(Non)BuildingWeaponTemplate), and
// those weapons.
export namespace engine::gameplay
{
namespace body_collision_flag
{
inline constexpr std::uint8_t HasAI = 1u << 0;
inline constexpr std::uint8_t NoForce = 1u << 1;
inline constexpr std::uint8_t Vehicle = 1u << 2;
}

struct BodyCollision
{
	std::uint32_t buildingCrashWeapon{0xFFFFFFFFu};
	std::uint32_t otherCrashWeapon{0xFFFFFFFFu};
	// What it passes through (setIgnoreCollisionsWith: debris thrown clear of what made it, IgnorePrimaryObstacle).
	ecs::Entity ignored;
	std::uint8_t flags{0};
	std::uint8_t reserved[7]{};

	bool Has(std::uint8_t flag) const noexcept { return (flags & flag) != 0; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::BodyCollision>
{
	static constexpr std::string_view StableName = "engine.gameplay.body_collision";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
