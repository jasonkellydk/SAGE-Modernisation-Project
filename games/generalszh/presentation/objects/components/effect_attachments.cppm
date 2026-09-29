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
};

// A missile's exhaust (MissileAIUpdate: lit at ignition, tossed when its fuel runs out or it is done),
// sampled each tick: its weapon (which names the exhaust) and whether it burns.
struct ExhaustState
{
	std::uint32_t weapon{0};
	std::uint32_t lit{0};
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
struct ComponentTraits<generalszh::presentation::FxEmission>
{
	static constexpr std::string_view StableName = "generalszh.presentation.fx_emission";
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
