export module games.generalszh.session.composition.flight_deck;
import std;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.spatial.systems.snapshot_system;
import engine.gameplay.rts.combat.systems.weapon_bonus_retime_system;
import engine.gameplay.rts.death.systems.slow_death_system;
import engine.gameplay.rts.death.systems.structure_topple_system;
import engine.gameplay.rts.loadout.systems.loadout_system;
import games.generalszh.gameplay.appearance.systems.steering_look_system;
import games.generalszh.gameplay.flight_deck.systems.flight_deck_systems;
import games.generalszh.gameplay.flight_deck.components.flight_deck;

// The flight deck domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The flight deck domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceFlightDeckResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::DeckJets>();
	world.EmplaceResource<generalszh::gameplay::FlightDeckEvents>();
}

inline void RegisterFlightDeckComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::FlightDeck>();
}

// The flight deck domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterFlightDeckSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::DeckRosterSystem deckRoster;
	registry.Register(deckRoster);
	static generalszh::gameplay::FlightDeckSystem flightDecks;
	registry.Register(flightDecks);
}

// What the flight deck domain's systems run after (and the few they must precede), within the tick.
inline void OrderFlightDeckSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	// The flight decks, after the jets have moved.
	registry.OrderBefore<gameplay::StructureToppleSystem, domain::DeckRosterSystem>();
	registry.OrderBefore<gameplay::SlowDeathSystem, domain::DeckRosterSystem>();
	registry.OrderBefore<gameplay::WeaponBonusRetimeSystem, domain::DeckRosterSystem>();
	registry.OrderBefore<gameplay::LoadoutSystem, domain::DeckRosterSystem>();
	registry.OrderBefore<gameplay::SnapshotSystem, domain::FlightDeckSystem>();
	registry.OrderBefore<domain::SteeringLookSystem, domain::FlightDeckSystem>();
	registry.OrderBefore<domain::DeckRosterSystem, domain::FlightDeckSystem>();
}
}
