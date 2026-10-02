export module games.generalszh.gameplay.effects.components.radius_decal;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// An object's radius decal (the original's RadiusDecal held by RadiusDecalUpdate, DeliverPayloadAIUpdate or
// NeutronMissileUpdate): a RadiusDecalLooks look laid at `at` with a radius, made for the player controlling the object
// then (its colour, and who sees it). `look` None: none now (clear). Until:
//   NoLongerAttacking: a RadiusDecalUpdate's killWhenNoLongerAttacking (an AttackNugget's): once its object is no
//     longer attacking (or dies);
//   HeadsOffMap: a payload carrier's, until it heads off the map (HeadOffMapState::onEnter's killDeliveryDecal);
//   Dies: a neutron missile's, until it detonates or dies.
// Every one also goes with its object. Simulation state (the original saves it): hashed and checkpointed.
export namespace generalszh::gameplay
{
enum class RadiusDecalUntil : std::uint8_t
{
	ObjectGoes,
	NoLongerAttacking,
	HeadsOffMap,
	Dies,
};

struct RadiusDecal
{
	static constexpr std::uint32_t None = 0xFFFFFFFFu;
	Engine::Math::FixedVector3 at;
	Engine::Math::Fixed radius;
	std::uint32_t look{None};
	std::uint32_t player{0};
	RadiusDecalUntil until{RadiusDecalUntil::ObjectGoes};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::RadiusDecal>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.radius_decal";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
