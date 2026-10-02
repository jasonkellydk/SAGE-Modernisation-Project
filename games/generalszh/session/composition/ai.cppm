export module games.generalszh.session.composition.ai;
import std;
import games.generalszh.gameplay.ai.resources.retaliation_modes;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.spatial.systems.spatial_index_system;
import engine.gameplay.rts.combat.systems.firing_tracker_system;
import engine.gameplay.rts.combat.systems.neutron_flight_system;
import engine.gameplay.rts.emp.systems.emp_pulse_system;
import engine.gameplay.rts.movement.systems.face_target_system;
import games.generalszh.gameplay.abilities.systems.command_button_hunt_system;
import games.generalszh.gameplay.combat.systems.cleanup_hazard_system;
import games.generalszh.gameplay.containment.systems.scripted_evacuation_system;
import games.generalszh.gameplay.hacking.systems.internet_hack_system;
import games.generalszh.gameplay.railroad.systems.railroad_system;
import games.generalszh.gameplay.ai.systems.attack_squad_system;
import games.generalszh.gameplay.ai.systems.guard_system;
import games.generalszh.gameplay.ai.systems.mob_member_system;
import games.generalszh.gameplay.ai.systems.repulsion_system;
import games.generalszh.gameplay.ai.systems.tunnel_guard_system;
import games.generalszh.gameplay.ai.components.attack_squad;
import games.generalszh.gameplay.ai.components.guard;
import games.generalszh.gameplay.ai.components.mob_member;
import games.generalszh.gameplay.ai.components.repulsion;
import games.generalszh.gameplay.ai.components.tunnel_guard;
import games.generalszh.gameplay.ai.components.team_path_follow;
import games.generalszh.gameplay.ai.systems.team_path_follow_system;
import games.generalszh.gameplay.ai.components.exact_path_follow;
import games.generalszh.gameplay.ai.systems.exact_path_follow_system;
import engine.gameplay.rts.movement.systems.movement_system;
import engine.gameplay.common.status.systems.disable_systems;
import engine.gameplay.rts.death.systems.death_system;
import engine.gameplay.rts.death.systems.height_die_system;
import engine.gameplay.rts.death.systems.slow_death_system;
import engine.gameplay.rts.death.systems.structure_topple_system;
import engine.gameplay.rts.lifecycle.systems.removal_system;
import engine.gameplay.rts.parachute.systems.parachute_systems;

// The ai domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The ai domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceAiResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::RetaliationModes>();
	world.EmplaceResource<generalszh::gameplay::MobEvents>();
	world.EmplaceResource<generalszh::gameplay::TeamWaypoints>();
	world.EmplaceResource<generalszh::gameplay::TeamPathEvents>();
}

inline void RegisterAiComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::TunnelGuard>();
	world.RegisterComponent<generalszh::gameplay::Guard>();
	world.RegisterComponent<generalszh::gameplay::Repulsable>();
	world.RegisterComponent<generalszh::gameplay::RepulsorMark>();
	world.RegisterComponent<generalszh::gameplay::AttackSquad>();
	world.RegisterComponent<generalszh::gameplay::MobMember>();
	world.RegisterComponent<generalszh::gameplay::TeamPathFollow>();
	world.RegisterComponent<generalszh::gameplay::ExactPathFollow>();
}

// The ai domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterAiSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::TunnelGuardSystem tunnelGuards;
	registry.Register(tunnelGuards);
	static generalszh::gameplay::GuardSystem guards;
	registry.Register(guards);
	static generalszh::gameplay::RepulsionSystem repulsion;
	registry.Register(repulsion);
	static generalszh::gameplay::AttackSquadSystem attackSquads;
	registry.Register(attackSquads);
	static generalszh::gameplay::MobMemberSystem mobMembers;
	registry.Register(mobMembers);
	static generalszh::gameplay::TeamPathFollowSystem teamPathFollows;
	registry.Register(teamPathFollows);
	static generalszh::gameplay::ExactPathFollowSystem exactPathFollows;
	registry.Register(exactPathFollows);
}

// What the ai domain's systems run after (and the few they must precede), within the tick.
inline void OrderAiSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<domain::RailroadSystem, domain::TunnelGuardSystem>();
	registry.OrderBefore<domain::InternetHackSystem, domain::MobMemberSystem>();
	registry.OrderBefore<gameplay::FaceTargetSystem, domain::RepulsionSystem>();
	registry.OrderBefore<domain::CommandButtonHuntSystem, domain::AttackSquadSystem>();
	registry.OrderBefore<gameplay::EmpPulseSystem, domain::AttackSquadSystem>();
	registry.OrderBefore<domain::TunnelGuardSystem, domain::AttackSquadSystem>();
	registry.OrderBefore<gameplay::SpatialIndexSystem, domain::TunnelGuardSystem>();
	registry.OrderBefore<gameplay::FiringTrackerSystem, domain::RepulsionSystem>();
	registry.OrderBefore<gameplay::FiringTrackerSystem, domain::TunnelGuardSystem>();
	registry.OrderBefore<gameplay::NeutronFlightSystem, domain::MobMemberSystem>();
	registry.OrderBefore<domain::TunnelGuardSystem, domain::MobMemberSystem>();
	registry.OrderBefore<domain::CleanupHazardSystem, domain::GuardSystem>();
	registry.OrderBefore<domain::CleanupHazardSystem, domain::TunnelGuardSystem>();
	registry.OrderBefore<domain::GuardSystem, domain::TunnelGuardSystem>();
	registry.OrderBefore<gameplay::SpatialIndexSystem, domain::GuardSystem>();
	registry.OrderBefore<domain::GuardSystem, domain::AttackSquadSystem>();
	registry.OrderBefore<domain::ScriptedEvacuationSystem, domain::RepulsionSystem>();
	// Followers look once the tick's deaths, removals, falls and disablings are done (PostSimulation).
	registry.OrderBefore<gameplay::DisableExpirySystem, domain::TeamPathFollowSystem>();
	registry.OrderBefore<gameplay::DisableApplySystem, domain::TeamPathFollowSystem>();
	registry.OrderBefore<gameplay::DeathSystem, domain::TeamPathFollowSystem>();
	registry.OrderBefore<gameplay::HeightDieSystem, domain::TeamPathFollowSystem>();
	registry.OrderBefore<gameplay::SlowDeathSystem, domain::TeamPathFollowSystem>();
	registry.OrderBefore<gameplay::StructureToppleSystem, domain::TeamPathFollowSystem>();
	registry.OrderBefore<gameplay::RemovalSystem, domain::TeamPathFollowSystem>();
	registry.OrderBefore<gameplay::ParachuteSystem, domain::TeamPathFollowSystem>();
	registry.OrderBefore<gameplay::ParachuteLandingSystem, domain::TeamPathFollowSystem>();
	registry.OrderBefore<gameplay::ParachuteLossSystem, domain::TeamPathFollowSystem>();
	registry.OrderBefore<gameplay::FreeFallSystem, domain::TeamPathFollowSystem>();
	// An exact team follow's next waypoint is offset once movement has moved it on.
	registry.OrderBefore<gameplay::MovementSystem, domain::ExactPathFollowSystem>();
}
}
