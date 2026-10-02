export module games.generalszh.session.composition.wave_guides;
import std;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.topple.systems.topple_system;
import games.generalszh.gameplay.abilities.systems.special_ability_system;
import games.generalszh.gameplay.waveguide.components.wave_guide;
import games.generalszh.gameplay.waveguide.resources.wave_guide_events;
import games.generalszh.gameplay.waveguide.systems.wave_guide_system;

// The flood waves (WaveGuideUpdate): their component registered with the world, their resources (what the tick's waves
// leave for the session and the presentation), their system and its place in the tick.
export namespace generalszh::session::composition
{
inline void EmplaceWaveGuidesResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::WaveGuideEvents>();
	world.EmplaceResource<generalszh::gameplay::WaveGuideCues>();
}

inline void RegisterWaveGuidesComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::WaveGuide>();
}

inline void RegisterWaveGuidesSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::WaveGuideSystem waveGuides;
	registry.Register(waveGuides);
}

// The waves update once the tick's units have moved (the last mover: the special abilities), and push their victims over
// before the toppling (which comes before the tick's damage: their pending damage is dealt this tick).
inline void OrderWaveGuidesSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<domain::SpecialAbilitySystem, domain::WaveGuideSystem>();
	registry.OrderBefore<domain::WaveGuideSystem, gameplay::ToppleSystem>();
}
}
