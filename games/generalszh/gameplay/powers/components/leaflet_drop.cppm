export module games.generalszh.gameplay.powers.components.leaflet_drop;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// A leaflet drop's container (the original's LeafletDropBehavior): whether its leaflets have been started (m_fxFired).
// LeafletDropConfig: its definition's DisabledDuration (ticks), AffectRadius and LeafletFXParticleSystem (Delay is read
// but its update never runs past it: see leaflet_drops). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct LeafletDrop
{
	std::uint32_t fxFired{0};
};

struct LeafletDropConfig
{
	bool present{false};
	std::uint64_t durationTicks{0};
	Engine::Math::Fixed radius;
	std::string particles;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::LeafletDrop>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.leaflet_drop";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
