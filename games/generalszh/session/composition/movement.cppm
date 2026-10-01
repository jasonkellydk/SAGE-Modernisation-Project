export module games.generalszh.session.composition.movement;
import std;
import engine.gameplay.rts.movement.resources.movement_penalty;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.docking.systems.dock_system;
import engine.gameplay.rts.navigation.systems.obstacle_system;
import games.generalszh.gameplay.abilities.systems.command_button_hunt_system;
import games.generalszh.gameplay.ai.systems.repulsion_system;
import games.generalszh.gameplay.combat.systems.deploy_system;
import games.generalszh.gameplay.containment.systems.scripted_evacuation_system;
import games.generalszh.gameplay.hacking.systems.internet_hack_system;
import engine.gameplay.rts.movement.systems.descent_system;
import engine.gameplay.rts.movement.systems.face_target_system;
import engine.gameplay.rts.movement.systems.locomotor_damage_system;
import engine.gameplay.rts.movement.systems.move_path_system;
import engine.gameplay.rts.movement.systems.movement_system;
import engine.gameplay.rts.movement.systems.route_request_system;
import engine.gameplay.rts.movement.systems.wander_system;
import games.generalszh.gameplay.movement.systems.destination_adjust_system;
import games.generalszh.gameplay.movement.systems.goal_claim_system;
import engine.gameplay.rts.movement.components.descent;
import engine.gameplay.rts.movement.components.face_target;
import engine.gameplay.rts.movement.components.floor_lift;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.rts.movement.components.move_ended;
import engine.gameplay.rts.movement.components.move_goal;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.movement.components.move_path;
import engine.gameplay.rts.movement.components.path_completed;
import engine.gameplay.rts.movement.components.wander_anchor;
import engine.gameplay.rts.movement.components.wanderer;

// The movement domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The movement domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceMovementResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	// Hurt to GameData's MovementPenaltyDamageState, a body moves on its damaged rates (ActiveBody::calcDamageState: at
	// or below the state's threshold; RUBBLE: out of health; PRISTINE: always).
	const auto &globals = setup.content.gameData;
	const std::array<Engine::Math::Fixed, 4> ratios{Engine::Math::Fixed{}, globals.unitDamaged, globals.unitReallyDamaged, Engine::Math::Fixed{}};
	world.EmplaceResource<engine::gameplay::MovementPenalty>(
		engine::gameplay::MovementPenalty{ratios[std::min<std::uint32_t>(globals.movementPenaltyState, 3u)], globals.movementPenaltyState == 0});
}

inline void RegisterMovementComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::PathCompleted>();
	world.RegisterComponent<engine::gameplay::MoveEnded>();
	world.RegisterComponent<engine::gameplay::Locomotion>();
	world.RegisterComponent<engine::gameplay::MoveOrder>();
	world.RegisterComponent<engine::gameplay::MoveGoal>();
	world.RegisterComponent<engine::gameplay::Wanderer>();
	world.RegisterComponent<engine::gameplay::WanderAnchor>();
	world.RegisterComponent<engine::gameplay::FaceTarget>();
	world.RegisterComponent<engine::gameplay::MovePath>();
	world.RegisterComponent<engine::gameplay::Descent>();
	world.RegisterComponent<engine::gameplay::FloorLift>();
}

// The movement domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterMovementSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::DescentSystem descent;
	registry.Register(descent);
	static engine::gameplay::MovementSystem movement;
	registry.Register(movement);
	static engine::gameplay::RouteRequestSystem routes;
	registry.Register(routes);
	static generalszh::gameplay::DestinationAdjustSystem destinationAdjust;
	registry.Register(destinationAdjust);
	static generalszh::gameplay::GoalClaimSystem goalClaims;
	registry.Register(goalClaims);
	static engine::gameplay::LocomotorDamageSystem locomotorDamage;
	registry.Register(locomotorDamage);
	static engine::gameplay::WanderSystem wanderers;
	registry.Register(wanderers);
	static engine::gameplay::MovePathSystem movePaths;
	registry.Register(movePaths);
	static engine::gameplay::FaceTargetSystem faceTargets;
	registry.Register(faceTargets);
}

// What the movement domain's systems run after (and the few they must precede), within the tick.
inline void OrderMovementSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::ObstacleSystem, gameplay::DescentSystem>();
	registry.OrderBefore<gameplay::RouteRequestSystem, gameplay::MovementSystem>();
	// Goal claims: each new move's destination adjusted off others' claims before its route is planned; claims
	// follow the routes planned and the moves ended.
	registry.OrderBefore<domain::DestinationAdjustSystem, gameplay::RouteRequestSystem>();
	registry.OrderBefore<domain::GoalClaimSystem, domain::DestinationAdjustSystem>();
	registry.OrderBefore<domain::InternetHackSystem, gameplay::MovementSystem>();
	registry.OrderBefore<domain::RepulsionSystem, gameplay::WanderSystem>();
	registry.OrderBefore<domain::RepulsionSystem, gameplay::MovePathSystem>();
	registry.OrderBefore<gameplay::FaceTargetSystem, gameplay::WanderSystem>();
	registry.OrderBefore<gameplay::FaceTargetSystem, gameplay::MovePathSystem>();
	registry.OrderBefore<gameplay::FaceTargetSystem, gameplay::MovementSystem>();
	registry.OrderBefore<gameplay::WanderSystem, gameplay::MovementSystem>();
	registry.OrderBefore<gameplay::MovePathSystem, gameplay::MovementSystem>();
	registry.OrderBefore<gameplay::WanderSystem, gameplay::MovePathSystem>();
	registry.OrderBefore<gameplay::DockSystem, domain::GoalClaimSystem>();
	registry.OrderBefore<gameplay::LocomotorDamageSystem, gameplay::MovementSystem>();
	registry.OrderBefore<domain::ScriptedEvacuationSystem, gameplay::LocomotorDamageSystem>();
	registry.OrderBefore<domain::ScriptedEvacuationSystem, gameplay::FaceTargetSystem>();
	registry.OrderBefore<domain::ScriptedEvacuationSystem, gameplay::WanderSystem>();
	registry.OrderBefore<domain::ScriptedEvacuationSystem, gameplay::MovePathSystem>();
	registry.OrderBefore<domain::CommandButtonHuntSystem, gameplay::WanderSystem>();
	registry.OrderBefore<domain::CommandButtonHuntSystem, gameplay::MovePathSystem>();
	registry.OrderBefore<domain::DeploySystem, gameplay::MovementSystem>();
	registry.OrderBefore<gameplay::DockSystem, gameplay::RouteRequestSystem>();
}
}
