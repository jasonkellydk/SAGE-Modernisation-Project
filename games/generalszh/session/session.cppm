export module games.generalszh.session.session;
import games.generalszh.gameplay.containment.algorithms.transport_riders;
import games.generalszh.session.composition.tick_events;
import games.generalszh.session.composition.simulation;
import games.generalszh.session.composition.simulation_setup;
import engine.gameplay.rts.movement.components.pursuit;
import games.generalszh.gameplay.containment.algorithms.railed_transports;
import games.generalszh.gameplay.containment.systems.railed_transport_systems;
import games.generalszh.gameplay.combat.systems.checkpoint_system;
import games.generalszh.gameplay.world.resources.music_progress;
import games.generalszh.gameplay.world.resources.map_scenery;
import games.generalszh.gameplay.combat.algorithms.shot_creation_lists;
import games.generalszh.gameplay.production.algorithms.production_completion;
import games.generalszh.gameplay.fire.algorithms.fire_embers;
import games.generalszh.gameplay.spawning.algorithms.spawn_completion;
import games.generalszh.gameplay.crates.algorithms.crate_pickup_step;
import games.generalszh.gameplay.mines.algorithms.minefield_deaths;
import games.generalszh.gameplay.topple.algorithms.topple_stumps;
import games.generalszh.gameplay.upgrades.algorithms.upgrade_creations;
import games.generalszh.gameplay.death.algorithms.death_aftermath;
import games.generalszh.gameplay.world.resources.match_rules;
import games.generalszh.gameplay.aircraft.systems.airfield_heal_system;
import games.generalszh.gameplay.world.algorithms.water_levels;
import games.generalszh.gameplay.flight_deck.algorithms.flight_decks;
import games.generalszh.gameplay.flight_deck.systems.flight_deck_systems;
import games.generalszh.gameplay.combat_drop.resources.deferred_orders;
import games.generalszh.gameplay.combat_drop.algorithms.rappel_landings;
import games.generalszh.gameplay.combat_drop.systems.combat_drop_systems;
import engine.gameplay.rts.combat.systems.attack_move_system;
import engine.gameplay.common.status.components.script_status;
import engine.gameplay.rts.movement.systems.move_path_system;
import games.generalszh.gameplay.movement.systems.destination_adjust_system;
import games.generalszh.gameplay.movement.systems.goal_claim_system;
import engine.gameplay.rts.movement.systems.face_target_system;
import engine.gameplay.rts.death.systems.height_die_system;
import engine.gameplay.rts.powers.systems.special_power_pause_system;
import games.generalszh.gameplay.construction.algorithms.rebuild_holes;
import games.generalszh.gameplay.production.resources.production_notices;
import engine.gameplay.rts.radar.systems.radar_coverage_system;
import engine.gameplay.rts.vision.systems.vision_system;
import engine.gameplay.rts.vision.systems.object_shroud_system;
import engine.gameplay.common.health.components.inactive_body;
import engine.gameplay.rts.combat.systems.damage_reaction_system;
import games.generalszh.gameplay.eva.resources.eva_notices;
import games.generalszh.gameplay.powers.algorithms.cash_bounty;
import games.generalszh.gameplay.powers.resources.cash_notices;
import engine.gameplay.rts.economy.resources.player_bounties;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import games.generalszh.gameplay.powers.algorithms.special_power_launch;
import games.generalszh.gameplay.powers.algorithms.shortcut_powers;
import games.generalszh.gameplay.orders.algorithms.view_targets;
import games.generalszh.gameplay.production.algorithms.rally_points;
import std;
import engine.gameplay.common.identity.components.captured;
import games.generalszh.gameplay.construction.algorithms.build_legality;
import games.generalszh.gameplay.construction.algorithms.building;
import games.generalszh.gameplay.orders.algorithms.command_availability;
import games.generalszh.gameplay.orders.algorithms.build_tooltip_facts;
import games.generalszh.gameplay.orders.algorithms.move_hints;
import engine.gameplay.rts.collision.systems.body_collision_system;
import engine.gameplay.rts.containment.systems.rider_regen_system;
import engine.gameplay.rts.movement.systems.wander_system;
import engine.gameplay.rts.movement.systems.locomotor_damage_system;
import engine.gameplay.rts.death.systems.crash_collision_system;
import games.generalszh.gameplay.teams.systems.tech_building_system;
export import engine.gameplay.rts.match.systems.victory_system;
import games.generalszh.gameplay.ai.algorithms.ai_players;
import games.generalszh.gameplay.ai.algorithms.ai_base_building;
import games.generalszh.gameplay.ai.algorithms.ai_team_building;
import games.generalszh.gameplay.ai.algorithms.team_path_follows;
import games.generalszh.gameplay.sciences.algorithms.general_ranks;
import engine.gameplay.rts.veterancy.resources.skill_point_awards;
import engine.gameplay.common.identity.components.object_id;
import engine.gameplay.common.identity.resources.object_ids;
import games.generalszh.gameplay.teams.algorithms.team_states;
export import games.generalszh.gameplay.scripts.resources.script_records;
export import games.generalszh.gameplay.orders.resources.command_bar_overrides;
export import games.generalszh.gameplay.orders.resources.buildable_overrides;
import games.generalszh.gameplay.scripts.algorithms.object_counting;
import engine.gameplay.common.health.systems.pending_damage_system;
import engine.gameplay.rts.containment.systems.heal_pad_system;
import games.generalszh.gameplay.containment.systems.heal_seek_system;
import engine.gameplay.rts.collision.systems.collide_weapon_system;
import engine.gameplay.rts.combat.systems.assist_system;
import games.generalszh.gameplay.crates.systems.pilot_seek_system;
import engine.gameplay.rts.parachute.systems.parachute_systems;
import engine.gameplay.common.appearance.components.debris_look;
import engine.gameplay.rts.stealth.systems.defector_system;

export import games.generalszh.session.session_view;
export import engine.level.model.level;
export import engine.gameplay.common.spatial.systems.snapshot_system;
export import engine.gameplay.rts.lifecycle.resources.casualties;
export import engine.gameplay.rts.combat.resources.shots;
export import games.generalszh.content.objects.object_definition;
export import games.generalszh.scripting.camera_vocabulary;
export import games.generalszh.content.loading.game_content;
export import games.generalszh.commands.game_commands;
export import games.generalszh.scripting.presentation_vocabulary;
import engine.ecs.core.world;
import engine.ecs.query.query;
import engine.ecs.scheduler.scheduler;
export import engine.jobs.job_system;
import engine.time.simulation_time;
import engine.scripting.runtime.script_runtime;
import engine.gameplay.common.spatial.systems.spatial_index_system;
import engine.gameplay.rts.movement.systems.movement_system;
import engine.gameplay.rts.movement.systems.descent_system;
import engine.gameplay.rts.combat.systems.impact_system;
import engine.gameplay.rts.combat.systems.weapon_bonus_retime_system;
import engine.gameplay.rts.veterancy.systems.veterancy_system;
import engine.gameplay.rts.horde.systems.horde_system;
import engine.gameplay.rts.navigation.systems.obstacle_system;
import engine.gameplay.rts.movement.systems.route_request_system;
import games.generalszh.gameplay.world.algorithms.navigation_setup;
import engine.gameplay.rts.containment.systems.boarding_system;
import engine.gameplay.rts.containment.systems.unloading_system;
import engine.gameplay.rts.containment.systems.cargo_transfer_system;
import engine.gameplay.rts.containment.systems.passenger_ride_system;
import engine.gameplay.rts.delivery.systems.delivery_system;
import engine.gameplay.rts.lifecycle.systems.removal_system;
export import engine.gameplay.rts.death.systems.slow_death_system;
import engine.gameplay.rts.death.systems.structure_topple_system;
import engine.gameplay.common.physics.systems.physics_system;
import engine.gameplay.common.physics.systems.shock_wave_system;
import engine.gameplay.rts.containment.systems.garrison_clear_system;
import engine.gameplay.rts.containment.systems.container_classes_system;
import engine.gameplay.rts.containment.systems.garrison_kill_damage_system;
import engine.gameplay.rts.containment.systems.passed_bonus_system;
import engine.gameplay.common.lifetime.systems.lifetime_system;
import engine.gameplay.common.healing.systems.healing_system;
import engine.gameplay.common.fire.systems.flammability_system;
import engine.gameplay.common.fire.systems.fire_spread_system;
import engine.gameplay.rts.combat.systems.auto_fire_system;
import engine.gameplay.rts.combat.systems.projectile_flight_system;
import engine.gameplay.rts.combat.systems.projectile_launch_system;
import engine.gameplay.rts.combat.systems.missile_flight_system;
import engine.gameplay.rts.combat.systems.point_defense_system;
import games.generalszh.gameplay.combat.systems.projectile_body_system;
import games.generalszh.gameplay.upgrades.systems.upgrade_effect_system;
import games.generalszh.gameplay.upgrades.algorithms.research;
import games.generalszh.gameplay.upgrades.systems.research_completion_system;
import games.generalszh.gameplay.world.algorithms.starting_objects;
import games.generalszh.gameplay.production.systems.production_refund_system;
import engine.gameplay.rts.slaves.systems.slaved_system;
import engine.gameplay.rts.stealth.systems.stealth_system;
import engine.gameplay.rts.stealth.systems.stealth_detector_system;
import engine.gameplay.rts.topple.systems.topple_system;
import engine.gameplay.rts.death.systems.blast_wave_system;
import engine.gameplay.rts.economy.systems.overcharge_system;
import engine.gameplay.rts.death.systems.mount_system;
import engine.gameplay.rts.propaganda.systems.propaganda_system;
import engine.gameplay.rts.mines.systems.minefield_system;
import engine.gameplay.rts.mines.systems.demo_trap_system;
import engine.gameplay.rts.emp.systems.emp_pulse_system;
import engine.gameplay.rts.economy.systems.auto_deposit_system;
import engine.gameplay.common.appearance.components.part_overrides;
import engine.gameplay.common.areas.systems.area_presence_system;
import engine.gameplay.rts.combat.resources.attack_priorities;
import games.generalszh.gameplay.scripts.algorithms.team_sequences;
import games.generalszh.gameplay.abilities.algorithms.special_ability_update;
import games.generalszh.gameplay.abilities.algorithms.command_button_hunts;
import games.generalszh.gameplay.combat.algorithms.unmanned_vehicles;
import games.generalszh.gameplay.combat.algorithms.subdual_changes;
import games.generalszh.gameplay.containment.algorithms.initial_payloads;
import games.generalszh.gameplay.containment.algorithms.rider_changes;
import games.generalszh.content.objects.object_status;
import engine.gameplay.common.health.systems.subdual_system;
import engine.gameplay.common.spatial.components.body_extent;
import engine.gameplay.common.health.systems.status_damage_system;
import engine.gameplay.rts.combat.systems.missile_jam_system;
import engine.gameplay.rts.combat.systems.firing_tracker_system;
import games.generalszh.gameplay.combat.algorithms.common_targets;
import games.generalszh.gameplay.combat.algorithms.attack_records;
import games.generalszh.gameplay.ai.systems.tunnel_guard_system;
import games.generalszh.gameplay.ai.algorithms.tunnel_guards;
import games.generalszh.gameplay.containment.systems.scripted_evacuation_system;
import games.generalszh.gameplay.ai.systems.guard_system;
import games.generalszh.gameplay.combat.systems.cleanup_hazard_system;
import games.generalszh.gameplay.powers.systems.spy_vision_system;
import games.generalszh.gameplay.powers.algorithms.spy_visions;
import games.generalszh.gameplay.combat.algorithms.flares;
import games.generalszh.gameplay.combat.algorithms.object_flown_launches;
import games.generalszh.gameplay.powers.algorithms.visible_payloads;
import games.generalszh.gameplay.upgrades.algorithms.grant_upgrade;
import games.generalszh.gameplay.production.components.cost_modifying;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.rts.combat.systems.countermeasures_system;
import games.generalszh.gameplay.ai.algorithms.guards;
import games.generalszh.gameplay.teams.algorithms.reinforcements;
import games.generalszh.gameplay.ai.systems.repulsion_system;
import games.generalszh.gameplay.ai.algorithms.repulsion;
import games.generalszh.gameplay.scripts.components.emptied_watch;
import games.generalszh.gameplay.score.algorithms.scoring;
import games.generalszh.gameplay.world.resources.deselections;
import engine.gameplay.common.appearance.components.indicator_color;
import games.generalszh.gameplay.ai.systems.attack_squad_system;
import games.generalszh.gameplay.ai.algorithms.attack_squads;
import games.generalszh.gameplay.movement.algorithms.unit_settles;
import games.generalszh.gameplay.world.resources.solo_play;
import games.generalszh.gameplay.appearance.systems.panic_look_system;
import engine.gameplay.rts.movement.components.wander_anchor;
import games.generalszh.gameplay.world.components.difficulty_bonus;
import games.generalszh.gameplay.combat.systems.pilot_kill_system;
import games.generalszh.gameplay.abilities.resources.ability_notices;
import engine.gameplay.common.status.components.ai_activity;
import engine.gameplay.common.status.systems.disable_systems;
import engine.gameplay.rts.slaves.systems.disable_follow_system;
import games.generalszh.gameplay.mines.algorithms.minefields;
import games.generalszh.gameplay.mines.algorithms.mine_clearing;
import games.generalszh.gameplay.academy.algorithms.academy_records;
import games.generalszh.gameplay.mines.systems.mine_clearing_detail_system;
import games.generalszh.gameplay.construction.systems.builder_boredom_system;
import games.generalszh.gameplay.construction.algorithms.builder_boredom;
import games.generalszh.content.mines.mine_content;
import engine.gameplay.rts.collision.systems.crush_system;
import engine.gameplay.rts.production.systems.production_system;
import engine.gameplay.rts.economy.systems.energy_system;
import engine.gameplay.rts.aircraft.systems.jet_system;
import games.generalszh.gameplay.production.algorithms.team_building;
import games.generalszh.gameplay.aircraft.algorithms.airfields;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.harvesting.systems.harvest_roster_system;
import engine.gameplay.rts.harvesting.systems.harvest_system;
import engine.gameplay.rts.docking.systems.dock_system;
import engine.gameplay.rts.docking.systems.repair_dock_system;
import engine.gameplay.rts.slaves.systems.hive_damage_system;
import games.generalszh.gameplay.combat.systems.cooldown_creation_system;
import games.generalszh.gameplay.combat.systems.deploy_system;
import games.generalszh.gameplay.powers.systems.launcher_door_system;
import games.generalszh.gameplay.powers.systems.particle_cannon_system;
import games.generalszh.gameplay.ai.systems.mob_member_system;
import games.generalszh.gameplay.ai.algorithms.mobs;
import games.generalszh.gameplay.ai.algorithms.slave_orders;
import games.generalszh.gameplay.crates.algorithms.car_bombs;
import games.generalszh.gameplay.crates.algorithms.sabotage;
import games.generalszh.gameplay.ai.algorithms.retaliation;
import games.generalszh.gameplay.powers.systems.spectre_gunship_system;
import games.generalszh.gameplay.powers.algorithms.spectre_gunships;
import engine.gameplay.rts.combat.systems.neutron_flight_system;
import games.generalszh.gameplay.production.systems.drone_repair_system;
import engine.gameplay.rts.loadout.systems.loadout_system;
import engine.gameplay.rts.sciences.resources.player_sciences;
import games.generalszh.gameplay.crates.systems.crate_touch_system;
import games.generalszh.gameplay.crates.algorithms.crate_rules;
import games.generalszh.content.topple.topple_content;
import games.generalszh.content.fire.fire_content;
import games.generalszh.content.physics.physics_content;
import engine.gameplay.rts.containment.resources.drop_settings;
import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.containment.algorithms.garrisons;
import games.generalszh.gameplay.containment.algorithms.tunnels;
import engine.gameplay.rts.containment.components.garrison;
import engine.gameplay.rts.containment.components.tunnel;
import engine.gameplay.common.poison.systems.poison_system;
import engine.gameplay.rts.construction.systems.sale_system;
import engine.gameplay.rts.construction.systems.construction_system;
import games.generalszh.gameplay.construction.algorithms.selling;
import engine.gameplay.rts.slaves.systems.spawner_system;
import games.generalszh.gameplay.appearance.systems.steering_look_system;
import games.generalszh.gameplay.appearance.systems.extension_look_system;
import games.generalszh.gameplay.appearance.systems.appearance_system;
import games.generalszh.gameplay.world.algorithms.level_setup;
import games.generalszh.gameplay.bridges.algorithms.bridges;
import games.generalszh.gameplay.walls.algorithms.walls;
import games.generalszh.gameplay.waveguide.algorithms.wave_guides;
import games.generalszh.gameplay.hacking.algorithms.hack_events;
import games.generalszh.gameplay.battleplans.algorithms.battle_plan_events;
import games.generalszh.gameplay.crates.algorithms.hijacking;
import games.generalszh.gameplay.economy.algorithms.warehouse_crippling;
import games.generalszh.gameplay.stealth.systems.supply_stealth_grant_system;
import games.generalszh.gameplay.combat.algorithms.battle_bus;
import games.generalszh.gameplay.combat.algorithms.weapon_bonus_pulses;
import games.generalszh.gameplay.creation.algorithms.ocl_timers;
import games.generalszh.gameplay.effects.algorithms.bone_fx_effects;
import games.generalszh.gameplay.effects.algorithms.transition_creations;
import games.generalszh.gameplay.containment.algorithms.parachute_landings;
import games.generalszh.gameplay.effects.systems.radius_decal_system;
import games.generalszh.gameplay.effects.resources.radius_decal_looks;
import games.generalszh.gameplay.railroad.systems.railroad_system;
import games.generalszh.gameplay.beacons.resources.beacons;
import games.generalszh.gameplay.railroad.systems.railroad_collision_system;
import games.generalszh.gameplay.railroad.algorithms.railroad_carriages;
import games.generalszh.gameplay.containment.algorithms.assault_transports;
import games.generalszh.gameplay.combat.algorithms.neutron_blasts;
import games.generalszh.gameplay.combat.algorithms.bunker_busters;
import games.generalszh.gameplay.combat.systems.enemy_near_system;
import games.generalszh.gameplay.powers.algorithms.leaflet_drops;
import games.generalszh.gameplay.combat.algorithms.firestorms;
import engine.gameplay.rts.containment.systems.drop_homing_system;
import engine.gameplay.rts.vision.systems.dynamic_clearing_system;
import games.generalszh.content.vision.dynamic_clearing_content;
import engine.gameplay.common.weapons.systems.temp_weapon_bonus_system;
import games.generalszh.gameplay.creation.algorithms.creation_list_runner;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.orders.algorithms.command_application;
import games.generalszh.session.script_bridge;
import games.generalszh.scripting.core_vocabulary;
import games.generalszh.scripting.legacy_calls;
import games.generalszh.content.objects.kind_of;

// The composition root of a running Zero Hour game. It owns the ECS world,
// the resources and the systems (run in parallel by the scheduler on all but
// one hardware thread) and wires the level's scripts to the gameplay domains;
// the rules themselves live in the systems (engine gameplay) and the domain
// algorithms (games/generalszh/gameplay). One Tick is one deterministic
// logic step: the tick's commands, the scripts, then the systems.
export namespace generalszh::session
{
namespace gameplay = engine::gameplay;
namespace domain = generalszh::gameplay;
using Engine::Math::Fixed;

class Session final : public SessionView
{
public:
	Session(const engine::level::Level &level, const content::GameContent &content, SessionOptions options) :
		Session(level, content, std::move(options), true)
	{
	}

	// A session in the state a checkpoint holds (taken by SaveCheckpoint of a
	// session on the same level, content and seed): for rejoining, late
	// observers and desync repair. Null when the checkpoint does not fit.
	static std::unique_ptr<Session> Restore(const engine::level::Level &level, const content::GameContent &content, SessionOptions options,
		std::span<const std::byte> checkpoint);

	// The whole simulation state between ticks: tick, random stream,
	// templates, teams, names, cargo, relationships, the ECS world and the
	// scripts. Equal sessions save equal StateHash; Restore continues exactly.
	void SaveCheckpoint(engine::core::serialization::ByteWriter &writer) const;
	std::vector<std::byte> Checkpoint() const;

private:
	Session(const engine::level::Level &level, const content::GameContent &content, SessionOptions options, bool place) :
		m_level(level), m_seed(options.seed),
		m_templates(m_world.EmplaceResource<domain::ObjectTemplates>(content, m_step, m_world.EmplaceResource<gameplay::WeaponCatalog>(),
			m_world.EmplaceResource<gameplay::ArmorCatalog>(), m_world.EmplaceResource<gameplay::DeathCatalog>(),
			m_world.EmplaceResource<gameplay::LaunchLayouts>(), m_world.EmplaceResource<gameplay::UpgradeTriggers>(),
			m_world.EmplaceResource<domain::UpgradeEffects>(), m_world.EmplaceResource<gameplay::VeterancyCatalog>(),
			m_world.EmplaceResource<gameplay::HordeCatalog>(), m_world.EmplaceResource<gameplay::HarvestCatalog>(),
			m_world.EmplaceResource<gameplay::LoadoutCatalog>(), m_world.EmplaceResource<gameplay::ParachuteCatalog>(),
			m_world.EmplaceResource<gameplay::DynamicClearingCatalog>(), m_world.EmplaceResource<gameplay::TemplateEquivalence>())),
		m_random(Engine::Math::Stream(options.seed, {0x5E55u})), m_seatNames(std::move(options.seats)), m_localSeat(options.localSeat),
		m_game{m_world, level, m_step, m_ground, m_waypoints, m_roster, m_names, m_manifest, m_kills, m_casualties, m_templates, m_teams, m_random, m_tick},
		m_bridge(m_game)
	{
		domain::AddWater(level, m_ground);
		m_world.EmplaceResource<gameplay::TriggerAreas>(domain::ReadTriggerAreas(level));
		m_world.EmplaceResource<gameplay::AreaActivity>();
		m_world.EmplaceResource<gameplay::AreaChanges>();
		m_world.EmplaceResource<gameplay::AttackPriorities>().distanceModifier = content.aiData.attackPriorityDistanceModifier;
		m_world.EmplaceResource<gameplay::MoodRanges>(gameplay::MoodRanges{content.aiData.guardOuterModifierHuman, content.aiData.guardOuterModifierAi,
			content.aiData.alertRangeModifier, content.aiData.aggressiveRangeModifier, *content::DamageTypeIndex("HEALING"), content.aiData.guardInnerModifierHuman,
			content.aiData.guardInnerModifierAi});
		// INI::parseDurationUnsignedInt: milliseconds to frames, rounded up.
		const auto frames = [&](Engine::Math::Fixed ms) {
			return static_cast<std::uint64_t>((ms * Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(m_step.TicksPerSecond())) / Engine::Math::Fixed::FromInt(1000)).Ceil());
		};
		if (content.aiData.forceIdleMs)
			m_world.Resource<gameplay::MoodRanges>().forceIdleTicks = frames(*content.aiData.forceIdleMs);
		m_world.Resource<gameplay::MoodRanges>().ignoreInsignificantBuildings = content.aiData.attackIgnoreInsignificantBuildings;
		m_world.EmplaceResource<gameplay::ChaseRules>(gameplay::ChaseRules{content.aiData.aiCrushesInfantry});
		m_world.EmplaceResource<domain::TunnelGuardRules>(domain::TunnelGuardRules{frames(content.aiData.guardEnemyScanRateMs), frames(content.aiData.guardChaseUnitsMs)});
		m_world.EmplaceResource<domain::GuardRules>(domain::GuardRules{frames(content.aiData.guardEnemyScanRateMs), frames(content.aiData.guardEnemyReturnScanRateMs),
			frames(content.aiData.guardChaseUnitsMs)});
		// AIData EnableRepulsors / RepulsedDistance; the idle look every IDLE_COUNTDOWN_DELAY (two seconds of frames).
		m_world.EmplaceResource<domain::RepulsionRules>(domain::RepulsionRules{content.aiData.enableRepulsors, content.aiData.repulsedDistance, 2 * m_step.TicksPerSecond()});
		domain::BuildNavigationGrid(m_game, m_navigation);
		// Whose goal each grid cell is (Pathfinder's goal claims).
		m_world.EmplaceResource<gameplay::GoalCells>().Fit(m_navigation.Width(), m_navigation.Height());
		// Multiplayer beacons: each player's side's BeaconName, and MaxBeaconsPerPlayer.
		{
			auto &beacons = m_world.EmplaceResource<domain::BeaconRules>();
			beacons.maxPerPlayer = content.multiplayer.maxBeaconsPerPlayer;
			m_world.EmplaceResource<domain::BeaconCues>();
		}
		// The level's waypoints as trains read them, and the tracks they lay.
		{
			std::vector<domain::RailMarker> markers;
			for (const auto &marker : level.markers)
				markers.push_back({marker.id, marker.name, marker.position.XY(), marker.properties.Get<bool>("waypointPathBiDirectional").value_or(false)});
			m_world.EmplaceResource<domain::RailWaypoints>(domain::BuildRailWaypoints(markers, level.markerLinks, m_game.ground));
			m_world.EmplaceResource<domain::RailTracks>();
			m_world.EmplaceResource<domain::RailroadRequests>();
			m_world.EmplaceResource<domain::RailroadImpulses>();
			m_world.EmplaceResource<domain::RailroadCues>();
			m_world.EmplaceResource<domain::RailroadDamage>(domain::RailroadDamage{*content::DamageTypeIndex("UNRESISTABLE"), *content::DamageTypeIndex("CRUSH"),
				content::DeathTypeIndex("NORMAL").value_or(0), content::DeathTypeIndex("CRUSHED").value_or(0)});
		}
		// Every radius decal template objects may lay (AttackNuggets', payload runs', neutron missiles'), in content order.
		{
			auto &decals = m_world.EmplaceResource<domain::RadiusDecalLooks>();
			for (const auto &[name, list] : content.creation)
				for (const content::CreationNugget &nugget : list.nuggets)
					decals.Intern(nugget.deliveryDecal);
			for (const auto &[name, runs] : content.powers.deliveries)
				for (const content::DeliveryNugget &run : runs)
					decals.Intern(run.deliveryDecal);
			for (const auto &[name, object] : content.objects)
			{
				if (const auto neutron = content::ReadNeutronMissile(object, m_step))
					decals.Intern(neutron->deliveryDecal);
				if (const auto payload = content::ReadDeliverPayloadModule(object, m_step))
					decals.Intern(payload->deliveryDecal);
			}
		}
		// Weapons of the damage type no armor resists may do no damage and still be chosen (chooseBestWeaponForTarget).
		m_templates.weapons.unresistable = *content::DamageTypeIndex("UNRESISTABLE");
		m_templates.weapons.normalDeath = content::DeathTypeIndex("NORMAL").value_or(0);
		m_templates.weapons.continuousFireMean = content::weapon_bonus::ContinuousFireMean;
		m_templates.weapons.continuousFireFast = content::weapon_bonus::ContinuousFireFast;
		m_templates.weapons.targetFaerieFire = content::weapon_bonus::TargetFaerieFire;
		m_templates.weapons.faerieFireStatus = content::ObjectStatusBit("FAERIE_FIRE");
		m_templates.deaths.crushDamageType = *content::DamageTypeIndex("CRUSH");
		m_templates.deaths.underConstructionStatus = std::uint64_t{1} << content::ObjectStatusBit("UNDER_CONSTRUCTION");
		// ActiveBody::attemptDamage's handled damage types: KILL_PILOT takes no health (ApplyPilotKills carries it out).
		m_templates.armors.SetHandled((std::uint64_t{1} << *content::DamageTypeIndex("KILL_PILOT")) | (std::uint64_t{1} << *content::DamageTypeIndex("KILL_GARRISONED")) |
			(std::uint64_t{1} << *content::DamageTypeIndex("STATUS")));
		m_templates.weapons.killGarrisoned = *content::DamageTypeIndex("KILL_GARRISONED");
		// The damage types and death estimateWeaponDamage treats apart.
		m_templates.weapons.sniper = *content::DamageTypeIndex("SNIPER");
		m_templates.weapons.surrender = *content::DamageTypeIndex("SURRENDER");
		m_templates.weapons.disarm = *content::DamageTypeIndex("DISARM");
		m_templates.weapons.deploy = *content::DamageTypeIndex("DEPLOY");
		m_templates.weapons.hack = *content::DamageTypeIndex("HACK");
		m_templates.weapons.killPilot = *content::DamageTypeIndex("KILL_PILOT");
		m_templates.weapons.burnedDeath = content::DeathTypeIndex("BURNED").value_or(gameplay::WeaponCatalog::None);
		m_templates.weapons.detonatedDeath = content::DeathTypeIndex("DETONATED").value_or(0);
		// IsSubdualDamage: taken as subdual damage by a body that can be subdued, by no other body.
		{
			std::uint64_t subdual = 0;
			for (const char *type : {"SUBDUAL_MISSILE", "SUBDUAL_VEHICLE", "SUBDUAL_BUILDING", "SUBDUAL_UNRESISTABLE"})
				subdual |= std::uint64_t{1} << *content::DamageTypeIndex(type);
			m_templates.armors.SetSubdual(subdual);
		}
		m_templates.armors.SetUnscaled(std::uint64_t{1} << *content::DamageTypeIndex("UNRESISTABLE"));
		// Object::onVeterancyLevelChanged: WEAPONSET_VETERAN, _ELITE, _HERO by level (regular: none).
		m_templates.loadouts.levelWeaponFlags = {0u, content::SetFlag(content::WeaponSetFlagNames, "VETERAN"),
			content::SetFlag(content::WeaponSetFlagNames, "ELITE"), content::SetFlag(content::WeaponSetFlagNames, "HERO")};
		m_templates.weapons.globalBonus = content.gameData.weaponBonus;
		// Hordes: HORDE, and NATIONALISM with Upgrade_Nationalism then FANATICISM with Upgrade_Fanaticism
		// (AIUpdateInterface::evaluateMoraleBonus, hasNationalism / hasFanaticism).
		m_templates.hordes.hordeBonus = content::weapon_bonus::Horde;
		m_templates.hordes.upgrade = content.upgrades.Find("Upgrade_Nationalism").value_or(gameplay::HordeCatalog::NoUpgrade);
		m_templates.hordes.upgradeBonus = content::weapon_bonus::Nationalism;
		m_templates.hordes.followerUpgrade = content.upgrades.Find("Upgrade_Fanaticism").value_or(gameplay::HordeCatalog::NoUpgrade);
		m_templates.hordes.followerBonus = content::weapon_bonus::Fanaticism;
		m_world.EmplaceResource<gameplay::HordeRoster>();
		// Veterancy: HealthBonus per level and the veterancy upgrades (UpgradeCenter's first three bits).
		m_templates.veterancy.healthBonus = content.gameData.healthBonus;
		// Object::onVeterancyLevelChanged: VETERAN, ELITE and HERO by level (regular: none).
		m_templates.veterancy.levelBonus = {0u, content::weapon_bonus::Veteran, content::weapon_bonus::Elite, content::weapon_bonus::Hero};
		m_templates.veterancy.levelUpgrade = {gameplay::VeterancyCatalog::NoUpgrade, content.upgrades.Find("Upgrade_Veterancy_VETERAN").value_or(gameplay::VeterancyCatalog::NoUpgrade),
			content.upgrades.Find("Upgrade_Veterancy_ELITE").value_or(gameplay::VeterancyCatalog::NoUpgrade),
			content.upgrades.Find("Upgrade_Veterancy_HEROIC").value_or(gameplay::VeterancyCatalog::NoUpgrade)};
		BuildWorld(options.jobs, options.workers, options.presentationComponents);
		m_world.Resource<domain::MapSceneryRules>() = domain::MapSceneryRules{options.scenery.useTrees, options.scenery.forceFluffToProp};
		domain::SetUpTeams(level.scenario, m_roster, m_relationships, m_teams);
		// PartitionManager::init: each player's shroud over the map's active boundary (TerrainLogic::getExtent).
		{
			const auto &terrain = level.terrain;
			Fixed width = terrain.cellSize * Fixed::FromInt(static_cast<std::int64_t>(terrain.width) - 1 - 2 * terrain.border);
			Fixed height = terrain.cellSize * Fixed::FromInt(static_cast<std::int64_t>(terrain.height) - 1 - 2 * terrain.border);
			if (!terrain.playableExtents.empty())
			{
				width = terrain.cellSize * Fixed::FromInt(terrain.playableExtents.front()[0]);
				height = terrain.cellSize * Fixed::FromInt(terrain.playableExtents.front()[1]);
			}
			m_world.Resource<gameplay::ShroudMap>().Init(width, height, content.gameData.partitionCellSize, m_roster.PlayerCount(), content.gameData.unlookPersistTicks);
		}
		// Player::isPlayableSide: its faction's PlayerTemplate PlayableSide.
		for (std::uint32_t player = 0; player < m_roster.PlayerCount() && player < level.scenario.participants.size(); ++player)
		{
			const std::string faction = level.scenario.participants[player].properties.Get<std::string>("playerFaction").value_or("");
			for (const auto &info : content.playerTemplates.templates)
				if (info.name == faction)
				{
					m_roster.PlayerAt(player).playable = info.playable;
					if (m_playerSides.size() <= player)
						m_playerSides.resize(player + 1);
					m_playerSides[player] = info.side;
					// Player::init: its AcademyStats (m_academyStats.init) for its template's base side.
					domain::InitAcademy(m_academy, player, info.baseSide, m_tick);
					if (m_playerTemplates.size() <= player)
						m_playerTemplates.resize(player + 1);
					m_playerTemplates[player] = info.name;
					// Player::initFromDict: its side's IntrinsicSciences.
					for (const std::string &science : info.intrinsicSciences)
						if (const auto bit = content.Science(science))
							m_ranks.Of(player).intrinsicSciences.push_back(*bit);
					m_ranks.Of(player).intrinsicPurchasePoints = info.intrinsicSciencePurchasePoints;
					// Player::getBeaconTemplate: its side's BeaconName.
					{
						auto &beacons = m_world.Resource<domain::BeaconRules>();
						if (beacons.beaconOf.size() <= player)
							beacons.beaconOf.resize(player + 1);
						beacons.beaconOf[player] = info.beaconName;
					}
				}
		}
		// Player::resetRank: rank 1 with its purchase points; resetSciences: the side's sciences, then each rank's so far.
		m_rankRules = BuildRankRules(content);
		m_powerRules = BuildPowerRules(content);
		for (std::uint32_t player = 0; player < m_roster.PlayerCount(); ++player)
			domain::StartRank(m_game, player);
		// Supply: ValuePerSupplyBox; computer players' trucks look twice as far for a warehouse
		// (SupplyTruckAIUpdate::getWarehouseScanDistance: PLAYER_COMPUTER, the map's non-human players).
		m_templates.harvest.valuePerBox = content.gameData.valuePerSupplyBox;
		m_templates.harvest.computer.assign(m_roster.PlayerCount(), 0);
		for (std::uint32_t player = 0; player < m_roster.PlayerCount(); ++player)
			for (const auto &participant : level.scenario.participants)
				if (participant.properties.Get<std::string>("playerName") == m_roster.PlayerAt(player).name)
				{
					const bool human = participant.properties.Get<bool>("playerIsHuman").value_or(false);
					m_templates.harvest.computer[player] = human ? 0 : 1;
					m_roster.PlayerAt(player).human = human;
				}
		m_soloPlay.singlePlayer = options.singlePlayer;
		m_soloPlay.difficulty = std::min<std::uint8_t>(options.difficulty, 2);
		m_soloPlay.challenge = options.singlePlayer && options.challenge;
		if (options.singlePlayer && !m_seatNames.empty())
			if (const auto local = m_roster.FindPlayer(m_seatNames.front()))
			{
				m_soloPlay.localPlayer = *local;
				if (m_soloPlay.challenge)
					domain::ApplyChallengeRelationships(m_roster, m_relationships, *local);
			}
		// PlayerList::newGame: a map with no human player (and no network) gives the first player but the neutral one to
		// the local human.
		if (options.starts.empty() && std::none_of(level.scenario.participants.begin(), level.scenario.participants.end(),
				[](const auto &participant) { return participant.properties.template Get<bool>("playerIsHuman").value_or(false); }))
			for (std::uint32_t player = 0; player < m_roster.PlayerCount(); ++player)
				if (!m_roster.PlayerAt(player).name.empty())
				{
					m_roster.PlayerAt(player).human = true;
					m_templates.harvest.computer[player] = 0;
					break;
				}
		// Player::init(pt): a player's template's StartMoney, or (none) the game's starting cash (GameInfo's: a skirmish's,
		// LAN game's or challenge's; else GlobalData's DefaultStartingCash); then initFromDict adds its map entry's
		// playerStartMoney. The neutral player (no name) starts with nothing.
		m_money.Resize(m_roster.PlayerCount());
		m_upgrades.Resize(m_roster.PlayerCount());
		m_sciences.Resize(m_roster.PlayerCount());
		const std::int64_t gameCash = level.properties.Get<std::int64_t>("startingCash").value_or(m_templates.Content().gameData.defaultStartingCash);
		for (std::uint32_t player = 0; player < m_roster.PlayerCount(); ++player)
		{
			const std::string &name = m_roster.PlayerAt(player).name;
			if (name.empty())
				continue;
			const engine::level::Participant *side = nullptr;
			for (const auto &participant : level.scenario.participants)
				if (participant.properties.Get<std::string>("playerName") == name)
					side = &participant;
			std::int64_t templateMoney = 0;
			if (side != nullptr)
			{
				const std::string faction = side->properties.Get<std::string>("playerFaction").value_or("");
				for (const auto &info : m_templates.Content().playerTemplates.templates)
					if (info.name == faction)
						templateMoney = info.startMoney;
			}
			std::int64_t cash = templateMoney != 0 ? templateMoney : gameCash;
			if (side != nullptr)
				cash += side->properties.Get<std::int64_t>("playerStartMoney").value_or(0);
			// Starting cash (setStartingCash / deposit(.., FALSE, FALSE)): no deposit sound; a map's playerStartMoney is
			// paid as the level loads, before anything is heard.
			m_money.Deposit(player, cash, false);
		}
		if (place)
		{
			domain::PlaceBridges(m_game);
			domain::PlaceBridgeLikeObjects(m_game);
			// Pathfinder::newMap: the wall layer of the wall pieces placed.
			domain::BuildWallLayer(m_game);
			// The bridges' decks in the pathfinding: every clearance plane afresh.
			domain::RebuildClearance(m_game);
			domain::PlaceObjects(m_game);
			for (const StartingPlayer &start : options.starts)
				if (const auto *faction = m_templates.Content().playerTemplates.At(start.playerTemplate))
					if (const auto team = m_roster.FindTeam(start.team))
						domain::PlaceStartingObjects(m_game, *faction, start.startPosition, *team);
			// Garrisons made with their occupants inside (InitialRoster) take on their occupants' player from the start.
			domain::TendGarrisons(m_game);
			// A game with seats (skirmish, LAN; not a campaign or challenge mission): the seats see the map (observers for
			// good; the rest fogged unless Multiplayer.ini's UseShroud).
			if (!options.starts.empty() && !options.singlePlayer)
			{
				std::vector<std::uint32_t> seated;
				std::vector<std::uint8_t> observers;
				for (const StartingPlayer &start : options.starts)
					if (const auto player = m_roster.FindPlayer(start.player))
					{
						const auto *faction = m_templates.Content().playerTemplates.At(start.playerTemplate);
						seated.push_back(*player);
						observers.push_back(faction != nullptr && faction->observer ? 1 : 0); // Player::isPlayerObserver: IsObserver
					}
				domain::RevealSeatsAtStart(m_game, seated, observers, m_templates.Content().multiplayer.useShroud);
			}
			// Each seat's default team (a computer player's is its side's, qualified).
			for (const StartingPlayer &start : options.starts)
				if (const auto player = m_roster.FindPlayer(start.player))
					m_roster.PlayerAt(*player).defaultTeam = start.team;
			// Player::setDefaultTeam activates it.
			for (std::uint32_t player = 0; player < m_roster.PlayerCount(); ++player)
				if (const auto team = m_roster.DefaultTeam(player))
					m_roster.SetActive(*team);
			// The computer players (AISkirmishPlayer::newMap): their base plans, their command centres put up anew.
			for (const StartingPlayer &start : options.starts)
			{
				const auto player = m_roster.FindPlayer(start.player);
				const auto team = m_roster.FindTeam(start.team);
				const auto *faction = m_templates.Content().playerTemplates.At(start.playerTemplate);
				if (!player || !team || faction == nullptr || m_roster.PlayerAt(*player).human)
					continue;
				std::int64_t difficulty = 1;
				for (const auto &participant : level.scenario.participants)
					if (participant.properties.Get<std::string>("playerName") == start.player)
						difficulty = participant.properties.Get<std::int64_t>("skirmishDifficulty").value_or(1);
				domain::SetUpSkirmishAi(m_game, m_aiPlayers, *player, static_cast<std::uint8_t>(difficulty), faction->side, *team);
			}
			// The map's own computer players (AIPlayer::newMap): their plans and the factories they were given.
			for (std::uint32_t player = 0; player < m_roster.PlayerCount(); ++player)
			{
				if (m_roster.PlayerAt(player).human || m_aiPlayers.Of(player) != nullptr)
					continue;
				const engine::level::Participant *mine = nullptr;
				for (const auto &participant : level.scenario.participants)
					if (participant.properties.Get<std::string>("playerName").value_or("") == m_roster.PlayerAt(player).name)
						mine = &participant;
				static const std::vector<engine::level::PlannedPlacement> none;
				// Player::setPlayerType: ForceSkirmishAI gives a map's computer player (a named one: the neutral player has no
				// AI) the skirmish AI (AISkirmishPlayer::newMap: its side's skirmish build list) instead.
				if (m_templates.Content().aiData.forceSkirmishAi && !m_roster.PlayerAt(player).name.empty())
					domain::SetUpSkirmishAi(m_game, m_aiPlayers, player, 1, player < m_playerSides.size() ? m_playerSides[player] : std::string{},
						m_roster.DefaultTeam(player).value_or(0));
				else
					domain::SetUpAi(m_game, m_aiPlayers, player, 1, mine != nullptr ? mine->plan : none, m_roster.DefaultTeam(player).value_or(0));
			}
		}
		// GameLogic::startNewGame, a new single-player game (not a saved one: its checkpoint replaces all this): each human
		// player takes the rank points the campaign carried in (Player::addSkillPoints).
		if (place && options.singlePlayer)
			for (std::uint32_t player = 0; player < m_roster.PlayerCount(); ++player)
				if (m_roster.PlayerAt(player).human)
					domain::AddSkillPoints(m_game, player, options.rankPointsAtStart);
		// A match's players (VictoryConditions::cachePlayerPtrs): its seats' players, but civilians and observers.
		if (!options.starts.empty())
		{
			m_outcome.enabled = true;
			for (std::uint32_t player = 0; player < m_roster.PlayerCount(); ++player)
				for (const StartingPlayer &start : options.starts)
				{
					const auto *faction = m_templates.Content().playerTemplates.At(start.playerTemplate);
					if (start.player == m_roster.PlayerAt(player).name && faction != nullptr && faction->name != "FactionCivilian" &&
						!faction->observer)
					{
						m_outcome.players.push_back({player});
						break;
					}
				}
		}
		scripting::AddCoreVocabulary(m_vocabulary);
		scripting::UseLegacyCallReading(m_vocabulary, content::KindOfNames);
		scripting::AddCameraVocabulary(m_vocabulary, level, {&m_cameraCommands, std::move(options.cameraMovementFinished)});
		scripting::AddUnitVocabulary(m_vocabulary, m_bridge);
		scripting::AddPlayerVocabulary(m_vocabulary, &m_bridge);
		m_bridge.SetSoundLengths(std::move(options.soundLengthTicks));
		m_cameraFreezesTime = std::move(options.cameraFreezesTime);
		m_timeFreezes = options.timeFreezes;
		m_bridge.SetSelectionQuery(std::move(options.selected));
		// Where a named unit is, or a team with units (its newest member, the head of the original's member list).
		scripting::AddPresentationVocabulary(m_vocabulary, m_clientCommands, &level,
			[this](bool team, const std::string &name) -> std::optional<Engine::Math::FixedVector3> {
				ecs::Entity at = m_names.Find(name);
				if (team)
				{
					const auto index = domain::ResolveTeam(m_game, name);
					if (!index || !domain::ai_team_detail::TeamHasAnyUnits(m_game, *index) || m_roster.TeamAt(*index).members.empty())
						return std::nullopt;
					at = m_roster.TeamAt(*index).members.back();
				}
				const auto *transform = m_world.IsAlive(at) ? m_world.Get<gameplay::Transform>(at) : nullptr;
				return transform != nullptr ? std::optional(transform->position) : std::nullopt;
			});
		// ScriptEngine::executeScript: a script runs at its player's difficulty (Player::getPlayerDifficulty: a computer
		// player's skirmishDifficulty, else the game's: ScriptEngine::getGlobalDifficulty, a campaign's own, else normal).
		engine::scripting::ScriptHooks hooks;
		const std::int64_t globalDifficulty = options.singlePlayer ? std::min<std::int64_t>(options.difficulty, 2) : 1;
		hooks.difficulty = [&scenario = level.scenario, globalDifficulty](std::size_t participant) {
			const auto level = participant < scenario.participants.size()
				? scenario.participants[participant].properties.Get<std::int64_t>("skirmishDifficulty").value_or(globalDifficulty) : globalDifficulty;
			return level == 0 ? engine::scripting::Difficulty::Easy : level == 2 ? engine::scripting::Difficulty::Hard : engine::scripting::Difficulty::Normal;
		};
		if (options.scriptTrace)
			hooks.actionRun = [](const engine::level::Script &script, const engine::level::ScriptCall &action, std::uint64_t tick) {
				// Its conditions with their parameters, before its first action.
				if (!script.actions.empty() && &action == &script.actions.front())
					for (const auto &clause : script.conditions)
					{
						std::cerr << "script " << tick << ' ' << script.name << " if";
						for (const auto &condition : clause)
						{
							std::cerr << ' ' << condition.name << '(';
							for (const auto &parameter : condition.parameters)
								std::cerr << parameter.text << (parameter.text.empty() ? std::to_string(parameter.integer) : std::string{}) << ',';
							std::cerr << ')';
						}
						std::cerr << '\n';
					}
				std::cerr << "script " << tick << ' ' << script.name << ": " << action.name << '\n';
			};
		// Sequential scripts run on teams (their subjects: team instances).
		hooks.sequenceSubject = [this](std::uint64_t subject) {
			if (domain::IsUnitSubject(subject))
				return static_cast<engine::scripting::ScriptHooks::SubjectState>(domain::UnitSequenceState(m_game, subject));
			return static_cast<engine::scripting::ScriptHooks::SubjectState>(domain::TeamSequenceState(m_game, subject));
		};
		m_scripts.emplace(level.scenario, m_vocabulary, std::move(hooks),
			engine::scripting::ScriptRuntimeOptions{m_step.TicksPerSecond(), options.seed});
		m_bridge.runActions = [this](const std::string &name) { m_scripts->RunNamedActions(name); };
		m_aiHooks.allowed = [this](const std::string &name) { return m_scripts->AllowedAtDifficulty(name); };
		m_aiHooks.delaySeconds = [this](const std::string &name) {
			const auto *script = m_scripts->FindScript(name);
			return script != nullptr ? script->evaluationDelaySeconds : 0u;
		};
		m_aiHooks.conditions = [this](const std::string &name) { return m_scripts->ConditionsHold(name); };
		m_aiHooks.actions = [this](const std::string &name, std::optional<std::uint32_t> team) {
			m_scripts->RunNamedActions(name, team ? std::uint64_t{*team} : engine::scripting::NoSubject);
		};
		// ScriptEngine::clearTeamFlags: the skirmish scripts' team-building flags.
		m_aiHooks.clearTeamFlags = [this] {
			for (const std::string_view flag : {"USA Team is Building", "USA Air Team Is Building", "USA Inf Team Is Building", "China Team is Building",
					 "China Air Team Is Building", "China Inf Team Is Building", "GLA Team is Building", "GLA Inf Team is Building"})
				m_scripts->SetFlag(flag, false);
		};
		m_bridge.aiHooks = &m_aiHooks;
		// The first snapshot, so the client can draw before the first tick.
		if (place)
			Execute();
	}

	bool LoadCheckpoint(engine::core::serialization::ByteReader &reader);
	// The state outside the ECS world (resources and scripts), in checkpoint order.
	void SaveResources(engine::core::serialization::ByteWriter &writer, std::vector<std::pair<std::string, std::size_t>> *marks = nullptr) const;
	bool LoadResources(engine::core::serialization::ByteReader &reader);

public:

	Session(const Session &) = delete;
	Session &operator=(const Session &) = delete;

	// One logic tick. `commands` is the tick's bundle from the lockstep
	// server (every player's orders, in canonical order); they apply before
	// the scripts and systems run. The per-tick outputs (camera and client
	// commands, casualties) hold this tick's only: read them after each Tick.
	void Tick(std::span<const engine::net::CommandEnvelope> commands = {})
	{
		if (m_tickProfiling)
			m_tickMark = std::chrono::steady_clock::now();
		m_cameraCommands.clear();
		m_clientCommands.clear();
		// Money::deposit's recordIncome for what came in since the last tick (at that frame).
		domain::RecordAcademyIncomes(m_game);
		composition::ClearTickEvents(m_world);
		++m_tick;
		for (const engine::net::CommandEnvelope &envelope : commands)
			if (const auto command = commands::Decode(envelope))
			{
				if (const auto *signal = std::get_if<commands::SignalUi>(&*command))
					m_scripts->Signal(signal->hook); // any seat's, as the original's signalUIInteract
				else if (const auto player = SeatPlayer(envelope.player))
					domain::ApplyCommand(m_game, *player, *command);
			}
		// Orders kept for transports whose combat drop is over.
		domain::ApplyDeferredOrders(m_game);
		TickMark(TickPart::Commands);
		m_scripts->Tick(m_tick);
		TickMark(TickPart::Scripts);
		// ThePlayerList->updateTeamStates: the teams' own scripts, once the side scripts have run.
		const domain::TeamScriptHooks teamHooks = TeamHooks();
		domain::UpdateTeamStates(m_game, teamHooks);
		TickMark(TickPart::TeamStates);
		// GameLogic::update: time frozen (by a script, or a camera move while it lasts) the frame ends with its scripts;
		// its number does not move on.
		m_timeFrozen = m_timeFreezes && (m_scriptRecords.timeFrozen || (m_cameraFreezesTime && m_cameraFreezesTime()));
		if (m_timeFrozen)
		{
			--m_tick;
			return;
		}
		// TerrainLogic::update: the water tables moving; the pathfinding map remade when one changed.
		if (domain::UpdateWaterLevels(m_game))
			domain::RefreshNavigationTerrain(m_game, m_navigation);
		// Transports made since the last tick take on their initial payloads (TransportContain::update's first pass).
		domain::CreateInitialPayloads(m_game);
		Execute();
		TickMark(TickPart::Systems);
		// The tick's pilot kills (ActiveBody's DAMAGE_KILLPILOT) and unmanned vehicles taken over.
		domain::ApplyPilotKills(m_game);
		domain::ApplyTakeOvers(m_game);
		// Bikes take on and let go of their riders (RiderChangeContain).
		domain::ApplyRiderChanges(m_game);
		// TransportContain's riders: armed riders' weapon set, riders out looking for targets at once.
		domain::ApplyTransportRiders(m_game);
		// Containers subdued this tick idle their passengers (onSubdualChange).
		domain::ApplySubdualChanges(m_game);
		// A human player's things struck this tick call their friends to strike back (ActiveBody's retaliation).
		domain::ApplyRetaliation(m_game);
		// Teams that attack together take the tick's first victims as theirs.
		domain::ApplyCommonTargets(m_game);
		// Who has attacked whom (Player::setAttackedBy).
		domain::ApplyAttackRecords(m_game);
		// The tunnel network guards' boarding, leaving, nemesis and team victim.
		domain::ApplyTunnelGuards(m_game);
		// The scripted evacuations' teams made active and transports removed.
		domain::ApplyScriptedEvacuations(m_game);
		// The flight decks: jets made, orders passed on, the queue moved up, replacements queued, jets healed.
		domain::ApplyFlightDeckEvents(m_game, m_world.Resource<domain::FlightDeckEvents>());
		// ParkingPlaceBehavior: the airfields' repairs of their parked jets.
		for (const domain::AirfieldHeal &heal : m_world.Resource<domain::AirfieldHeals>().list)
			if (auto *health = m_world.IsAlive(heal.jet) ? m_world.Get<gameplay::Health>(heal.jet) : nullptr; health != nullptr && !gameplay::IsDead(*health))
				health->current = std::min(health->maximum, health->current + heal.amount);
		// The railed transports: dockers captured and taken in, riders let out, paths set off on.
		domain::ApplyRailedTransportEvents(m_game);
		// Rappellers down from their ropes: into their buildings, or beside them.
		domain::ApplyRappelLandings(m_game, m_world.Resource<domain::RappelLandings>());
		// The guards' team victims cleared.
		domain::ApplyGuards(m_game);
		// Spy visions turned on or off: the enemies' things seen through.
		domain::ApplySpyVisions(m_game);
		// Countermeasure volleys: their flares out.
		domain::ApplyFlareLaunches(m_game);
		// ParticleUplinkCannonUpdate: each damage pulse's remnant (DamagePulseRemnantObjectName) on the cannon's team.
		for (const domain::ParticleCannonEvents::Remnant &remnant : m_world.Resource<domain::ParticleCannonEvents>().remnants)
			if (const domain::ParticleCannonConfig *config = m_templates.ParticleCannonOf(remnant.definition))
			{
				const ecs::Entity made = domain::SpawnObject(m_game, config->remnant, remnant.at.XY(), {}, remnant.team, "");
				if (m_world.IsAlive(made))
					m_world.Get<gameplay::Transform>(made)->position = remnant.at;
			}
		// Angry mob members: their looks' orders.
		domain::ApplyMobOrders(m_game);
		// Slaves (repair drones): their own locomotor sets and flags.
		domain::ApplySlaveOrders(m_game);
		// Spectre gunships: their orbit, gattling and howitzer orders.
		domain::ApplyGunshipEvents(m_game);
		// Superweapon missiles fired this tick: their objects out, flying from the next.
		domain::ApplyObjectFlownLaunches(m_game);
		// Payload carriers' visible payload items let go this tick: made, flying from the next.
		domain::ApplyVisibleDrops(m_game);
		// FireOCLAfterWeaponCooldownUpdate::fireOCL: ObjectCreationList::create(ocl, obj, obj, frames).
		domain::RunCooldownCreations(m_game);
		// Hurt runners scare the others; runners run from repulsors, or wander once their run is over.
		domain::ApplyRepulsorMarks(m_game);
		domain::ApplyRepulsions(m_game);
		// Units whose squad has nobody left idle.
		domain::ApplySquadsDone(m_game);
		// Units following a path as a team: the team on to its next waypoint, the others after it.
		domain::ApplyTeamPathFollows(m_game);
		// Idle units on top of one another step clear (processCollision, not moving).
		domain::ApplyUnitSettles(m_game);
		domain::CarryOutDeathAftermath(m_game);
		// DamDie::onDie (after the dam's creation lists): the flood waves enabled.
		domain::ApplyDamDie(m_game, m_casualties.list);
		// NeutronBlastBehavior::onDie: the tick's neutron deaths and neutron shells that died detonating.
		domain::ApplyNeutronBlasts(m_game, m_casualties, m_world.Resource<gameplay::Detonations>());
		// BunkerBusterBehavior::bustTheBunker: the tick's bunker busters that went off.
		domain::ApplyBunkerBusters(m_game, m_world.Resource<gameplay::MissileDetonations>());
		// LeafletDropBehavior: new containers' leaflets, and the leaflet drops that came down.
		domain::ApplyLeafletDrops(m_game, m_casualties);
		// FirestormDynamicGeometryInfoUpdate: the firestorms' effects, scorches and damage scans.
		domain::ApplyFirestorms(m_game);
		// Object::onDie's booby traps and the special objects that do not outlive their owners; then the sticky bombs
		// whose targets died (StickyBombSystem).
		domain::ApplySpecialObjectCasualties(m_game, m_casualties.list);
		// Airfields and flight decks killed take their jets on the ground with them.
		domain::KillParkedJets(m_game, m_casualties.list);
		domain::ApplyStickyBombEvents(m_game);
		// BridgeBehavior: the bridges' damage-state transitions and due death effects, then those that died.
		domain::ApplyBridgeEvents(m_game, m_casualties.list);
		// The wall's pieces gone or fallen to rubble: out of the wall, what stood on them falling.
		domain::ApplyWallEvents(m_game, m_casualties.list);
		// WaveGuideUpdate: the flood waves set off, removed, their victims made wet and the bridges they broke replaced.
		domain::ApplyWaveGuideEvents(m_game);
		// HackInternetAIUpdate: the hackers' pay.
		{
			std::vector<domain::HackEvent> hacks;
			m_world.Resource<domain::HackEvents>().AppendTo(hacks);
			m_world.Resource<domain::HackEvents>().Reset(0);
			domain::ApplyHackEvents(m_game, hacks);
		}
		// BattlePlanUpdate: the plans given and taken, paralysis and idling; a center gone takes its plan back.
		{
			std::vector<domain::BattlePlanEvent> plans;
			m_world.Resource<domain::BattlePlanEvents>().AppendTo(plans);
			m_world.Resource<domain::BattlePlanEvents>().Reset(0);
			domain::ApplyBattlePlanEvents(m_game, plans);
			domain::ApplyBattlePlanCasualties(m_game, m_casualties.list);
		}
		// UndeadBody: second lives begun (a battle bus's first death), and the battle buses thrown, landed or emptied.
		domain::ApplySecondLives(m_game);
		domain::ApplyBattleBusEvents(m_game);
		// WeaponBonusUpdate: the pulses' temporary weapon bonuses.
		domain::ApplyWeaponBonusPulses(m_game);
		// OCLUpdate: the creation timers' lists (a supply drop zone's cargo plane).
		domain::ApplyOclTimers(m_game, m_playerSides);
		// BoneFXUpdate: the FX lists and creation lists played at bones this tick.
		domain::ApplyBoneFx(m_game);
		// TransitionDamageFX: the creation lists of the hits that left their targets worse off.
		domain::ApplyTransitionCreations(m_game);
		// ParachuteContain::onRemoving: what the riders set down this tick do next.
		domain::ApplyParachuteLandings(m_game);
		domain::ApplyRailroadRequests(m_game);
		domain::ApplyRailroadImpulses(m_game);
		// DAMAGE_DISARM: the mines and traps disarmed this tick, and the clearers going on to the next.
		domain::ApplyDisarms(m_game);
		// WeaponSet::updateWeaponSet: what had no weapons armed by the set its flags now pick.
		domain::ApplyLoadoutArmings(m_game);
		// DozerPrimaryIdleState: the bored dozers and workers finding something to repair, or a mine to clear.
		domain::ApplyBoredBuilders(m_game);
		// AssaultTransportAIUpdate: the DEPLOY shots' designated enemies, then the troop crawlers' orders.
		domain::BeginAssaults(m_game, m_fired);
		domain::ApplyAssaultOrders(m_game);
		// SupplyWarehouseDockUpdate::setDockCrippled: whoever a crippled warehouse had let in.
		domain::ApplyWarehouseCrippling(m_game);
		// HijackerUpdate: riding with the vehicle, or let out once it is gone.
		{
			std::vector<domain::HijackerEvent> hijackers;
			m_world.Resource<domain::HijackerEvents>().AppendTo(hijackers);
			m_world.Resource<domain::HijackerEvents>().Reset(0);
			domain::ApplyHijackerEvents(m_game, hijackers);
		}
		domain::LayDeathMinefields(m_game);
		// Anything that is no projectile or inert destroyed changes what the areas hold (Object's destructor).
		for (const gameplay::Casualty &casualty : m_casualties.list)
		{
			const content::ObjectDefinition &object = m_templates.DefinitionAt(casualty.definition);
			if (!object.Is("PROJECTILE") && !object.Is("INERT"))
			{
				m_world.Resource<gameplay::AreaActivity>().lastChange = m_tick;
				break;
			}
		}
		domain::RunShotCreationLists(m_game);
		domain::LeaveToppleStumps(m_game);
		domain::ThrowFireEmbers(m_game);
		domain::CompleteProduction(m_game);
		// What the abilities (SpecialAbilitySystem) did beyond their units; then the command button hunts.
		domain::ApplyAbilityEvents(m_game);
		domain::ApplyHuntScans(m_game);
		TickMark(TickPart::Aftermath);
		// ThePlayerList->update after the tick's objects: each player's AI, then its teams' generic scripts.
		for (std::uint32_t player = 0; player < m_game.roster.PlayerCount(); ++player)
		{
			if (domain::AiPlayer *ai = m_aiPlayers.Of(player))
				domain::UpdateAiPlayer(m_game, m_aiPlayers, *ai, m_aiHooks);
			TickMark(TickPart::AiPlayers);
			domain::UpdateGenericScripts(m_game, player, teamHooks);
			TickMark(TickPart::GenericScripts);
			// Player::update: its AcademyStats (update).
			domain::UpdateAcademy(m_game, player);
		}
		domain::RunUpgradeCreations(m_game);
		domain::TendMinefields(m_game);
		domain::CompleteCratePickups(m_game);
		domain::CompleteSpawns(m_game);
		domain::FinishSales(m_game);
		domain::TendGarrisons(m_game);
		domain::TendTunnels(m_game);
		domain::TendRebuildHoles(m_game);
		// The tick's boardings set off their containers' booby traps (OpenContain::addToContain).
		domain::ApplyBoobyTrapEntries(m_game);
		// Internet Centers set the hackers they take in hacking (InternetHackContain::onContaining).
		domain::ApplyInternetHackCargo(m_game);
		// The academy's records of the tick: incomes, boardings into garrisons and tunnels, garrisons cleared, disguises.
		domain::RecordAcademyIncomes(m_game);
		domain::RecordAcademyCargo(m_game);
		domain::RecordAcademyGarrisonClears(m_game);
		domain::RecordAcademyDisguises(m_game);
		TickMark(TickPart::Tending);
	}

	// Where a tick's time goes outside the systems (profiling on): by part, summed nanoseconds.
	enum class TickPart : std::uint8_t
	{
		Commands,
		Scripts,
		TeamStates,
		Systems,
		Aftermath,
		AiPlayers,
		GenericScripts,
		Tending,
		Count,
	};
	std::array<std::uint64_t, static_cast<std::size_t>(TickPart::Count)> TickNanos() const noexcept { return m_tickNanos; }

	// SpecialPowerStore as the power rules: each template's reload, science and shared timer, by index.
	static gameplay::SpecialPowerRules BuildPowerRules(const content::GameContent &content)
	{
		gameplay::SpecialPowerRules rules;
		for (const content::SpecialPowerTemplate &power : content.powers.templates)
		{
			gameplay::SpecialPowerRule rule{power.reloadTicks, gameplay::SpecialPowerRule::NoScience, power.sharedSynced, power.publicTimer};
			if (const auto science = content.Science(power.requiredScience))
				rule.requiredScience = *science;
			rules.powers.push_back(rule);
		}
		return rules;
	}

	// RankInfoStore and ScienceStore as the rank rules: sciences by bit.
	static gameplay::RankRules BuildRankRules(const content::GameContent &content)
	{
		gameplay::RankRules rules;
		for (const content::RankInfo &rank : content.ranks)
		{
			gameplay::RankLevel level{rank.skillPointsNeeded, rank.purchasePointsGranted, {}};
			for (const std::string &science : rank.sciencesGranted)
				if (const auto bit = content.Science(science))
					level.sciencesGranted.push_back(*bit);
			rules.ranks.push_back(std::move(level));
		}
		for (const content::ScienceInfo &science : content.scienceInfo)
		{
			gameplay::ScienceRule rule{science.purchaseCost, science.grantable, {}};
			for (const std::string &needed : science.prerequisites)
				if (const auto bit = content.Science(needed))
					rule.prerequisites.push_back(*bit);
			rules.sciences.push_back(std::move(rule));
		}
		// ScienceInfo::addRootSciences as each is read: a science without prerequisites is its own root; else the roots of
		// its prerequisites already read (those read later are not in the store yet).
		for (std::uint32_t science = 0; science < rules.sciences.size(); ++science)
		{
			std::vector<std::uint32_t> roots;
			const auto add = [&](const auto &self, std::uint32_t at) -> void {
				const gameplay::ScienceRule &rule = rules.sciences[at];
				if (rule.prerequisites.empty())
				{
					if (std::find(roots.begin(), roots.end(), at) == roots.end())
						roots.push_back(at);
					return;
				}
				for (const std::uint32_t needed : rule.prerequisites)
					if (needed <= science && needed != at)
						self(self, needed);
			};
			add(add, science);
			rules.sciences[science].roots = std::move(roots);
		}
		return rules;
	}

public:
	const gameplay::PlayerRank *RankOf(std::uint32_t player) const { return m_ranks.Find(player); }
	bool KnowsScience(std::uint32_t player, std::string_view science) const
	{
		const auto bit = m_templates.Content().Science(science);
		return bit && m_sciences.Has(player, *bit);
	}

	// How the teams reach this session's scripts (ScriptEngine::runScript / evaluateConditions / friend_executeAction).
	domain::TeamScriptHooks TeamHooks()
	{
		domain::TeamScriptHooks hooks;
		hooks.run = [this](const std::string &name, std::uint32_t team) { m_scripts->CallSubroutine(name, team); };
		hooks.evaluate = [this](const std::string &name, std::uint32_t team) { return m_scripts->EvaluateNamed(name, team); };
		hooks.actions = [this](const std::string &name, std::uint32_t team) { m_scripts->RunNamedActions(name, team); };
		hooks.oneShot = [this](const std::string &name) {
			const auto *script = m_scripts->FindScript(name);
			return script != nullptr && script->oneShot;
		};
		hooks.exists = [this](const std::string &name) { return m_scripts->HasScript(name); };
		hooks.destroying = [this](std::uint32_t team) { domain::AiPreTeamDestroy(m_aiPlayers, team); };
		return hooks;
	}

	// Shots fired this tick, in deterministic order (for muzzle flashes and fire sounds).
	const gameplay::FiredShots &Fired() const noexcept override { return m_fired; }
	// Shots that landed this tick (for the client's effects).
	const std::vector<gameplay::Impact> &Impacts() const noexcept override { return m_shots.Impacts(); }
	const content::WeaponContent *WeaponContentOf(std::uint32_t weapon) const override { return m_templates.WeaponContentAt(weapon); }
	std::uint32_t WeaponCount() const override { return m_templates.WeaponCount(); }
	std::uint32_t ArmorCount() const override { return static_cast<std::uint32_t>(m_templates.armors.Size()); }
	std::string_view ArmorName(std::uint32_t armor) const override { return m_templates.ArmorName(armor); }
	const content::ObjectDefinition *ObjectNamed(std::string_view name) const override { return m_templates.Content().objects.Find(name); }
	const gameplay::PointDefenseShots &DefenseShots() const noexcept override { return m_world.Resource<gameplay::PointDefenseShots>(); }
	// Who left the world this tick and how (for death effects).
	const std::vector<gameplay::Casualty> &Casualties() const noexcept { return m_casualties.list; }

	// What dying entities played this tick, in deterministic order: the
	// moment of death first, then slow deaths playing out.
	// Teams an AI is still building.

	// How many jets are in each state of their airfield cycle (by JetState).
	std::array<std::size_t, 12> JetStateCounts() const
	{
		std::array<std::size_t, 12> counts{};
		ecs::Query<ecs::Read<gameplay::Jet>> jets(const_cast<ecs::World &>(m_world));
		jets.ForEachChunk([&](auto chunk) {
			for (const gameplay::Jet &jet : chunk.template Get<gameplay::Jet>())
				if (static_cast<std::size_t>(jet.state) < counts.size())
					++counts[static_cast<std::size_t>(jet.state)];
		});
		return counts;
	}

	// Things that started to topple or bounced this tick (for their effects).
	std::vector<gameplay::ToppleEvent> ToppleEvents() const override
	{
		std::vector<gameplay::ToppleEvent> events;
		m_world.Resource<gameplay::ToppleEvents>().ForEach([&](const gameplay::ToppleEvent &event) { events.push_back(event); });
		return events;
	}
	// A definition's name, by index.
	std::string_view DefinitionName(std::uint32_t index) const { return m_templates.DefinitionAt(index).name; }
	// The definition an entity was made from, while it lives.
	std::optional<std::uint32_t> DefinitionOf(ecs::Entity entity) const override
	{
		const auto *definition = m_world.IsAlive(entity) ? m_world.Get<gameplay::DefinitionRef>(entity) : nullptr;
		return definition != nullptr ? std::optional(definition->index) : std::nullopt;
	}

	std::vector<CargoMove> CargoMoves() const override
	{
		std::vector<CargoMove> moves;
		const auto find = [&](ecs::Entity entity, std::uint32_t &definition, Engine::Math::FixedVector3 &at) {
			if (m_world.IsAlive(entity))
				if (const auto *ref = m_world.Get<gameplay::DefinitionRef>(entity))
					if (const auto *transform = m_world.Get<gameplay::Transform>(entity))
					{
						definition = ref->index;
						at = transform->position;
						return true;
					}
			for (const gameplay::Casualty &casualty : m_casualties.list)
				if (casualty.entity == entity)
				{
					definition = casualty.definition;
					at = casualty.transform.position;
					return true;
				}
			return false;
		};
		for (const gameplay::CargoChange &change : m_world.Resource<gameplay::CargoManifest>().Changes())
		{
			if (change.quiet)
				continue;
			CargoMove move;
			move.entered = change.entered;
			move.hasContainer = find(change.container, move.containerDefinition, move.containerAt);
			move.hasRider = find(change.rider, move.riderDefinition, move.riderAt);
			moves.push_back(move);
		}
		return moves;
	}

	std::vector<gameplay::DeathEvent> DeathEvents() const override
	{
		std::vector<gameplay::DeathEvent> events = m_deathEvents.events;
		m_dyingEvents.ForEach([&](const gameplay::DeathEvent &event) { events.push_back(event); });
		m_structureToppleEvents.ForEach([&](const gameplay::DeathEvent &event) { events.push_back(event); });
		return events;
	}
	// A model shown instead of a definition's (debris), by its ModelOverride id.
	std::string_view ModelName(std::uint32_t id) const override { return m_templates.ModelName(id); }
	// A death effect's name (an FX list, object creation list or weapon).
	std::string_view DeathEffectName(gameplay::DeathEffectKind kind, std::uint32_t id) const override { return m_templates.DeathEffectName(kind, id); }

	std::uint64_t CurrentTick() const noexcept override { return m_tick; }
	// Places an object of a player's team, as a map or script would (tests, tools, debugging).
	ecs::Entity Place(const std::string &type, Engine::Math::FixedVector2 position, Engine::Math::TurnAngle facing, std::uint32_t team)
	{
		const ecs::Entity made = domain::SpawnObject(m_game, type, position, facing, team, "");
		domain::CreateModulesBuildComplete(m_game, made); // placed as the map places its objects: built
		return made;
	}
	const gameplay::VisibleObjects &Visible() const noexcept override { return m_visible; }
	const content::ObjectDefinition &Definition(std::uint32_t index) const override { return m_templates.DefinitionAt(index); }
	bool DefinitionHasAi(std::uint32_t index) const noexcept override { return m_templates.HasAI(index); }
	bool RetaliationEnabled(std::uint32_t player) const noexcept override
	{
		const auto *modes = m_world.FindResource<domain::RetaliationModes>();
		return modes != nullptr && modes->On(player);
	}
	// The game's content (definitions, locomotors, ...), for the presentation to read.
	const content::GameContent &Content() const noexcept override { return m_templates.Content(); }
	std::size_t DefinitionCount() const noexcept override { return m_templates.DefinitionCount(); }
	std::vector<scripting::CameraScriptCommand> &CameraCommands() noexcept override { return m_cameraCommands; }
	// What the scripts asked the client to show or play this tick (music, speech, radar, ...).
	std::vector<scripting::ClientScriptCommand> &ClientCommands() noexcept override { return m_clientCommands; }
	std::size_t EntityCount() const noexcept override { return m_world.EntityCount(); }
	std::string CommandSetOf(ecs::Entity entity) const override { return domain::CommandSetOf(m_game, entity); }
	domain::ButtonState CommandAvailability(ecs::Entity entity, const content::CommandButtonContent &button) override
	{
		return domain::CommandAvailability(m_game, entity, button);
	}
	domain::BuildTooltipFacts BuildTooltip(std::uint32_t player, ecs::Entity selected, const content::CommandButtonContent &button) override
	{
		return domain::ReadBuildTooltipFacts(m_game, player, selected, button);
	}
	bool QuickPathAvailable(ecs::Entity unit, Engine::Math::FixedVector2 to) override { return domain::QuickPathAvailable(m_game, unit, to); }
	bool CanBuildAt(ecs::Entity builder, std::string_view structure, Engine::Math::FixedVector2 at, Engine::Math::TurnAngle facing,
		bool specialPowerConstruct) override
	{
		const content::ObjectDefinition *what = m_templates.Content().objects.Find(structure);
		return what != nullptr && domain::CanMakeUnit(m_game, builder, *what, specialPowerConstruct) == domain::CanMake::Ok &&
			domain::CheckBuildLocation(m_game, *what, at, facing, builder) == domain::LegalBuild::Ok;
	}
	bool CanCombatDropInto(ecs::Entity transport, ecs::Entity target) override
	{
		return m_templates.CombatDropOf(m_world.IsAlive(transport) && m_world.Has<gameplay::DefinitionRef>(transport) ? m_world.Get<gameplay::DefinitionRef>(transport)->index : 0xFFFFFFFFu) != nullptr &&
			domain::CombatDropTarget(m_game, transport, target);
	}
	bool CanTargetWithPower(ecs::Entity source, std::string_view power, ecs::Entity target) override
	{
		const auto index = m_templates.Content().powers.Template(power);
		return index && domain::PowerModuleFor(m_game, source, std::string(power), false) != nullptr && domain::CanTargetWithPower(m_game, source, *index, target);
	}
	std::uint32_t CommandClock(ecs::Entity entity, const content::CommandButtonContent &button) override
	{
		return domain::CommandClock(m_game, entity, button);
	}
	std::optional<ecs::Entity> ShortcutPowerSource(std::uint32_t player, std::string_view type) override
	{
		return domain::MostReadyShortcutPower(m_game, player, type);
	}
	bool HasAnyShortcutPower(std::uint32_t player) override { return domain::HasAnyShortcutPower(m_game, player); }
	std::int32_t ReadyShortcutPowers(std::uint32_t player, std::string_view type) override
	{
		return domain::CountReadyShortcutPowers(m_game, player, type);
	}
	std::optional<ecs::Entity> AnyObjectOfType(std::uint32_t player, const std::string &object) override
	{
		return domain::AnyExistingObjectOfType(m_game, player, object);
	}
	std::vector<ecs::Entity> ObjectsOfType(std::uint32_t player, const std::string &object) override
	{
		return domain::PlayerObjectsOfType(m_game, player, object);
	}
	std::optional<ecs::Entity> CommandCenterToView(std::uint32_t player) override { return domain::CommandCenterToView(m_game, player); }
	std::optional<ecs::Entity> HeroToSelect(std::uint32_t player) const override { return domain::FirstHero(m_game, player); }
	std::optional<ecs::Entity> MostReadyPowerOfType(std::uint32_t player, const std::string &object) override
	{
		return domain::MostReadySpecialPowerForThing(m_game, player, object);
	}
	std::optional<std::uint32_t> DefinitionIndex(std::string_view name) override
	{
		const content::ObjectDefinition *what = m_templates.Content().objects.Find(name);
		return what != nullptr ? std::optional(m_templates.Definition(*what)) : std::nullopt;
	}
	std::string PlayerSide(std::uint32_t player) const override { return player < m_playerSides.size() ? m_playerSides[player] : std::string{}; }
	std::uint32_t LocalSeat() const noexcept override { return m_localSeat; }
	std::vector<PlayerScore> Scores() const override
	{
		std::vector<PlayerScore> scores;
		const auto local = SeatPlayer(m_localSeat);
		const auto &money = m_world.Resource<gameplay::PlayerMoney>();
		for (std::uint32_t player = 0; player < m_roster.PlayerCount(); ++player)
		{
			const gameplay::Player &record = m_roster.PlayerAt(player);
			PlayerScore &score = scores.emplace_back();
			score.name = record.name;
			score.playerTemplate = player < m_playerTemplates.size() ? m_playerTemplates[player] : std::string{};
			score.human = record.human;
			score.listed = record.listInScoreScreen;
			score.playable = record.playable;
			score.local = local && *local == player;
			if (local)
			{
				const auto relation = *local == player ? gameplay::Relationship::Allies : m_relationships.Between(*local, player);
				score.relation = relation == gameplay::Relationship::Allies ? 1 : relation == gameplay::Relationship::Enemies ? 2 : 0;
			}
			if (const domain::ScoreKeeper *keeper = m_scoreKeepers.Find(player))
			{
				score.unitsBuilt = keeper->unitsBuilt;
				score.unitsLost = keeper->unitsLost;
				score.buildingsBuilt = keeper->buildingsBuilt;
				score.buildingsLost = keeper->buildingsLost;
				score.moneySpent = keeper->moneySpent;
				for (std::size_t other = 0; other < domain::ScorePlayers; ++other)
				{
					score.unitsDestroyed += keeper->unitsDestroyed[other];
					score.buildingsDestroyed += keeper->buildingsDestroyed[other];
				}
			}
			score.moneyEarned = money.Earned(player);
			score.score = domain::Score(m_game, player);
		}
		return scores;
	}
	std::int64_t CountPlayerObjects(std::uint32_t player, const std::string &types, bool ignoreDead) const override
	{
		const auto &records = m_world.Resource<domain::ScriptRecords>();
		return domain::CountPlayerObjects(m_game, player, domain::ObjectTypesFrom(m_game, records, types), ignoreDead);
	}
	std::string PlayerTemplateName(std::uint32_t player) const override { return player < m_playerTemplates.size() ? m_playerTemplates[player] : std::string{}; }
	ecs::World &World() noexcept override { return m_world; }
	const ecs::World &World() const noexcept override { return m_world; }
	// The game state the rules act on (for the game's own algorithms and tests).
	domain::GameWorld &Game() noexcept { return m_game; }
	std::size_t WorkerCount() const noexcept override { return m_workers; }
	// Everything a checkpoint holds, hashed: equal on every peer while in step.
	ecs::StateHashValue StateHash() const;
	// StateHash's session resources one by one (name, hash of its saved bytes), for finding what a desync or a checkpoint
	// leaves out.
	std::vector<std::pair<std::string, ecs::StateHashValue>> ResourceHashes() const;
	const gameplay::TeamRoster &Teams() const noexcept { return m_roster; }
	// The entity a script name refers to (invalid when none).
	ecs::Entity Named(std::string_view name) const override { return m_names.Find(name); }
	std::vector<ecs::Entity> TeamMembers(std::string_view team) override
	{
		const auto index = domain::ResolveTeam(m_game, std::string(team));
		if (!index)
			return {};
		const auto &members = m_roster.TeamAt(*index).members;
		return {members.rbegin(), members.rend()};
	}
	Engine::Math::FixedVector2 PlayableExtent() const override { return m_ground.Extent()[1]; }
	bool TimeFrozen() const noexcept override { return m_timeFrozen; }
	bool FlyingAboveSurface(ecs::Entity entity) const override
	{
		const auto *transform = m_world.IsAlive(entity) ? m_world.Get<gameplay::Transform>(entity) : nullptr;
		const auto *motion = transform != nullptr ? m_world.Get<gameplay::Locomotion>(entity) : nullptr;
		return motion != nullptr && gameplay::IsAirborne(motion->locomotor) && transform->position.z > m_ground.Surface(transform->position.XY());
	}
	// A script counter's value (for tests).
	std::int64_t ScriptCounter(std::string_view name) const override { return m_scripts ? m_scripts->Counter(name) : 0; }
	std::optional<gameplay::Transform> TransformOf(ecs::Entity entity) const
	{
		const auto *transform = m_world.IsAlive(entity) ? m_world.Get<gameplay::Transform>(entity) : nullptr;
		return transform != nullptr ? std::optional(*transform) : std::nullopt;
	}
	std::int64_t PlayerObjectCount(const std::string &player, const std::string &type) const { return domain::PlayerObjectCount(m_game, player, type); }

	std::string UnportedSummary() const override
	{
		std::string summary = std::to_string(m_scripts->UnknownConditions().size()) + " conditions, " +
			std::to_string(m_scripts->UnknownActions().size()) + " actions unported:";
		for (const auto &name : m_scripts->UnknownConditions())
			summary += " " + name;
		for (const auto &name : m_scripts->UnknownActions())
			summary += " " + name;
		return summary;
	}

private:
	void BuildWorld(engine::jobs::JobSystem *jobs, std::size_t workers, const std::function<void(ecs::World &)> &presentationComponents)
	{
		const auto &terrain = m_level.terrain;
		const Fixed playableWidth = terrain.cellSize * Fixed::FromInt(static_cast<std::int64_t>(terrain.width) - 1 - 2 * terrain.border);
		const Fixed playableHeight = terrain.cellSize * Fixed::FromInt(static_cast<std::int64_t>(terrain.height) - 1 - 2 * terrain.border);
		const composition::SimulationSetup setup{m_templates.Content(), m_level, m_step, m_seed, {playableWidth, playableHeight}};
		composition::ComposeSimulation(m_world, m_registry, setup, presentationComponents);
		m_spatial = gameplay::SpatialIndex({playableWidth, playableHeight}, Fixed::FromInt(100));
		m_registry.Finalize(m_world.Components());
		const std::size_t hardware = std::max<std::size_t>(std::thread::hardware_concurrency(), 2);
		if (jobs != nullptr)
		{
			m_workers = jobs->WorkerCount();
			m_scheduler.emplace(m_world, m_registry, *jobs);
		}
		else
		{
			m_workers = workers != 0 ? workers : hardware - 1;
			m_scheduler.emplace(m_world, m_registry, engine::jobs::JobSystemConfig{m_workers});
		}
		m_scheduler->Finalize(m_step);
	}

	// The map player a lockstep seat plays; none for observers.
	std::optional<std::uint32_t> SeatPlayer(std::uint32_t seat) const override
	{
		return seat < m_seatNames.size() ? m_roster.FindPlayer(m_seatNames[seat]) : std::nullopt;
	}

	void Execute() { m_scheduler->Execute(engine::time::SimulationTime{m_tick, m_step}); }

public:
	// Where the ticks' time goes (profiling off unless enabled).
	std::vector<engine::scripting::ScriptRuntime::CallTiming> ScriptProfile() const { return m_scripts->Profile(); }
	void EnableProfiling(bool enabled)
	{
		m_scripts->EnableProfiling(enabled);
		m_scheduler->EnableProfiling(enabled);
		m_tickProfiling = enabled;
		m_tickNanos = {};
		m_tickMark = std::chrono::steady_clock::now();
	}
	void TickMark(TickPart part)
	{
		if (!m_tickProfiling)
			return;
		const auto now = std::chrono::steady_clock::now();
		m_tickNanos[static_cast<std::size_t>(part)] += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_tickMark).count());
		m_tickMark = now;
	}
	std::vector<ecs::Scheduler::SystemTiming> Profile() const { return m_scheduler->Profile(); }
	std::uint64_t WaveNanos() const noexcept { return m_scheduler->WaveNanos(); }
	std::size_t WaveCount() const noexcept { return m_scheduler->WaveCount(); }
	bool IsBatchSystem(std::string_view name) const { return m_scheduler->IsBatch(name); }
	std::vector<std::vector<std::string_view>> WavePlan() const { return m_scheduler->WavePlan(); }
	ecs::Scheduler::WaveOverhead SchedulerOverhead() const noexcept { return m_scheduler->Overhead(); }
	const std::vector<std::uint64_t> &WaveWallNanos() const noexcept { return m_scheduler->WaveWallNanos(); }
	const std::vector<std::uint64_t> &WaveLongestJobNanos() const noexcept { return m_scheduler->WaveLongestJobNanos(); }
	const std::vector<std::uint64_t> &WaveJobCounts() const noexcept { return m_scheduler->WaveJobCounts(); }

private:

	const engine::level::Level &m_level;
	std::uint64_t m_seed;
	engine::time::FixedStep m_step{30};
	std::uint64_t m_tick{0};

	// The world owns every resource the systems use; the session keeps
	// handles to the ones it drives between ticks.
	ecs::World m_world;
	gameplay::GroundHeight &m_ground{m_world.EmplaceResource<gameplay::GroundHeight>(m_level.terrain)};
	gameplay::WaypointGraph &m_waypoints{m_world.EmplaceResource<gameplay::WaypointGraph>(domain::BuildWaypoints(m_level))};
	gameplay::NameRegistry &m_names{m_world.EmplaceResource<gameplay::NameRegistry>()};
	gameplay::TeamRoster &m_roster{m_world.EmplaceResource<gameplay::TeamRoster>()};
	// The pathfinding grid (derived from the level and the obstacles on it; rebuilt on restore) and routing scratch.
	gameplay::NavigationGrid &m_navigation{m_world.EmplaceResource<gameplay::NavigationGrid>()};
	gameplay::RouteScratchPool &m_routeScratch{m_world.EmplaceResource<gameplay::RouteScratchPool>()};
	gameplay::Relationships &m_relationships{m_world.EmplaceResource<gameplay::Relationships>()};
	gameplay::CargoManifest &m_manifest{m_world.EmplaceResource<gameplay::CargoManifest>()};
	domain::ObjectTemplates &m_templates;
	domain::TeamTemplates m_teams;
	Engine::Math::RandomStream m_random;
	std::vector<std::string> m_seatNames;
	std::uint32_t m_localSeat{0};
	std::function<bool()> m_cameraFreezesTime;
	bool m_timeFrozen{false};
	bool m_timeFreezes{true};

	// Per-tick channels between systems (and out to the presentation).
	gameplay::SpatialIndex &m_spatial{m_world.EmplaceResource<gameplay::SpatialIndex>()};
	gameplay::VisibleObjects &m_visible{m_world.EmplaceResource<gameplay::VisibleObjects>()};
	gameplay::FiredShots &m_fired{m_world.EmplaceResource<gameplay::FiredShots>()};
	gameplay::ShotQueue &m_shots{m_world.EmplaceResource<gameplay::ShotQueue>()};
	gameplay::KillRequests &m_kills{m_world.EmplaceResource<gameplay::KillRequests>()};
	gameplay::MatchOutcome &m_outcome{m_world.EmplaceResource<gameplay::MatchOutcome>()};
	gameplay::Evacuations &m_evacuations{m_world.EmplaceResource<gameplay::Evacuations>()};
	domain::AiPlayers &m_aiPlayers{m_world.EmplaceResource<domain::AiPlayers>()};
	gameplay::ObjectIds &m_objectIds{m_world.EmplaceResource<gameplay::ObjectIds>()};
	// How the computer players reach this session's scripts.
	domain::AiScriptHooks m_aiHooks;
	domain::ScriptRecords &m_scriptRecords{m_world.EmplaceResource<domain::ScriptRecords>()};
	gameplay::Casualties &m_casualties{m_world.EmplaceResource<gameplay::Casualties>()};
	gameplay::DeathEvents &m_deathEvents{m_world.EmplaceResource<gameplay::DeathEvents>()};
	gameplay::DyingEvents &m_dyingEvents{m_world.EmplaceResource<gameplay::DyingEvents>()};
	gameplay::StructureToppleEvents &m_structureToppleEvents{m_world.EmplaceResource<gameplay::StructureToppleEvents>()};
	gameplay::PlayerMoney &m_money{m_world.EmplaceResource<gameplay::PlayerMoney>()};
	gameplay::PlayerUpgrades &m_upgrades{m_world.EmplaceResource<gameplay::PlayerUpgrades>()};
	gameplay::PlayerSciences &m_sciences{m_world.EmplaceResource<gameplay::PlayerSciences>()};
	gameplay::PlayerRanks &m_ranks{m_world.EmplaceResource<gameplay::PlayerRanks>()};
	gameplay::RankRules &m_rankRules{m_world.EmplaceResource<gameplay::RankRules>()};
	gameplay::SpecialPowerRules &m_powerRules{m_world.EmplaceResource<gameplay::SpecialPowerRules>()};
	gameplay::SharedPowerTimers &m_sharedPowerTimers{m_world.EmplaceResource<gameplay::SharedPowerTimers>()};
	gameplay::SkillPointAwards &m_skillPointAwards{m_world.EmplaceResource<gameplay::SkillPointAwards>()};
	gameplay::PlayerBounties &m_bounties{m_world.EmplaceResource<gameplay::PlayerBounties>()};
	domain::CashNotices &m_cashNotices{m_world.EmplaceResource<domain::CashNotices>()};
	domain::RallyNotices &m_rallyNotices{m_world.EmplaceResource<domain::RallyNotices>()};
	domain::EvaNotices &m_evaNotices{m_world.EmplaceResource<domain::EvaNotices>()};
	domain::Deselections &m_deselections{m_world.EmplaceResource<domain::Deselections>()};
	domain::InfiltrationNotices &m_infiltrationNotices{m_world.EmplaceResource<domain::InfiltrationNotices>()};
	domain::AbilityNotices &m_abilityNotices{m_world.EmplaceResource<domain::AbilityNotices>()};
	domain::AbilityEvents &m_abilityEvents{m_world.EmplaceResource<domain::AbilityEvents>()};
	domain::HuntScans &m_huntScans{m_world.EmplaceResource<domain::HuntScans>()};
	domain::TunnelGuardEvents &m_tunnelGuardEvents{m_world.EmplaceResource<domain::TunnelGuardEvents>()};
	domain::ScriptedEvacuationEvents &m_scriptedEvacuationEvents{m_world.EmplaceResource<domain::ScriptedEvacuationEvents>()};
	domain::GuardEvents &m_guardEvents{m_world.EmplaceResource<domain::GuardEvents>()};
	domain::SpyVisionEvents &m_spyVisionEvents{m_world.EmplaceResource<domain::SpyVisionEvents>()};
	domain::RepulsionEvents &m_repulsionEvents{m_world.EmplaceResource<domain::RepulsionEvents>()};
	domain::AttackSquads &m_attackSquadList{m_world.EmplaceResource<domain::AttackSquads>()};
	domain::SoloPlay &m_soloPlay{m_world.EmplaceResource<domain::SoloPlay>()};
	domain::ScoreKeepers &m_scoreKeepers{m_world.EmplaceResource<domain::ScoreKeepers>()};
	domain::AcademyStats &m_academy{m_world.EmplaceResource<domain::AcademyStats>()};
	domain::CommandBarOverrides &m_commandBarOverrides{m_world.EmplaceResource<domain::CommandBarOverrides>()};
	domain::BuildableOverrides &m_buildableOverrides{m_world.EmplaceResource<domain::BuildableOverrides>()};
	gameplay::HulkLifetime &m_hulkLifetime{m_world.EmplaceResource<gameplay::HulkLifetime>()};
	domain::SquadsDone &m_squadsDone{m_world.EmplaceResource<domain::SquadsDone>()};
	domain::ProductionNotices &m_productionNotices{m_world.EmplaceResource<domain::ProductionNotices>()};
	gameplay::UpgradeReactions &m_upgradeReactions{m_world.EmplaceResource<gameplay::UpgradeReactions>()};
	domain::UpgradeCreations &m_upgradeCreations{m_world.EmplaceResource<domain::UpgradeCreations>()};
	gameplay::ObjectUpgradeGrants &m_objectUpgradeGrants{m_world.EmplaceResource<gameplay::ObjectUpgradeGrants>()};

	// What the domain algorithms and the scripts act on.
	domain::GameWorld m_game;
	ScriptBridge m_bridge;

	// Systems are stateless: registered once, all their data is in the world.
	// Each map player's side (its PlayerTemplate's Side), for the control bar's scheme.
	std::vector<std::string> m_playerSides;
	std::vector<std::string> m_playerTemplates;
	ecs::SystemRegistry m_registry;
	std::size_t m_workers{1};
	std::optional<ecs::Scheduler> m_scheduler;
	// Tick profiling (EnableProfiling): the time of each part of the tick outside the systems.
	bool m_tickProfiling{false};
	std::array<std::uint64_t, static_cast<std::size_t>(TickPart::Count)> m_tickNanos{};
	std::chrono::steady_clock::time_point m_tickMark;

	engine::scripting::Vocabulary m_vocabulary;
	std::vector<scripting::CameraScriptCommand> m_cameraCommands;
	std::vector<scripting::ClientScriptCommand> m_clientCommands;
	std::optional<engine::scripting::ScriptRuntime> m_scripts;
};
}
