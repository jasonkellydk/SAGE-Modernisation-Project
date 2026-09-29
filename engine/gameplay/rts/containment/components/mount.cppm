export module engine.gameplay.rts.containment.components.mount;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

// A thing mounted on another (the original's portable structures: an Overlord's or Helix's gattling cannon, propaganda
// tower or battle bunker, contained by it but drawn and fighting on top of it). The carrier holds one (Mount: HelixContain
// m_portableStructureID, OverlordContain's rider); the mounted one (Mounted) rides at its carrier's position and facing
// (HelixContain::update), off the map for everything but its own fighting (it is armed), and goes with it: killed as it
// dies (HelixContain / OverlordContain::onDie), gone as it is removed (onDelete). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct Mount
{
	ecs::Entity rider;
};

struct Mounted
{
	ecs::Entity carrier;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Mount>
{
	static constexpr std::string_view StableName = "engine.gameplay.mount";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ComponentTraits<engine::gameplay::Mounted>
{
	static constexpr std::string_view StableName = "engine.gameplay.mounted";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
