export module games.generalszh.gameplay.powers.components.spectre_gunship;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
export import games.generalszh.content.global.radius_decal;
import engine.ecs.system.system;

// The Spectre gunship's attack (the original's SpectreGunshipUpdate): on its way in from the map's edge, circling the
// target area (the orbit a point OrbitInsertionSlope of the way from straight at the target to a quarter turn round,
// GunshipOrbitRadius out), then leaving along its heading; the area's centre, the reticle the player drives (kept
// within AttackAreaRadius less TargetingReticleRadius of it), where its gattling's fire walks and where it walks to,
// when it leaves, the howitzer's count of steady gattling ticks, and its gattling (in its HelixContain).
export namespace generalszh::gameplay
{
enum class GunshipStatus : std::uint8_t
{
	Idle,
	Inserting,
	Orbiting,
	Departing,
};

struct SpectreGunship
{
	Engine::Math::FixedVector3 initialTarget;
	Engine::Math::FixedVector3 reticle;       // m_overrideTargetDestination
	Engine::Math::FixedVector3 gattlingTarget;
	Engine::Math::FixedVector3 shootAt;        // m_positionToShootAt
	std::uint64_t orbitEscapeTick{0};
	ecs::Entity gattling;
	std::uint32_t steadyTicks{0}; // m_okToFireHowitzerCounter
	GunshipStatus status{GunshipStatus::Idle};
	std::uint8_t reserved[3]{}; // no padding: checkpoints hold its bytes
};

// Per kind of gunship (SpectreGunshipUpdate): its power, its gattling (a definition name), the howitzer (a weapon
// index; none: 0xFFFFFFFF), its times (ticks) and distances.
struct SpectreGunshipConfig
{
	bool present{false};
	std::uint32_t power{0xFFFFFFFFu};
	std::string gattling;
	std::uint32_t howitzer{0xFFFFFFFFu};
	std::uint64_t howitzerFiringTicks{1};
	std::uint64_t orbitTicks{0};
	std::uint64_t howitzerFollowLag{0};
	Engine::Math::Fixed attackAreaRadius;
	Engine::Math::Fixed strafingIncrement;
	Engine::Math::Fixed orbitInsertionSlope;
	Engine::Math::Fixed randomOffset;
	Engine::Math::Fixed reticleRadius;
	Engine::Math::Fixed orbitRadius;
	// AttackAreaDecal and TargetingReticleDecal (the presentation's, laid while it inserts and orbits).
	content::RadiusDecalLook attackAreaDecal;
	content::RadiusDecalLook reticleDecal;
};

// Per kind of building that calls one in (SpectreGunshipDeploymentUpdate): its power, the gunship (a definition name)
// and where it comes in from (CreateLocation).
enum class GunshipEntry : std::uint8_t
{
	EdgeNearSource,
	EdgeFarthestFromSource,
	EdgeNearTarget,
	EdgeFarthestFromTarget,
};

struct SpectreDeploymentConfig
{
	bool present{false};
	std::uint32_t power{0xFFFFFFFFu};
	std::string gunship;
	GunshipEntry entry{GunshipEntry::EdgeFarthestFromTarget};
	// RequiredScience (doesSpecialPowerUpdatePassScienceTest: the Airforce general's three command centre modules, one
	// per gunship level); none: 0xFFFFFFFF.
	std::uint32_t requiredScience{0xFFFFFFFFu};
};

// What the tick decided, carried out after it through the AI's orders (CMD_FROM_AI).
enum class GunshipOrder : std::uint8_t
{
	ShipMove,          // the gunship aiMoveToPosition(at)
	ShipSet,           // the gunship chooseLocomotorSet(set)
	GattlingAttack,    // the gattling aiAttackObject(target, 9999)
	GattlingAttackAt,  // the gattling aiAttackPosition(at, 9999)
	GattlingParalyzed, // the gattling DISABLED_PARALYZED on (set) or off
	Howitzer,          // createAndFireTempWeapon(howitzer, gunship, at)
	DestroyGattling,
	DestroyShip,
};

struct GunshipEvent
{
	ecs::Entity gunship;
	GunshipOrder order{GunshipOrder::ShipMove};
	std::uint8_t set{0};
	ecs::Entity target;
	Engine::Math::FixedVector3 at;
};

struct GunshipEvents
{
	std::vector<GunshipEvent> list;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::SpectreGunship>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.spectre_gunship";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<generalszh::gameplay::GunshipEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.gunship_events";
};
}
