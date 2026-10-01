export module games.generalszh.presentation.objects.components.effect_attachments;
import std;

import engine.ecs.core.component_registry;

// Particle systems riding on an object, as side tables on the simulation's
// own entities: the systems its model state carries at its bones (flames,
// lights, exhausts), and a shot-down helicopter's smoke trail. Each system
// sits at a place in the object's frame and follows it.
export namespace generalszh::presentation
{
struct AttachedSystem
{
	std::uint64_t id{0};           // in the particle world
	std::array<float, 3> local{};  // in the object's frame (unscaled)
	float yaw{0.0f};
	std::int32_t bone{-1};         // its ParticleSysBone (index in its state's list) when it sits at a bone
};

// A missile's exhaust (MissileAIUpdate: lit at ignition, tossed when its fuel runs out or it is done),
// sampled each tick: its weapon (which names the exhaust) and whether it burns.
struct ExhaustState
{
	std::uint32_t weapon{0};
	std::uint32_t lit{0};
	std::uint8_t veterancy{0}; // its launcher's (the exhaust for that level: VeterancyProjectileExhaust)
	std::uint8_t reserved[7]{};
};

// The exhaust trailing it in the particle world (0: none yet).
struct ExhaustEmission
{
	std::uint64_t id{0};
	std::uint32_t seenFrame{0};
	std::uint32_t tossed{0}; // stopped: what is out lives on
};

struct ConditionEmission
{
	static constexpr std::uint32_t NoLook = 0xFFFFFFFFu;
	std::vector<AttachedSystem> systems;
	std::uint32_t look{NoLook}; // the look whose systems run (NoLook: none started yet)
	std::uint32_t seenFrame{0};
};

// The effects of its body's damage state (TransitionDamageFX) riding on it.
struct DamageEmission
{
	std::vector<AttachedSystem> systems;
	std::uint32_t level{0}; // the damage level shown (DamageLevel)
	std::uint32_t known{0}; // a level has been seen
	std::uint32_t seenFrame{0};
};

struct CrashTrailEmission
{
	std::vector<AttachedSystem> systems;
	std::uint32_t started{0};
	std::uint32_t seenFrame{0};
};

// A firestorm's particle systems (FirestormDynamicGeometryInfoUpdate: started where it stands, ParticleOffsetZ over the
// ground, as its effects fire; each frame their emission radius is its radius).
struct FirestormEmission
{
	std::vector<std::uint64_t> systems;
};

// Its BoneFXUpdate's particle systems (BoneFXUpdate::doParticleSystemAtBone: started at their bones, riding on it) and
// their timers, on the tick the simulation keeps and the presentation's random stream (GameClientRandomVariable): for
// each slot of its damage state the tick it starts its system next (Off: not at all); the simulation's timings and stops
// last followed.
struct BoneFxEmission
{
	static constexpr std::int64_t Off = -1;
	std::vector<AttachedSystem> systems;
	std::array<std::int64_t, 8> next{Off, Off, Off, Off, Off, Off, Off, Off};
	std::uint32_t timings{0};
	std::uint32_t stops{0};
	std::uint32_t seenFrame{0};
};

// Systems its FX lists attached to it (AttachToObject: a dying buggy's
// debris trail), riding on it until it goes.
struct FxEmission
{
	std::vector<AttachedSystem> systems;
	std::uint32_t seenFrame{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::ConditionEmission>
{
	static constexpr std::string_view StableName = "generalszh.presentation.condition_emission";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::ExhaustState>
{
	static constexpr std::string_view StableName = "generalszh.presentation.exhaust_state";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::ExhaustEmission>
{
	static constexpr std::string_view StableName = "generalszh.presentation.exhaust_emission";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::DamageEmission>
{
	static constexpr std::string_view StableName = "generalszh.presentation.damage_emission";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::BoneFxEmission>
{
	static constexpr std::string_view StableName = "generalszh.presentation.bone_fx_emission";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::FxEmission>
{
	static constexpr std::string_view StableName = "generalszh.presentation.fx_emission";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::FirestormEmission>
{
	static constexpr std::string_view StableName = "generalszh.presentation.firestorm_emission";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::CrashTrailEmission>
{
	static constexpr std::string_view StableName = "generalszh.presentation.crash_trail_emission";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
