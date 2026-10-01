export module games.generalszh.session.composition.combat;
import std;
import engine.gameplay.rts.combat.resources.assists;
import engine.gameplay.rts.combat.resources.garrison_kills;
import engine.gameplay.rts.combat.resources.historic_damage;
import engine.gameplay.rts.combat.resources.shots;
import games.generalszh.gameplay.combat.algorithms.battle_bus;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.healing.systems.healing_system;
import engine.gameplay.common.health.systems.health_system;
import engine.gameplay.common.health.systems.subdual_system;
import engine.gameplay.common.physics.systems.shock_wave_system;
import engine.gameplay.common.spatial.systems.spatial_index_system;
import engine.gameplay.rts.aircraft.systems.jet_system;
import engine.gameplay.rts.collision.systems.body_collision_system;
import engine.gameplay.rts.collision.systems.collide_weapon_system;
import engine.gameplay.rts.construction.systems.construction_system;
import engine.gameplay.rts.construction.systems.sale_system;
import engine.gameplay.rts.containment.systems.drop_homing_system;
import engine.gameplay.rts.containment.systems.garrison_clear_system;
import engine.gameplay.rts.containment.systems.passed_bonus_system;
import engine.gameplay.rts.containment.systems.unloading_system;
import engine.gameplay.rts.docking.systems.dock_system;
import engine.gameplay.rts.horde.systems.horde_system;
import engine.gameplay.rts.loadout.systems.loadout_system;
import engine.gameplay.rts.mines.systems.minefield_system;
import engine.gameplay.rts.movement.systems.route_request_system;
import engine.gameplay.rts.slaves.systems.slaved_system;
import engine.gameplay.rts.stealth.systems.stealth_detector_system;
import engine.gameplay.rts.topple.systems.topple_system;
import games.generalszh.gameplay.abilities.systems.special_ability_system;
import games.generalszh.gameplay.abilities.systems.sticky_bomb_system;
import games.generalszh.gameplay.ai.systems.tunnel_guard_system;
import games.generalszh.gameplay.battleplans.systems.battle_plan_system;
import games.generalszh.gameplay.containment.systems.heal_seek_system;
import games.generalszh.gameplay.hacking.systems.internet_hack_system;
import games.generalszh.gameplay.powers.systems.launcher_door_system;
import games.generalszh.gameplay.railroad.systems.railroad_system;
import games.generalszh.gameplay.upgrades.systems.upgrade_effect_system;
import engine.gameplay.rts.combat.systems.assist_system;
import engine.gameplay.rts.combat.systems.attack_move_system;
import engine.gameplay.rts.combat.systems.auto_fire_system;
import engine.gameplay.rts.combat.systems.countermeasures_system;
import engine.gameplay.rts.combat.systems.damage_reaction_system;
import engine.gameplay.rts.combat.systems.firing_tracker_system;
import engine.gameplay.rts.combat.systems.impact_system;
import engine.gameplay.rts.combat.systems.missile_flight_system;
import engine.gameplay.rts.combat.systems.missile_jam_system;
import engine.gameplay.rts.combat.systems.neutron_flight_system;
import engine.gameplay.rts.combat.systems.point_defense_system;
import engine.gameplay.rts.combat.systems.projectile_flight_system;
import engine.gameplay.rts.combat.systems.projectile_launch_system;
import engine.gameplay.rts.combat.systems.targeting_system;
import engine.gameplay.rts.combat.systems.turret_system;
import engine.gameplay.rts.combat.systems.weapon_bonus_retime_system;
import engine.gameplay.rts.combat.systems.weapon_system;
import games.generalszh.gameplay.combat.systems.battle_bus_system;
import games.generalszh.gameplay.combat.systems.checkpoint_system;
import games.generalszh.gameplay.combat.systems.cleanup_hazard_system;
import games.generalszh.gameplay.combat.systems.cooldown_creation_system;
import games.generalszh.gameplay.combat.systems.deploy_system;
import games.generalszh.gameplay.combat.systems.enemy_near_system;
import games.generalszh.gameplay.combat.systems.firestorm_system;
import games.generalszh.gameplay.combat.systems.pilot_kill_system;
import games.generalszh.gameplay.combat.systems.projectile_body_system;
import games.generalszh.gameplay.combat.systems.weapon_bonus_pulse_system;
import engine.gameplay.rts.combat.components.aggression;
import engine.gameplay.rts.combat.components.assisted_targeting;
import engine.gameplay.rts.combat.components.attack_move;
import engine.gameplay.rts.combat.components.auto_fire;
import engine.gameplay.rts.combat.components.countermeasures;
import engine.gameplay.rts.combat.components.damage_reaction;
import engine.gameplay.rts.combat.components.deploy;
import engine.gameplay.rts.combat.components.firing_tracker;
import engine.gameplay.rts.combat.components.missile;
import engine.gameplay.rts.combat.components.neutron_flight;
import engine.gameplay.rts.combat.components.point_defense;
import engine.gameplay.rts.combat.components.projectile;
import engine.gameplay.rts.combat.components.turret;
import games.generalszh.gameplay.combat.components.battle_bus;
import games.generalszh.gameplay.combat.components.checkpoint;
import games.generalszh.gameplay.combat.components.cleanup_hazard;
import games.generalszh.gameplay.combat.components.cooldown_creation;
import games.generalszh.gameplay.combat.components.enemy_near;
import games.generalszh.gameplay.combat.components.firestorm;
import games.generalszh.gameplay.combat.components.weapon_bonus_pulse;

// The combat domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The combat domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceCombatResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::MissileReports>();
	world.EmplaceResource<engine::gameplay::FlareLaunches>();
	world.EmplaceResource<engine::gameplay::TemporaryWeaponFires>();
	world.EmplaceResource<engine::gameplay::HistoricDamage>(setup.content.gameData.historicDamageLimitTicks);
	world.EmplaceResource<engine::gameplay::NeutronEffects>();
	world.EmplaceResource<generalszh::gameplay::CooldownCreationEvents>();
	world.EmplaceResource<engine::gameplay::GarrisonHits>();
	world.EmplaceResource<engine::gameplay::MissileGarrisonHits>();
	world.EmplaceResource<engine::gameplay::GarrisonClears>();
	world.EmplaceResource<engine::gameplay::Assists>();
	world.EmplaceResource<engine::gameplay::Detonations>();
	world.EmplaceResource<engine::gameplay::MissileDetonations>();
	world.EmplaceResource<engine::gameplay::PointDefenseShots>();
	world.EmplaceResource<engine::gameplay::DirectShots>();
	world.EmplaceResource<engine::gameplay::AutoShots>();
	world.EmplaceResource<generalszh::gameplay::BattleBusEvents>();
	world.EmplaceResource<generalszh::gameplay::BattleBusCues>();
	world.EmplaceResource<generalszh::gameplay::WeaponBonusPulses>();
	world.EmplaceResource<generalszh::gameplay::FirestormEvents>();
	world.EmplaceResource<engine::gameplay::Disarms>();
}

inline void RegisterCombatComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::CleanupHazard>();
	world.RegisterComponent<engine::gameplay::Countermeasures>();
	world.RegisterComponent<generalszh::gameplay::BattleBus>();
	world.RegisterComponent<generalszh::gameplay::WeaponBonusPulse>();
	world.RegisterComponent<generalszh::gameplay::EnemyNear>();
	world.RegisterComponent<generalszh::gameplay::Checkpoint>();
	world.RegisterComponent<generalszh::gameplay::Firestorm>();
	world.RegisterComponent<engine::gameplay::FiringTracker>();
	world.RegisterComponent<engine::gameplay::Turret>();
	world.RegisterComponent<engine::gameplay::AltTurret>();
	world.RegisterComponent<engine::gameplay::PointDefense>();
	world.RegisterComponent<engine::gameplay::ProjectileFlight>();
	world.RegisterComponent<engine::gameplay::MissileFlight>();
	world.RegisterComponent<engine::gameplay::Aggression>();
	world.RegisterComponent<engine::gameplay::AttackMove>();
	world.RegisterComponent<engine::gameplay::AssistedTargeting>();
	world.RegisterComponent<engine::gameplay::Assisting>();
	world.RegisterComponent<engine::gameplay::AutoFire>();
	world.RegisterComponent<engine::gameplay::DamageReaction>();
	world.RegisterComponent<generalszh::gameplay::CooldownCreations>();
	world.RegisterComponent<engine::gameplay::Deploy>();
	world.RegisterComponent<engine::gameplay::NeutronFlight>();
}

// The combat domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterCombatSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::TargetingSystem targeting;
	registry.Register(targeting);
	static engine::gameplay::AttackMoveSystem attackMoves;
	registry.Register(attackMoves);
	static engine::gameplay::TurretSystem turrets;
	registry.Register(turrets);
	static engine::gameplay::AltTurretSystem altTurrets;
	registry.Register(altTurrets);
	static engine::gameplay::WeaponSystem firing;
	registry.Register(firing);
	static engine::gameplay::FiringTrackerSystem firingTracker;
	registry.Register(firingTracker);
	static engine::gameplay::ProjectileFlightSystem projectiles;
	registry.Register(projectiles);
	static engine::gameplay::ProjectileLaunchSystem launches;
	registry.Register(launches);
	static engine::gameplay::MissileFlightSystem missiles;
	registry.Register(missiles);
	static engine::gameplay::PointDefenseSystem pointDefense;
	registry.Register(pointDefense);
	static generalszh::gameplay::ProjectileBodySystem projectileBodies;
	registry.Register(projectileBodies);
	static generalszh::gameplay::ShellBodySystem shellBodies;
	registry.Register(shellBodies);
	static engine::gameplay::WeaponBonusRetimeSystem bonusRetime;
	registry.Register(bonusRetime);
	static engine::gameplay::ImpactSystem impacts;
	registry.Register(impacts);
	static engine::gameplay::MissileJamSystem missileJam;
	registry.Register(missileJam);
	static engine::gameplay::AutoFireSystem autoFire;
	registry.Register(autoFire);
	static engine::gameplay::DamageReactionSystem damageReaction;
	registry.Register(damageReaction);
	static generalszh::gameplay::BattleBusSystem battleBuses;
	registry.Register(battleBuses);
	static generalszh::gameplay::WeaponBonusPulseSystem weaponBonusPulses;
	registry.Register(weaponBonusPulses);
	static generalszh::gameplay::EnemyNearSystem enemyNears;
	registry.Register(enemyNears);
	static generalszh::gameplay::CheckpointSystem checkpoints;
	registry.Register(checkpoints);
	static generalszh::gameplay::FirestormSystem firestorms;
	registry.Register(firestorms);
	static generalszh::gameplay::PilotKillSystem pilotKills;
	registry.Register(pilotKills);
	static generalszh::gameplay::CleanupHazardSystem cleanupHazards;
	registry.Register(cleanupHazards);
	static engine::gameplay::CountermeasuresSystem countermeasures;
	registry.Register(countermeasures);
	static engine::gameplay::AssistSystem assists;
	registry.Register(assists);
	static generalszh::gameplay::CooldownCreationSystem cooldownCreations;
	registry.Register(cooldownCreations);
	static generalszh::gameplay::DeploySystem deploys;
	registry.Register(deploys);
	static engine::gameplay::NeutronFlightSystem neutronFlights;
	registry.Register(neutronFlights);
}

// What the combat domain's systems run after (and the few they must precede), within the tick.
inline void OrderCombatSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	// Riders' passed-on bonuses before the weapons aim and fire.
	registry.OrderBefore<gameplay::PassedBonusSystem, gameplay::TargetingSystem>();
	registry.OrderBefore<gameplay::PassedBonusSystem, gameplay::WeaponSystem>();
	registry.OrderBefore<gameplay::GarrisonClearSystem, gameplay::ImpactSystem>();
	registry.OrderBefore<gameplay::RouteRequestSystem, gameplay::TargetingSystem>();
	// The trackers cool down before the weapons fire.
	registry.OrderBefore<gameplay::FiringTrackerSystem, gameplay::WeaponSystem>();
	registry.OrderBefore<domain::UpgradeEffectSystem, gameplay::WeaponBonusRetimeSystem>();
	registry.OrderBefore<gameplay::SlavedSystem, gameplay::TargetingSystem>();
	// Missiles subdued this tick are jammed.
	registry.OrderBefore<gameplay::SubdualSystem, gameplay::MissileJamSystem>();
	registry.OrderBefore<gameplay::HordeSystem, gameplay::WeaponSystem>();
	registry.OrderBefore<gameplay::HealthSystem, gameplay::DamageReactionSystem>();
	registry.OrderBefore<gameplay::AutoFireSystem, gameplay::DamageReactionSystem>();
	registry.OrderBefore<domain::RailroadSystem, domain::FirestormSystem>();
	registry.OrderBefore<domain::RailroadSystem, domain::BattleBusSystem>();
	registry.OrderBefore<domain::RailroadSystem, domain::EnemyNearSystem>();
	registry.OrderBefore<domain::RailroadSystem, domain::WeaponBonusPulseSystem>();
	registry.OrderBefore<gameplay::ShockWaveSystem, domain::CheckpointSystem>();
	registry.OrderBefore<gameplay::DropHomingSystem, domain::CheckpointSystem>();
	registry.OrderBefore<gameplay::StealthRevealSystem, domain::CheckpointSystem>();
	registry.OrderBefore<gameplay::NeutronFlightSystem, domain::CheckpointSystem>();
	registry.OrderBefore<gameplay::DropHomingSystem, domain::FirestormSystem>();
	registry.OrderBefore<gameplay::NeutronFlightSystem, domain::FirestormSystem>();
	registry.OrderBefore<domain::StickyBombSystem, domain::EnemyNearSystem>();
	registry.OrderBefore<gameplay::NeutronFlightSystem, domain::EnemyNearSystem>();
	registry.OrderBefore<gameplay::HordeSystem, domain::WeaponBonusPulseSystem>();
	registry.OrderBefore<domain::StickyBombSystem, domain::WeaponBonusPulseSystem>();
	registry.OrderBefore<gameplay::NeutronFlightSystem, domain::WeaponBonusPulseSystem>();
	registry.OrderBefore<domain::StickyBombSystem, domain::BattleBusSystem>();
	registry.OrderBefore<gameplay::NeutronFlightSystem, domain::BattleBusSystem>();
	registry.OrderBefore<domain::BattlePlanSystem, gameplay::WeaponSystem>();
	registry.OrderBefore<domain::BattlePlanSystem, gameplay::TurretSystem>();
	registry.OrderBefore<domain::InternetHackSystem, domain::CleanupHazardSystem>();
	registry.OrderBefore<gameplay::BodyCollisionSystem, gameplay::ImpactSystem>();
	registry.OrderBefore<gameplay::CollideWeaponSystem, gameplay::ImpactSystem>();
	// Those asked to help join in after the tick's shots.
	registry.OrderBefore<gameplay::WeaponSystem, gameplay::AssistSystem>();
	registry.OrderBefore<gameplay::HealingSystem, gameplay::AssistSystem>();
	registry.OrderBefore<gameplay::SaleSystem, gameplay::AssistSystem>();
	registry.OrderBefore<gameplay::MissileFlightSystem, gameplay::AssistSystem>();
	registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::AssistSystem>();
	registry.OrderBefore<domain::DeploySystem, gameplay::AttackMoveSystem>();
	registry.OrderBefore<gameplay::JetSystem, gameplay::AttackMoveSystem>();
	registry.OrderBefore<gameplay::UnloadingSystem, gameplay::AttackMoveSystem>();
	registry.OrderBefore<gameplay::FiringTrackerSystem, domain::CleanupHazardSystem>();
	registry.OrderBefore<gameplay::FiringTrackerSystem, domain::CooldownCreationSystem>();
	registry.OrderBefore<domain::ProjectileBodySystem, gameplay::MissileJamSystem>();
	registry.OrderBefore<gameplay::NeutronFlightSystem, gameplay::MissileJamSystem>();
	registry.OrderBefore<domain::LauncherDoorSystem, gameplay::NeutronFlightSystem>();
	registry.OrderBefore<gameplay::GrantStealthSystem, gameplay::NeutronFlightSystem>();
	registry.OrderBefore<domain::HealSeekSystem, gameplay::NeutronFlightSystem>();
	registry.OrderBefore<gameplay::ConstructionSystem, gameplay::NeutronFlightSystem>();
	registry.OrderBefore<gameplay::DockSystem, gameplay::NeutronFlightSystem>();
	registry.OrderBefore<domain::TunnelGuardSystem, gameplay::NeutronFlightSystem>();
	registry.OrderBefore<gameplay::JetSystem, domain::DeploySystem>();
	registry.OrderBefore<gameplay::UnloadingSystem, domain::DeploySystem>();
	registry.OrderBefore<gameplay::ProjectileLaunchSystem, gameplay::CountermeasuresSystem>();
	registry.OrderBefore<gameplay::SpatialIndexSystem, domain::CleanupHazardSystem>();
	registry.OrderBefore<gameplay::MinefieldSystem, domain::PilotKillSystem>();
	registry.OrderBefore<domain::SpecialAbilitySystem, gameplay::AltTurretSystem>();
	registry.OrderBefore<domain::SpecialAbilitySystem, gameplay::TurretSystem>();
	registry.OrderBefore<domain::SpecialAbilitySystem, gameplay::ProjectileFlightSystem>();
	registry.OrderBefore<gameplay::MinefieldSystem, gameplay::DamageReactionSystem>();
	registry.OrderBefore<gameplay::LoadoutSystem, gameplay::WeaponBonusRetimeSystem>();
	registry.OrderBefore<domain::DeploySystem, gameplay::TurretSystem>();
	registry.OrderBefore<domain::DeploySystem, gameplay::AltTurretSystem>();
	registry.OrderBefore<domain::CooldownCreationSystem, gameplay::WeaponSystem>();
	// Things fall before this tick's shots are aimed.
	registry.OrderBefore<gameplay::ToppleSystem, gameplay::WeaponSystem>();
	// Projectiles fly on from where the tick's shots left them.
	registry.OrderBefore<gameplay::WeaponSystem, gameplay::ProjectileFlightSystem>();
	registry.OrderBefore<gameplay::WeaponSystem, gameplay::MissileFlightSystem>();
	registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::ImpactSystem>();
	registry.OrderBefore<gameplay::SpatialIndexSystem, gameplay::ProjectileFlightSystem>();
	registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::AutoFireSystem>();
	// Turrets aim before this tick's shots.
	registry.OrderBefore<gameplay::TurretSystem, gameplay::WeaponSystem>();
	registry.OrderBefore<gameplay::ToppleSystem, gameplay::TurretSystem>();
	registry.OrderBefore<gameplay::AltTurretSystem, gameplay::WeaponSystem>();
	registry.OrderBefore<gameplay::ToppleSystem, gameplay::AltTurretSystem>();
}
}
