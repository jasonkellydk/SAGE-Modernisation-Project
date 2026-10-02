cmake_minimum_required(VERSION 4.4.3)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(engine_gameplay_check LANGUAGES CXX)
endif()

# Reusable gameplay, split by genre (see games/generalszh/migration/README.md):
#   common/<domain>/  genre-neutral (health, weapons, movement, ...)
#   rts/<domain>/     RTS-specific (economy, power, production, ...)
#   fps/<domain>/     FPS-specific
# Each domain declares one library with engine_add_module_library() and is
# added below as it is ported. Domains never depend on game code.
option(ENGINE_GAMEPLAY_BUILD_TESTS "Build headless gameplay tests" ${BUILD_TESTING})
if(NOT TARGET engine_ecs)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../ecs" "${CMAKE_CURRENT_BINARY_DIR}/engine_ecs")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/module_library.cmake")
if(NOT TARGET engine_level)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../level" "${CMAKE_CURRENT_BINARY_DIR}/engine_level")
endif()

# common/identity: what an entity was built from, script names.
engine_add_module_library(engine_gameplay_identity
	MODULES
		common/identity/components/definition_ref.cppm
		common/identity/components/owner.cppm
		common/identity/components/team_member.cppm
		common/identity/components/captured.cppm
		common/identity/components/object_id.cppm
		common/identity/components/producer.cppm
		common/identity/resources/object_ids.cppm
		common/identity/resources/name_registry.cppm
		common/identity/resources/relationships.cppm
		common/identity/resources/template_equivalence.cppm
	DEPENDS engine_ecs)

# common/appearance: condition bits the presentation draws by.
engine_add_module_library(engine_gameplay_appearance
	MODULES
		common/appearance/components/appearance.cppm
		common/appearance/components/indicator_color.cppm
		common/appearance/components/model_override.cppm
		common/appearance/components/draw_offset.cppm
		common/appearance/components/draw_hidden.cppm
		common/appearance/components/occlusion_safe.cppm
		common/appearance/components/debris_look.cppm
		common/appearance/components/part_overrides.cppm
		common/appearance/components/special_model_state.cppm
		common/appearance/algorithms/special_model_states.cppm
		common/appearance/systems/special_model_state_system.cppm
	DEPENDS engine_ecs)

# common/spatial: transforms, ground height, the presentation snapshot.
engine_add_module_library(engine_gameplay_spatial
	MODULES
		common/spatial/components/transform.cppm
		common/spatial/algorithms/find_position.cppm
		common/spatial/algorithms/footprint.cppm
		common/spatial/algorithms/geometry_collision.cppm
		common/spatial/components/attitude.cppm
		common/spatial/components/targetable.cppm
		common/spatial/components/body_extent.cppm
		common/spatial/components/off_map.cppm
		common/spatial/components/carried.cppm
		common/spatial/components/bounding_volume.cppm
		common/spatial/components/dynamic_geometry.cppm
		common/spatial/systems/dynamic_geometry_system.cppm
		common/spatial/components/object_shroud.cppm
		common/spatial/resources/ground_height.cppm
		common/spatial/resources/deck_surfaces.cppm
		common/spatial/components/surface_layer.cppm
		common/spatial/components/airborne_target.cppm
		common/spatial/resources/spatial_index.cppm
		common/spatial/systems/snapshot_system.cppm
		common/spatial/systems/spatial_index_system.cppm
	DEPENDS engine_ecs engine_core_math_fixed engine_level engine_gameplay_identity engine_gameplay_appearance
	TESTS common/spatial/spatial.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# common/areas: the level's named polygon areas and which of them each object is in (trigger areas).
engine_add_module_library(engine_gameplay_areas
	MODULES
		common/areas/resources/trigger_areas.cppm
		common/areas/resources/area_activity.cppm
		common/areas/components/area_presence.cppm
		common/areas/systems/area_presence_system.cppm
	DEPENDS engine_ecs engine_gameplay_spatial engine_core_math_fixed
	TESTS common/areas/areas.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# common/status: what disables an entity (the original's DisabledMaskType), and which behaviours still run.
engine_add_module_library(engine_gameplay_status
	MODULES
		common/status/components/disabled.cppm
		common/status/components/status_flags.cppm
		common/status/components/script_status.cppm
		common/status/components/disabled_until.cppm
		common/status/components/status_damage.cppm
		common/status/components/ai_activity.cppm
		common/status/systems/disable_systems.cppm
	DEPENDS engine_ecs
	TESTS common/status/status.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# common/random: the match's seed systems derive their streams from.
engine_add_module_library(engine_gameplay_random
	MODULES common/random/resources/random_seed.cppm
	DEPENDS engine_ecs)

# common/lifetime: limited lives that end in a kill.
engine_add_module_library(engine_gameplay_lifetime
	MODULES
		common/lifetime/components/lifetime.cppm
		common/lifetime/resources/expirations.cppm
		common/lifetime/systems/lifetime_system.cppm
	DEPENDS engine_ecs
	TESTS common/lifetime/lifetime.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/teams: players, teams and membership.
engine_add_module_library(engine_gameplay_teams
	MODULES
		rts/teams/resources/team_roster.cppm
	DEPENDS engine_ecs engine_gameplay_identity)

# rts/navigation: the waypoint graph, and the pathfinding grid (cells classified from terrain and obstacle
# footprints) with its ground routes.
engine_add_module_library(engine_gameplay_navigation
	MODULES
		rts/navigation/resources/waypoint_graph.cppm
		rts/navigation/definitions/pathfind_cell.cppm
		rts/navigation/resources/navigation_grid.cppm
		rts/navigation/algorithms/grid_classification.cppm
		rts/navigation/algorithms/clearance.cppm
		rts/navigation/algorithms/decks.cppm
		rts/navigation/resources/unit_cells.cppm
		rts/navigation/algorithms/unit_movement.cppm
		rts/navigation/algorithms/route_search.cppm
		rts/navigation/components/navigation.cppm
		rts/navigation/components/ignored_obstacle.cppm
		rts/navigation/components/pathfind_goal.cppm
		rts/navigation/resources/goal_cells.cppm
		rts/navigation/algorithms/goal_claims.cppm
		rts/navigation/algorithms/move_away_search.cppm
		rts/navigation/algorithms/view_blocked.cppm
		rts/navigation/algorithms/attack_view.cppm
		rts/navigation/systems/obstacle_system.cppm
	DEPENDS engine_ecs engine_core_math_fixed engine_gameplay_spatial engine_gameplay_identity
	TESTS rts/navigation/navigation.test rts/navigation/goal_claims.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/slaves (components): what a slave knows of its master (pure data, below combat: an attack's view passes over a
# slaver's obstacle).
engine_add_module_library(engine_gameplay_slaves_components
	MODULES
		rts/slaves/components/slaved.cppm
	DEPENDS engine_ecs engine_core_math_fixed)

# rts/collision (components): how a body crushes and is crushed (pure data the pathfinder weighs too).
engine_add_module_library(engine_gameplay_collision_components
	MODULES
		rts/collision/components/collider.cppm
		rts/collision/components/squishable.cppm
	DEPENDS engine_ecs engine_core_math_fixed)

# rts/blocking (state): what a ground unit's AI keeps about the units in its way, and the rules it weighs them by.
engine_add_module_library(engine_gameplay_blocking_state
	MODULES
		rts/blocking/components/blocking_unit.cppm
		rts/blocking/components/blocked_state.cppm
		rts/blocking/components/block_contact.cppm
		rts/blocking/algorithms/blocking_rules.cppm
		rts/blocking/algorithms/locomotor_blocking.cppm
	DEPENDS engine_ecs engine_core_math_fixed)

# rts/movement: locomotors, move orders, steering and the movement system.
engine_add_module_library(engine_gameplay_movement
	MODULES
		rts/movement/definitions/locomotor.cppm
		rts/movement/components/locomotion.cppm
		rts/movement/components/locomotor_choice.cppm
		rts/movement/components/move_order.cppm
		rts/movement/components/path_completed.cppm
		rts/movement/components/move_ended.cppm
		rts/movement/components/descent.cppm
		rts/movement/components/floor_lift.cppm
		rts/movement/algorithms/steering.cppm
		rts/movement/systems/movement_system.cppm
		rts/movement/systems/descent_system.cppm
		rts/movement/systems/route_request_system.cppm
		rts/movement/resources/movement_penalty.cppm
		rts/movement/systems/locomotor_damage_system.cppm
		rts/movement/systems/locomotor_choice_system.cppm
		rts/movement/components/wanderer.cppm
		rts/movement/components/wander_anchor.cppm
		rts/movement/components/face_target.cppm
		rts/movement/systems/face_target_system.cppm
		rts/movement/systems/wander_system.cppm
		rts/movement/components/move_path.cppm
		rts/movement/resources/path_points.cppm
		rts/movement/algorithms/move_paths.cppm
		rts/movement/components/move_goal.cppm
		rts/movement/components/move_away.cppm
		rts/movement/components/desired_speed.cppm
		rts/movement/components/formation_member.cppm
		rts/movement/components/pursuit.cppm
		rts/movement/components/attack_approach.cppm
		rts/movement/systems/move_path_system.cppm
		rts/movement/systems/path_points_sweep_system.cppm
		rts/movement/algorithms/hover_motion.cppm
		rts/movement/systems/airborne_target_system.cppm
		rts/movement/systems/locomotor_physics_system.cppm
		rts/movement/algorithms/flight_forces.cppm
	DEPENDS engine_gameplay_spatial engine_gameplay_navigation engine_gameplay_random engine_gameplay_status engine_gameplay_physics engine_gameplay_health
		engine_gameplay_blocking_state engine_gameplay_identity engine_gameplay_collision_components
	TESTS rts/movement/movement.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# Passive health components are shared independently of damage policies.
engine_add_module_library(engine_gameplay_health_components
	MODULES common/health/components/health.cppm common/health/components/shield.cppm
	DEPENDS engine_ecs engine_core_math_fixed)

# common/health: armor, hit points, damage application and deaths.
engine_add_module_library(engine_gameplay_health
	MODULES
		common/health/definitions/armor.cppm
		common/health/components/health_floor.cppm
		common/health/components/damage_scalar.cppm
		common/health/components/second_life.cppm
		common/status/algorithms/disable_now.cppm
		common/health/components/inactive_body.cppm
		common/health/algorithms/max_health.cppm
		common/health/resources/armor_catalog.cppm
		common/health/resources/incoming_damage.cppm
		common/health/systems/health_system.cppm
		common/health/components/pending_damage.cppm
		common/health/systems/pending_damage_system.cppm
		common/health/components/subdual.cppm
		common/health/systems/subdual_system.cppm
		common/health/systems/status_damage_system.cppm
	DEPENDS engine_ecs engine_core_math_fixed engine_gameplay_spatial engine_gameplay_status engine_gameplay_health_components)

# common/physics: bodies moved by forces (gravity, friction, bouncing, tumbling).
engine_add_module_library(engine_gameplay_physics
	MODULES
		common/physics/components/physics_body.cppm
		common/physics/resources/physics_settings.cppm
		common/physics/algorithms/forces.cppm
		common/physics/resources/fall_damage.cppm
		common/physics/components/bounce_sound.cppm
		common/physics/resources/landings.cppm
		common/physics/systems/physics_system.cppm
		common/physics/resources/shock_waves.cppm
		common/physics/systems/shock_wave_system.cppm
	DEPENDS engine_gameplay_spatial engine_gameplay_lifetime engine_gameplay_health engine_gameplay_status engine_gameplay_random
	TESTS common/physics/physics.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# common/fire: catching fire and burning.
engine_add_module_library(engine_gameplay_fire
	MODULES
		common/fire/components/flammable.cppm
		common/fire/components/fire_spread.cppm
		common/fire/resources/fire_settings.cppm
		common/fire/resources/ignitions.cppm
		common/fire/systems/flammability_system.cppm
		common/fire/systems/fire_spread_system.cppm
	DEPENDS engine_gameplay_health engine_gameplay_spatial engine_gameplay_random
	TESTS common/fire/fire.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# common/healing: self healing, area healers and the pass that applies their heals.
engine_add_module_library(engine_gameplay_healing
	MODULES
		common/healing/components/healing.cppm
		common/healing/resources/heal_pulses.cppm
		common/healing/systems/self_healing_system.cppm
		common/healing/systems/area_healing_system.cppm
		common/healing/systems/healing_system.cppm
	DEPENDS engine_gameplay_health engine_gameplay_spatial engine_gameplay_identity engine_gameplay_status
	TESTS common/healing/healing.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# common/poison: poison that stays in its victim and keeps hurting it.
engine_add_module_library(engine_gameplay_poison
	MODULES
		common/poison/components/poison.cppm
		common/poison/systems/poison_system.cppm
	DEPENDS engine_gameplay_health
	TESTS common/poison/poison.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# common/weapons: weapon definitions, armaments and attack targets.
engine_add_module_library(engine_gameplay_weapons
	MODULES
		common/weapons/definitions/weapon.cppm
		common/weapons/definitions/weapon_bonus.cppm
		common/weapons/components/weapon_bonus_conditions.cppm
		common/weapons/resources/weapon_catalog.cppm
		common/weapons/components/armament.cppm
		common/weapons/components/weapon_slots.cppm
		common/weapons/components/temp_weapon_bonus.cppm
		common/weapons/systems/temp_weapon_bonus_system.cppm
	DEPENDS engine_gameplay_spatial)

# rts/combat: target acquisition, firing and impacts.
engine_add_module_library(engine_gameplay_combat
	MODULES
		rts/combat/components/aggression.cppm
		rts/combat/components/attack_move.cppm
		rts/combat/components/attack_move_resume.cppm
		rts/combat/components/contained_definitions.cppm
		rts/combat/components/turret.cppm
		rts/combat/components/deploy.cppm
		rts/combat/components/sight_looker.cppm
		rts/combat/components/neutron_flight.cppm
		rts/combat/systems/neutron_flight_system.cppm
		rts/combat/algorithms/deploy_states.cppm
		rts/combat/systems/turret_system.cppm
		rts/combat/resources/shots.cppm
		rts/combat/resources/historic_damage.cppm
		rts/combat/resources/garrison_kills.cppm
		rts/combat/resources/launch_layouts.cppm
		rts/combat/algorithms/projectile_arc.cppm
		rts/combat/algorithms/attack_goal.cppm
		rts/combat/algorithms/attack_pursuit.cppm
		rts/combat/algorithms/target_pitch.cppm
		rts/combat/algorithms/weapon_fitness.cppm
		rts/combat/algorithms/waypoint_weapon.cppm
		rts/combat/algorithms/countermeasure_decoys.cppm
		rts/combat/components/countermeasures.cppm
		rts/combat/components/sneaky_target.cppm
		rts/combat/systems/countermeasures_system.cppm
		rts/combat/algorithms/projectile_collision.cppm
		rts/combat/resources/attack_priorities.cppm
		rts/combat/resources/mood_ranges.cppm
		rts/combat/systems/targeting_system.cppm
		rts/combat/systems/attack_move_system.cppm
		rts/combat/systems/attack_move_resume_system.cppm
		rts/combat/components/firing_tracker.cppm
		rts/combat/systems/weapon_system.cppm
		rts/combat/algorithms/spot_fire.cppm
		rts/combat/systems/firing_tracker_system.cppm
		rts/combat/components/assisted_targeting.cppm
		rts/combat/resources/assists.cppm
		rts/combat/systems/assist_system.cppm
		rts/combat/systems/weapon_bonus_retime_system.cppm
		rts/combat/systems/impact_system.cppm
		rts/combat/components/auto_fire.cppm
		rts/combat/systems/auto_fire_system.cppm
		rts/combat/components/damage_reaction.cppm
		rts/combat/systems/damage_reaction_system.cppm
		rts/combat/components/projectile.cppm
		rts/combat/systems/projectile_flight_system.cppm
		rts/combat/algorithms/thrust.cppm
		rts/combat/components/missile.cppm
		rts/combat/systems/missile_flight_system.cppm
		rts/combat/systems/missile_jam_system.cppm
		rts/combat/systems/projectile_launch_system.cppm
		rts/combat/components/point_defense.cppm
		rts/combat/systems/point_defense_system.cppm
	DEPENDS engine_gameplay_weapons engine_gameplay_health engine_gameplay_movement engine_gameplay_identity engine_gameplay_physics engine_gameplay_status engine_gameplay_construction engine_gameplay_teams engine_gameplay_areas engine_gameplay_veterancy engine_gameplay_lifetime
		engine_gameplay_navigation engine_gameplay_collision_components engine_gameplay_blocking_state engine_gameplay_spatial engine_gameplay_slaves_components
	TESTS rts/combat/combat.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/containment: transports, boarding and unloading.
engine_add_module_library(engine_gameplay_containment
	MODULES
		rts/containment/definitions/transport.cppm
		rts/containment/components/transport.cppm
		rts/containment/components/drop_homing.cppm
		rts/containment/systems/drop_homing_system.cppm
		rts/containment/components/cargo_size.cppm
		rts/containment/components/garrison.cppm
		rts/containment/components/garrison_points.cppm
		rts/containment/components/tunnel.cppm
		rts/containment/components/heal_pad.cppm
		rts/containment/components/mount.cppm
		rts/containment/resources/cargo_manifest.cppm
		rts/containment/resources/drop_settings.cppm
		rts/containment/resources/evacuations.cppm
		rts/containment/systems/boarding_system.cppm
		rts/containment/systems/unloading_system.cppm
		rts/containment/systems/cargo_transfer_system.cppm
		rts/containment/systems/garrison_clear_system.cppm
		rts/containment/systems/container_classes_system.cppm
		rts/containment/systems/contained_definitions_system.cppm
		rts/containment/systems/garrison_kill_damage_system.cppm
		rts/containment/systems/passed_bonus_system.cppm
		rts/containment/algorithms/garrison_stations.cppm
		rts/containment/systems/garrison_station_system.cppm
		rts/containment/systems/passenger_ride_system.cppm
		rts/containment/systems/heal_pad_system.cppm
		rts/containment/systems/rider_regen_system.cppm
	DEPENDS engine_gameplay_combat engine_gameplay_navigation engine_gameplay_physics
	TESTS rts/containment/containment.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/delivery: payload runs (paradrops, supply drops) by air.
engine_add_module_library(engine_gameplay_delivery
	MODULES
		rts/delivery/components/delivery.cppm
		rts/delivery/systems/delivery_system.cppm
	DEPENDS engine_gameplay_containment engine_gameplay_combat
	TESTS rts/delivery/delivery.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/lifecycle: taking entities out of the game (deaths, finished runs, deletes).
engine_add_module_library(engine_gameplay_lifecycle
	MODULES
		rts/lifecycle/resources/casualties.cppm
		rts/lifecycle/resources/kill_requests.cppm
		rts/lifecycle/resources/removals.cppm
		rts/lifecycle/algorithms/retirement.cppm
		rts/lifecycle/systems/removal_system.cppm
	DEPENDS engine_gameplay_delivery engine_gameplay_teams engine_gameplay_lifetime
	TESTS rts/lifecycle/lifecycle.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/collision: where the bodies are, and who runs into whom.
engine_add_module_library(engine_gameplay_collision
	MODULES
		rts/collision/resources/contacts.cppm
		rts/collision/systems/collision_systems.cppm
		rts/collision/resources/crush_damage.cppm
		rts/collision/systems/crush_system.cppm
		rts/collision/components/collide_weapon.cppm
		rts/collision/systems/collide_weapon_system.cppm
		rts/collision/components/body_collision.cppm
		rts/collision/resources/collision_settings.cppm
		rts/collision/systems/body_collision_system.cppm
	DEPENDS engine_gameplay_spatial engine_gameplay_movement engine_gameplay_health engine_gameplay_identity engine_gameplay_fire engine_gameplay_combat
		engine_gameplay_weapons engine_gameplay_physics engine_gameplay_lifetime engine_gameplay_collision_components
	TESTS rts/collision/collision.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/blocking: ground units held up by the units in their way, and planning their routes again when stuck.
engine_add_module_library(engine_gameplay_blocking
	MODULES
		rts/blocking/systems/unit_blocking_system.cppm
		rts/blocking/systems/blocked_repath_system.cppm
		rts/blocking/systems/unit_cell_system.cppm
		rts/blocking/resources/move_away_requests.cppm
	DEPENDS engine_gameplay_blocking_state engine_gameplay_collision engine_gameplay_movement engine_gameplay_navigation engine_gameplay_identity
		engine_gameplay_status engine_gameplay_health engine_gameplay_spatial engine_gameplay_death engine_gameplay_weapons engine_gameplay_containment engine_gameplay_docking
	TESTS rts/blocking/blocking.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/topple: things that fall over when run into.
engine_add_module_library(engine_gameplay_topple
	MODULES
		rts/topple/components/topple.cppm
		rts/topple/resources/topple_events.cppm
		rts/topple/systems/topple_system.cppm
	DEPENDS engine_gameplay_collision engine_gameplay_lifetime engine_gameplay_death
	TESTS rts/topple/topple.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/economy: players' money.
engine_add_module_library(engine_gameplay_economy
	MODULES
		rts/economy/resources/player_money.cppm
		rts/economy/components/energy_source.cppm
		rts/economy/resources/player_energy.cppm
		rts/economy/resources/player_bounties.cppm
		rts/economy/systems/energy_system.cppm
		rts/economy/components/overcharge.cppm
		rts/economy/resources/overcharge_events.cppm
		rts/economy/algorithms/overcharge_switch.cppm
		rts/economy/systems/overcharge_system.cppm
		rts/economy/components/auto_deposit.cppm
		rts/economy/systems/auto_deposit_system.cppm
	DEPENDS engine_ecs engine_gameplay_identity engine_gameplay_health engine_gameplay_status engine_gameplay_construction engine_gameplay_upgrades engine_gameplay_teams engine_gameplay_spatial
	TESTS rts/economy/economy.test rts/economy/auto_deposit.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/radar: which players have radar (the objects giving it, their power).
engine_add_module_library(engine_gameplay_radar
	MODULES
		rts/radar/components/radar_provider.cppm
		rts/radar/resources/player_radar.cppm
		rts/radar/systems/radar_coverage_system.cppm
	DEPENDS engine_ecs engine_gameplay_identity engine_gameplay_status engine_gameplay_economy
	TESTS rts/radar/radar.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/slaves: units that serve a master (drones guarding, attacking and scouting around it) and
# spawners that keep a number of them about.
engine_add_module_library(engine_gameplay_slaves
	MODULES
		rts/slaves/algorithms/enslave.cppm
		rts/slaves/systems/slaved_system.cppm
		rts/slaves/components/spawner.cppm
		rts/slaves/components/spawn_points.cppm
		rts/slaves/resources/spawn_requests.cppm
		rts/slaves/resources/slave_orders.cppm
		rts/slaves/systems/spawner_system.cppm
		rts/slaves/systems/disable_follow_system.cppm
		rts/slaves/components/hive_body.cppm
		rts/slaves/systems/hive_damage_system.cppm
	DEPENDS engine_gameplay_slaves_components engine_gameplay_movement engine_gameplay_health engine_gameplay_healing engine_gameplay_spatial engine_gameplay_weapons engine_gameplay_lifecycle engine_gameplay_random engine_gameplay_status engine_gameplay_death engine_gameplay_construction engine_gameplay_identity engine_gameplay_containment engine_gameplay_production
	TESTS rts/slaves/slaves.test rts/slaves/disable_follow.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/match: a match's standing (who is defeated, who won, when it ended) and the end of a defeated player.
engine_add_module_library(engine_gameplay_match
	MODULES
		rts/match/components/victory_role.cppm
		rts/match/resources/match_outcome.cppm
		rts/match/systems/victory_system.cppm
	DEPENDS engine_gameplay_death engine_gameplay_containment engine_gameplay_lifecycle engine_gameplay_teams engine_gameplay_identity engine_gameplay_health
		engine_gameplay_lifetime engine_gameplay_vision
	TESTS rts/match/match.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/upgrades: players' and objects' upgrades, and the triggers they set off.
engine_add_module_library(engine_gameplay_upgrades
	MODULES
		rts/upgrades/definitions/upgrade_trigger.cppm
		rts/upgrades/resources/upgrade_triggers.cppm
		rts/upgrades/resources/player_upgrades.cppm
		rts/upgrades/resources/upgrade_reactions.cppm
		rts/upgrades/resources/object_upgrade_grants.cppm
		rts/upgrades/components/upgradable.cppm
		rts/upgrades/systems/upgrade_system.cppm
	DEPENDS engine_ecs engine_gameplay_identity engine_gameplay_construction
	TESTS rts/upgrades/upgrades.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/veterancy: experience from kills, veterancy levels and what they change.
engine_add_module_library(engine_gameplay_veterancy
	MODULES
		rts/veterancy/definitions/veterancy.cppm
		rts/veterancy/components/experience.cppm
		rts/veterancy/resources/veterancy_catalog.cppm
		rts/veterancy/algorithms/experience.cppm
		rts/veterancy/algorithms/promotion.cppm
		rts/veterancy/resources/promotions.cppm
		rts/veterancy/resources/experience_awards.cppm
		rts/veterancy/resources/skill_point_awards.cppm
		rts/veterancy/systems/veterancy_system.cppm
	DEPENDS engine_ecs engine_gameplay_identity engine_gameplay_health engine_gameplay_weapons engine_gameplay_teams engine_gameplay_upgrades engine_gameplay_construction
	TESTS rts/veterancy/veterancy.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/horde: units that fight better together (horde, nationalism, fanaticism bonuses).
engine_add_module_library(engine_gameplay_horde
	MODULES
		rts/horde/components/horde.cppm
		rts/horde/resources/horde_catalog.cppm
		rts/horde/systems/horde_system.cppm
	DEPENDS engine_ecs engine_gameplay_identity engine_gameplay_spatial engine_gameplay_weapons engine_gameplay_upgrades
	TESTS rts/horde/horde.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/production: factories building their queues.
engine_add_module_library(engine_gameplay_production
	MODULES
		rts/production/components/production_queue.cppm
		rts/production/components/production_doors.cppm
		rts/production/components/production_exit_gate.cppm
		rts/production/components/rally_point.cppm
		rts/production/systems/production_system.cppm
	DEPENDS engine_ecs engine_gameplay_economy engine_gameplay_status
	TESTS rts/production/production.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/construction: how far structures are built, and selling them.
engine_add_module_library(engine_gameplay_construction
	MODULES
		rts/construction/components/construction_progress.cppm
		rts/construction/components/sale.cppm
		rts/construction/components/under_construction.cppm
		rts/construction/components/builder.cppm
		rts/construction/resources/sales.cppm
		rts/construction/systems/sale_system.cppm
	DEPENDS engine_ecs
	TESTS rts/construction/construction.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/construction (building work): builders raising structures, paced by their player's power.
engine_add_module_library(engine_gameplay_construction_work
	MODULES
		rts/construction/systems/construction_system.cppm
	DEPENDS engine_gameplay_construction engine_gameplay_economy engine_gameplay_movement engine_gameplay_health engine_gameplay_identity engine_gameplay_healing engine_gameplay_spatial
	TESTS rts/construction/construction_work.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/aircraft: jets at their airfields (parking, runways, the flight cycle).
engine_add_module_library(engine_gameplay_aircraft
	MODULES
		rts/aircraft/components/airfield.cppm
		rts/aircraft/components/jet.cppm
		rts/aircraft/resources/jet_damage.cppm
		rts/aircraft/algorithms/helicopter_flight.cppm
		rts/aircraft/algorithms/jet_runway_cycle.cppm
		rts/aircraft/systems/jet_system.cppm
		rts/aircraft/components/touchdown.cppm
		rts/aircraft/resources/jet_touchdowns.cppm
		rts/aircraft/systems/jet_touchdown_system.cppm
	DEPENDS engine_gameplay_movement engine_gameplay_combat engine_gameplay_weapons engine_gameplay_health
	TESTS rts/aircraft/aircraft.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/powers: special power countdowns (each object's modules, a player's shared timers).
engine_add_module_library(engine_gameplay_powers
	MODULES
		rts/powers/components/special_power_timers.cppm
		rts/powers/resources/shared_power_timers.cppm
		rts/powers/definitions/special_power_rules.cppm
		rts/powers/algorithms/special_power_timing.cppm
		rts/powers/systems/special_power_pause_system.cppm
	DEPENDS engine_ecs engine_gameplay_status engine_gameplay_identity
	TESTS rts/powers/powers.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/sciences: what each player knows (its side's sciences, those bought or granted) and its rank as a general.
engine_add_module_library(engine_gameplay_sciences
	MODULES
		rts/sciences/resources/player_sciences.cppm
		rts/sciences/resources/player_ranks.cppm
		rts/sciences/definitions/rank_rules.cppm
		rts/sciences/algorithms/ranks.cppm
	DEPENDS engine_ecs
	TESTS rts/sciences/sciences.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/loadout: weapon and armor sets picked by condition flags (upgrades, crates, riders, veterancy).
engine_add_module_library(engine_gameplay_loadout
	MODULES
		rts/loadout/components/loadout.cppm
		rts/loadout/resources/loadout_catalog.cppm
		rts/loadout/algorithms/equip.cppm
		rts/loadout/systems/loadout_system.cppm
	DEPENDS engine_gameplay_weapons engine_gameplay_health engine_gameplay_identity engine_gameplay_veterancy
	TESTS rts/loadout/loadout.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/docking: movers queueing at a dock, entering it, doing their business and leaving.
engine_add_module_library(engine_gameplay_docking
	MODULES
		rts/docking/components/dock.cppm
		rts/docking/components/docking.cppm
		rts/docking/components/dock_look.cppm
		rts/docking/algorithms/dock_points.cppm
		rts/docking/systems/dock_system.cppm
		rts/docking/components/repair_dock.cppm
		rts/docking/resources/dock_repairs.cppm
		rts/docking/systems/repair_dock_system.cppm
	DEPENDS engine_gameplay_movement engine_gameplay_navigation engine_gameplay_spatial engine_gameplay_random engine_gameplay_health engine_gameplay_containment
	TESTS rts/docking/docking.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/harvesting: harvesters ferrying boxes from stores to their player's depots.
engine_add_module_library(engine_gameplay_harvesting
	MODULES
		rts/harvesting/components/harvester.cppm
		rts/harvesting/components/resource_store.cppm
		rts/harvesting/resources/harvest_catalog.cppm
		rts/harvesting/resources/harvest_roster.cppm
		rts/harvesting/systems/harvest_roster_system.cppm
		rts/harvesting/systems/harvest_system.cppm
	DEPENDS engine_gameplay_docking engine_gameplay_economy engine_gameplay_identity engine_gameplay_lifetime engine_gameplay_upgrades engine_gameplay_containment
	TESTS rts/harvesting/harvesting.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/stealth: hiding from enemies until detected.
engine_add_module_library(engine_gameplay_stealth
	MODULES
		rts/stealth/components/stealth.cppm
		rts/stealth/components/stealth_detector.cppm
		rts/stealth/components/grant_stealth.cppm
		rts/stealth/components/undetected_defector.cppm
		rts/stealth/components/stealth_rider.cppm
		rts/stealth/resources/detections.cppm
		rts/stealth/systems/stealth_system.cppm
		rts/stealth/systems/stealth_detector_system.cppm
		rts/stealth/systems/defector_system.cppm
	DEPENDS engine_gameplay_spatial engine_gameplay_health engine_gameplay_weapons engine_gameplay_movement engine_gameplay_lifetime engine_gameplay_status engine_gameplay_containment
		engine_gameplay_combat engine_gameplay_teams engine_gameplay_identity
	TESTS rts/stealth/stealth.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/parachute: parachutes and their riders (the original's ParachuteContain): falling closed, opening, gliding for
# a landing spot on their locomotor's forces, swaying, and letting their rider go on landing.
engine_add_module_library(engine_gameplay_parachute
	MODULES
		rts/parachute/definitions/parachute_definition.cppm
		rts/parachute/components/parachute.cppm
		rts/parachute/resources/parachute_openings.cppm
		rts/parachute/resources/parachute_landings.cppm
		rts/parachute/algorithms/parachute_rigging.cppm
		rts/parachute/algorithms/parachute_release.cppm
		rts/parachute/systems/parachute_systems.cppm
	DEPENDS engine_gameplay_movement engine_gameplay_physics engine_gameplay_spatial engine_gameplay_status engine_gameplay_death engine_gameplay_lifecycle
		engine_gameplay_navigation
	TESTS rts/parachute/parachute.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/emp: electromagnetic pulses that disable vehicles and structures for a while (the original's EMPUpdate).
engine_add_module_library(engine_gameplay_emp
	MODULES
		rts/emp/components/emp_pulse.cppm
		rts/emp/resources/emp_strikes.cppm
		rts/emp/systems/emp_pulse_system.cppm
	DEPENDS engine_gameplay_status engine_gameplay_spatial engine_gameplay_identity engine_gameplay_weapons engine_gameplay_lifecycle
	TESTS rts/emp/emp.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/mines: mines that go off under what stands in them, their virtual mines held in their health (the original's
# MinefieldBehavior).
engine_add_module_library(engine_gameplay_mines
	MODULES
		rts/mines/components/minefield.cppm
		rts/mines/systems/minefield_system.cppm
		rts/mines/components/demo_trap.cppm
		rts/mines/systems/demo_trap_system.cppm
	DEPENDS engine_gameplay_health engine_gameplay_healing engine_gameplay_spatial engine_gameplay_identity engine_gameplay_lifetime engine_gameplay_combat engine_gameplay_death engine_gameplay_construction engine_gameplay_weapons
	TESTS rts/mines/mines.test rts/mines/demo_trap.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/propaganda: towers that rouse and heal the allies around them (the original's PropagandaTowerBehavior).
engine_add_module_library(engine_gameplay_propaganda
	MODULES
		rts/propaganda/components/propaganda.cppm
		rts/propaganda/resources/propaganda_scans.cppm
		rts/propaganda/systems/propaganda_system.cppm
	DEPENDS engine_gameplay_healing engine_gameplay_weapons engine_gameplay_upgrades engine_gameplay_spatial engine_gameplay_status engine_gameplay_identity engine_gameplay_health engine_gameplay_construction
	TESTS rts/propaganda/propaganda.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/death: how things die: die behaviours at the moment of death, slow
# deaths played out over time (sinking, phase effects), removal when done.
engine_add_module_library(engine_gameplay_death
	MODULES
		rts/death/definitions/death_definition.cppm
		rts/death/resources/hulk_lifetime.cppm
		rts/death/components/dying.cppm
		rts/death/components/crash.cppm
		rts/death/components/collapse.cppm
		rts/death/components/structure_topple.cppm
		rts/death/resources/death_events.cppm
		rts/death/algorithms/death_choice.cppm
		rts/death/algorithms/structure_topple.cppm
		rts/death/systems/death_system.cppm
		rts/death/systems/slow_death_system.cppm
		rts/death/systems/structure_topple_system.cppm
		rts/death/systems/crash_collision_system.cppm
		rts/death/components/height_die.cppm
		rts/death/components/death_credit.cppm
		rts/death/systems/height_die_system.cppm
		rts/death/components/blast_wave.cppm
		rts/death/resources/blast_waves.cppm
		rts/death/systems/blast_wave_system.cppm
		rts/death/systems/mount_system.cppm
	DEPENDS engine_gameplay_veterancy engine_gameplay_lifecycle engine_gameplay_health engine_gameplay_weapons engine_gameplay_combat engine_gameplay_movement
		engine_gameplay_containment engine_gameplay_spatial engine_gameplay_physics engine_gameplay_lifetime engine_gameplay_status engine_gameplay_collision engine_gameplay_construction
	TESTS rts/death/death.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)

# rts/vision: the shroud each player sees (partition cells looked at, fogged, shrouded) and the objects looking.
engine_add_module_library(engine_gameplay_vision
	MODULES
		rts/vision/resources/shroud_map.cppm
		rts/vision/components/vision.cppm
		rts/vision/systems/vision_system.cppm
		rts/vision/components/partition_footprint.cppm
		rts/vision/algorithms/footprint_cells.cppm
		rts/vision/systems/object_shroud_system.cppm
		rts/vision/definitions/dynamic_clearing.cppm
		rts/vision/components/dynamic_clearing.cppm
		rts/vision/systems/dynamic_clearing_system.cppm
	DEPENDS engine_ecs engine_gameplay_identity engine_gameplay_spatial engine_gameplay_construction engine_gameplay_containment engine_gameplay_stealth
		engine_gameplay_death
	TESTS rts/vision/vision.test
	TESTS_OPTION ENGINE_GAMEPLAY_BUILD_TESTS)
