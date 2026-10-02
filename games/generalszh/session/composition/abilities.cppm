export module games.generalszh.session.composition.abilities;
import std;
import games.generalszh.gameplay.abilities.resources.sticky_bomb_cues;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.spatial.systems.spatial_index_system;
import engine.gameplay.rts.combat.systems.missile_jam_system;
import engine.gameplay.rts.combat.systems.neutron_flight_system;
import engine.gameplay.rts.emp.systems.emp_pulse_system;
import engine.gameplay.rts.movement.systems.movement_system;
import engine.gameplay.rts.movement.systems.route_request_system;
import games.generalszh.gameplay.ai.systems.mob_member_system;
import games.generalszh.gameplay.ai.systems.repulsion_system;
import games.generalszh.gameplay.powers.systems.spectre_gunship_system;
import games.generalszh.gameplay.production.systems.drone_repair_system;
import games.generalszh.gameplay.abilities.systems.command_button_hunt_system;
import games.generalszh.gameplay.abilities.systems.special_ability_system;
import games.generalszh.gameplay.abilities.systems.sticky_bomb_system;
import games.generalszh.gameplay.abilities.components.command_button_hunt;
import games.generalszh.gameplay.abilities.components.special_abilities;
import games.generalszh.gameplay.abilities.components.special_object;
import games.generalszh.gameplay.abilities.components.ability_laser;
import games.generalszh.gameplay.abilities.components.sticky_bomb;

// The abilities domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The abilities domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceAbilitiesResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::StickyBombEvents>();
	world.EmplaceResource<generalszh::gameplay::StickyBombCues>();
}

inline void RegisterAbilitiesComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::SpecialAbilities>();
	world.RegisterComponent<generalszh::gameplay::CommandButtonHunt>();
	world.RegisterComponent<generalszh::gameplay::SpecialObject>();
	world.RegisterComponent<generalszh::gameplay::AbilityLaser>();
	world.RegisterComponent<generalszh::gameplay::StickyBomb>();
}

// The abilities domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterAbilitiesSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::SpecialAbilitySystem specialAbilities;
	registry.Register(specialAbilities);
	static generalszh::gameplay::StickyBombSystem stickyBombs;
	registry.Register(stickyBombs);
	static generalszh::gameplay::CommandButtonHuntSystem commandButtonHunts;
	registry.Register(commandButtonHunts);
}

// What the abilities domain's systems run after (and the few they must precede), within the tick.
inline void OrderAbilitiesSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::MovementSystem, domain::StickyBombSystem>();
	registry.OrderBefore<gameplay::MissileJamSystem, domain::StickyBombSystem>();
	registry.OrderBefore<domain::SpectreGunshipSystem, domain::StickyBombSystem>();
	registry.OrderBefore<domain::MobMemberSystem, domain::StickyBombSystem>();
	registry.OrderBefore<domain::DroneRepairSystem, domain::StickyBombSystem>();
	registry.OrderBefore<gameplay::NeutronFlightSystem, domain::StickyBombSystem>();
	registry.OrderBefore<domain::RepulsionSystem, domain::CommandButtonHuntSystem>();
	registry.OrderBefore<domain::CommandButtonHuntSystem, domain::SpecialAbilitySystem>();
	registry.OrderBefore<gameplay::SpatialIndexSystem, domain::CommandButtonHuntSystem>();
	registry.OrderBefore<gameplay::MovementSystem, domain::SpecialAbilitySystem>();
	registry.OrderBefore<gameplay::RouteRequestSystem, domain::SpecialAbilitySystem>();
	registry.OrderBefore<gameplay::EmpPulseSystem, domain::CommandButtonHuntSystem>();
}
}
