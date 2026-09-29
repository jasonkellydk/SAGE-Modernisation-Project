export module games.generalszh.session.session;
import engine.gameplay.rts.combat.systems.attack_move_system;
import engine.gameplay.common.status.components.script_status;
import engine.gameplay.rts.movement.systems.move_path_system;
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
import games.generalszh.gameplay.production.algorithms.rally_points;
import std;
import engine.gameplay.common.identity.components.captured;
import games.generalszh.gameplay.construction.algorithms.build_legality;
import games.generalszh.gameplay.construction.algorithms.building;
import games.generalszh.gameplay.orders.algorithms.command_availability;
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
import engine.gameplay.common.physics.systems.physics_system;
import engine.gameplay.common.physics.systems.shock_wave_system;
import engine.gameplay.rts.containment.systems.garrison_clear_system;
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
import games.generalszh.gameplay.crates.algorithms.car_bombs;
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
import games.generalszh.gameplay.hacking.algorithms.hack_events;
import games.generalszh.gameplay.battleplans.algorithms.battle_plan_events;
import games.generalszh.gameplay.crates.algorithms.hijacking;
import games.generalszh.gameplay.economy.algorithms.warehouse_crippling;
import games.generalszh.gameplay.combat.algorithms.battle_bus;
import games.generalszh.gameplay.combat.algorithms.weapon_bonus_pulses;
import engine.gameplay.common.weapons.systems.temp_weapon_bonus_system;
import games.generalszh.gameplay.creation.algorithms.creation_list_runner;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.orders.algorithms.command_application;
import games.generalszh.session.script_bridge;
import games.generalszh.scripting.core_vocabulary;

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
			m_world.EmplaceResource<gameplay::LoadoutCatalog>(), m_world.EmplaceResource<gameplay::ParachuteCatalog>())),
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
		m_world.EmplaceResource<domain::TunnelGuardRules>(domain::TunnelGuardRules{frames(content.aiData.guardEnemyScanRateMs), frames(content.aiData.guardChaseUnitsMs)});
		m_world.EmplaceResource<domain::GuardRules>(domain::GuardRules{frames(content.aiData.guardEnemyScanRateMs), frames(content.aiData.guardEnemyReturnScanRateMs),
			frames(content.aiData.guardChaseUnitsMs)});
		// AIData EnableRepulsors / RepulsedDistance; the idle look every IDLE_COUNTDOWN_DELAY (two seconds of frames).
		m_world.EmplaceResource<domain::RepulsionRules>(domain::RepulsionRules{content.aiData.enableRepulsors, content.aiData.repulsedDistance, 2 * m_step.TicksPerSecond()});
		domain::BuildNavigationGrid(m_game, m_navigation);
		// Weapons of the damage type no armor resists may do no damage and still be chosen (chooseBestWeaponForTarget).
		m_templates.weapons.unresistable = *content::DamageTypeIndex("UNRESISTABLE");
		m_templates.weapons.normalDeath = content::DeathTypeIndex("NORMAL").value_or(0);
		m_templates.weapons.continuousFireMean = content::weapon_bonus::ContinuousFireMean;
		m_templates.weapons.continuousFireFast = content::weapon_bonus::ContinuousFireFast;
		m_templates.weapons.targetFaerieFire = content::weapon_bonus::TargetFaerieFire;
		m_templates.weapons.faerieFireStatus = content::ObjectStatusBit("FAERIE_FIRE");
		m_templates.deaths.crushDamageType = *content::DamageTypeIndex("CRUSH");
		// ActiveBody::attemptDamage's handled damage types: KILL_PILOT takes no health (ApplyPilotKills carries it out).
		m_templates.armors.SetHandled((std::uint64_t{1} << *content::DamageTypeIndex("KILL_PILOT")) | (std::uint64_t{1} << *content::DamageTypeIndex("KILL_GARRISONED")) |
			(std::uint64_t{1} << *content::DamageTypeIndex("STATUS")));
		m_templates.weapons.killGarrisoned = *content::DamageTypeIndex("KILL_GARRISONED");
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
					if (m_playerTemplates.size() <= player)
						m_playerTemplates.resize(player + 1);
					m_playerTemplates[player] = info.name;
					// Player::initFromDict: its side's IntrinsicSciences.
					for (const std::string &science : info.intrinsicSciences)
						if (const auto bit = content.Science(science))
							m_ranks.Of(player).intrinsicSciences.push_back(*bit);
					m_ranks.Of(player).intrinsicPurchasePoints = info.intrinsicSciencePurchasePoints;
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
		// Every player starts with the map's money for it, else the original's default 10000.
		m_money.Resize(m_roster.PlayerCount());
		m_upgrades.Resize(m_roster.PlayerCount());
		m_sciences.Resize(m_roster.PlayerCount());
		for (std::uint32_t player = 0; player < m_roster.PlayerCount(); ++player)
		{
			std::int64_t cash = m_templates.Content().gameData.defaultStartingCash;
			for (const auto &participant : level.scenario.participants)
				if (participant.properties.Get<std::string>("playerName") == m_roster.PlayerAt(player).name)
					cash = participant.properties.Get<std::int64_t>("playerStartMoney").value_or(cash);
			m_money.Deposit(player, cash);
		}
		if (place)
		{
			domain::PlaceBridges(m_game);
			domain::PlaceBridgeLikeObjects(m_game);
			// The bridges' decks in the pathfinding: every clearance plane afresh.
			domain::RebuildClearance(m_game);
			domain::PlaceObjects(m_game);
			for (const StartingPlayer &start : options.starts)
				if (const auto *faction = m_templates.Content().playerTemplates.At(start.playerTemplate))
					if (const auto team = m_roster.FindTeam(start.team))
						domain::PlaceStartingObjects(m_game, *faction, start.startPosition, *team);
			// Garrisons made with their occupants inside (InitialRoster) have them from the start.
			domain::TendGarrisons(m_game);
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
						faction->name != "FactionObserver")
					{
						m_outcome.players.push_back({player});
						break;
					}
				}
		}
		scripting::AddCoreVocabulary(m_vocabulary);
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
	void SaveResources(engine::core::serialization::ByteWriter &writer) const;
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
		m_cameraCommands.clear();
		m_clientCommands.clear();
		m_casualties.list.clear();
		m_names.ClearReleased();
		m_cashNotices.list.clear();
		m_rallyNotices.list.clear();
		m_world.Resource<domain::MinefieldEffects>().played.clear();
		m_world.Resource<domain::StickyBombCues>().list.clear();
		m_world.Resource<domain::BridgeCues>().list.clear();
		m_world.Resource<domain::HackCues>().list.clear();
		m_world.Resource<domain::BattlePlanCues>().list.clear();
		m_world.Resource<domain::BattleBusCues>().list.clear();
		m_world.Resource<domain::LauncherDoorEffects>().played.clear();
		{
			auto &cannonEvents = m_world.Resource<domain::ParticleCannonEvents>();
			cannonEvents.scorches.clear();
			cannonEvents.played.clear();
			cannonEvents.remnants.clear();
			cannonEvents.changes.clear();
		}
		m_world.Resource<gameplay::SubdualChanges>().list.clear();
		m_world.Resource<gameplay::ParticleClears>().entities.clear();
		m_evaNotices.list.clear();
		m_world.Resource<domain::Deselections>().list.clear();
		m_world.Resource<domain::InfiltrationNotices>().list.clear();
		m_abilityNotices.abilities.clear();
		m_abilityNotices.powers.clear();
		m_abilityNotices.defected.clear();
		m_abilityNotices.hijacks.clear();
		m_productionNotices.created.clear();
		m_world.Resource<gameplay::CargoManifest>().ClearChanges();
		++m_tick;
		for (const engine::net::CommandEnvelope &envelope : commands)
			if (const auto command = commands::Decode(envelope))
			{
				if (const auto *signal = std::get_if<commands::SignalUi>(&*command))
					m_scripts->Signal(signal->hook); // any seat's, as the original's signalUIInteract
				else if (const auto player = SeatPlayer(envelope.player))
					domain::ApplyCommand(m_game, *player, *command);
			}
		m_scripts->Tick(m_tick);
		// ThePlayerList->updateTeamStates: the teams' own scripts, once the side scripts have run.
		const domain::TeamScriptHooks teamHooks = TeamHooks();
		domain::UpdateTeamStates(m_game, teamHooks);
		// GameLogic::update: time frozen (by a script, or a camera move while it lasts) the frame ends with its scripts;
		// its number does not move on.
		m_timeFrozen = m_timeFreezes && (m_scriptRecords.timeFrozen || (m_cameraFreezesTime && m_cameraFreezesTime()));
		if (m_timeFrozen)
		{
			--m_tick;
			return;
		}
		// Transports made since the last tick take on their initial payloads (TransportContain::update's first pass).
		domain::CreateInitialPayloads(m_game);
		Execute();
		// The tick's pilot kills (ActiveBody's DAMAGE_KILLPILOT) and unmanned vehicles taken over.
		domain::ApplyPilotKills(m_game);
		domain::ApplyTakeOvers(m_game);
		// Bikes take on and let go of their riders (RiderChangeContain).
		domain::ApplyRiderChanges(m_game);
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
		// Spectre gunships: their orbit, gattling and howitzer orders.
		domain::ApplyGunshipEvents(m_game);
		// Superweapon missiles fired this tick: their objects out, flying from the next.
		domain::ApplyObjectFlownLaunches(m_game);
		// FireOCLAfterWeaponCooldownUpdate::fireOCL: ObjectCreationList::create(ocl, obj, obj, frames).
		ApplyCooldownCreations();
		// Hurt runners scare the others; runners run from repulsors, or wander once their run is over.
		domain::ApplyRepulsorMarks(m_game);
		domain::ApplyRepulsions(m_game);
		// Units whose squad has nobody left idle.
		domain::ApplySquadsDone(m_game);
		CarryOutDeaths();
		// Object::onDie's booby traps and the special objects that do not outlive their owners; then the sticky bombs
		// whose targets died (StickyBombSystem).
		domain::ApplySpecialObjectCasualties(m_game, m_casualties.list);
		domain::ApplyStickyBombEvents(m_game);
		// BridgeBehavior: the bridges' damage-state transitions and due death effects, then those that died.
		domain::ApplyBridgeEvents(m_game, m_casualties.list);
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
		// SupplyWarehouseDockUpdate::setDockCrippled: whoever a crippled warehouse had let in.
		domain::ApplyWarehouseCrippling(m_game);
		// HijackerUpdate: riding with the vehicle, or let out once it is gone.
		{
			std::vector<domain::HijackerEvent> hijackers;
			m_world.Resource<domain::HijackerEvents>().AppendTo(hijackers);
			m_world.Resource<domain::HijackerEvents>().Reset(0);
			domain::ApplyHijackerEvents(m_game, hijackers);
		}
		CarryOutMinefieldDeaths();
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
		CarryOutShots();
		CarryOutTopples();
		CarryOutFireSpread();
		CarryOutProduction();
		// What the abilities (SpecialAbilitySystem) did beyond their units; then the command button hunts.
		domain::ApplyAbilityEvents(m_game);
		domain::ApplyHuntScans(m_game);
		// ThePlayerList->update after the tick's objects: each player's AI, then its teams' generic scripts.
		for (std::uint32_t player = 0; player < m_game.roster.PlayerCount(); ++player)
		{
			if (domain::AiPlayer *ai = m_aiPlayers.Of(player))
				domain::UpdateAiPlayer(m_game, m_aiPlayers, *ai, m_aiHooks);
			domain::UpdateGenericScripts(m_game, player, teamHooks);
		}
		CarryOutUpgradeCreations();
		domain::TendMinefields(m_game);
		CarryOutCratePickups();
		CarryOutSpawns();
		domain::FinishSales(m_game);
		domain::TendGarrisons(m_game);
		domain::TendTunnels(m_game);
		domain::TendRebuildHoles(m_game);
		// The tick's boardings set off their containers' booby traps (OpenContain::addToContain).
		domain::ApplyBoobyTrapEntries(m_game);
		// Internet Centers set the hackers they take in hacking (InternetHackContain::onContaining).
		domain::ApplyInternetHackCargo(m_game);
	}

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
		return domain::SpawnObject(m_game, type, position, facing, team, "");
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
	bool CanBuildAt(ecs::Entity builder, std::string_view structure, Engine::Math::FixedVector2 at, Engine::Math::TurnAngle facing) override
	{
		const content::ObjectDefinition *what = m_templates.Content().objects.Find(structure);
		return what != nullptr && domain::CanMakeUnit(m_game, builder, *what) == domain::CanMake::Ok &&
			domain::CheckBuildLocation(m_game, *what, at, facing, builder) == domain::LegalBuild::Ok;
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
		m_world.RegisterComponent<gameplay::Transform>();
		m_world.RegisterComponent<gameplay::DefinitionRef>();
		m_world.RegisterComponent<gameplay::TeamMember>();
		m_world.RegisterComponent<gameplay::Owner>();
		m_world.RegisterComponent<gameplay::ObjectId>();
		m_world.RegisterComponent<gameplay::SpecialPowerTimers>();
		m_world.RegisterComponent<gameplay::HeightDie>();
		m_world.RegisterComponent<domain::InitialPayload>();
		m_world.RegisterComponent<domain::RiderChange>();
		m_world.RegisterComponent<gameplay::BodyExtent>();
		m_world.RegisterComponent<gameplay::RadarProvider>();
		m_world.RegisterComponent<gameplay::Vision>();
		m_world.RegisterComponent<gameplay::InactiveBody>();
		m_world.RegisterComponent<gameplay::PartitionFootprint>();
		m_world.RegisterComponent<gameplay::ObjectShroud>();
		m_world.RegisterComponent<domain::RebuildHole>();
		m_world.RegisterComponent<domain::SpecialAbilities>();
		m_world.RegisterComponent<domain::CommandButtonHunt>();
		m_world.RegisterComponent<domain::TunnelGuard>();
		m_world.RegisterComponent<domain::ScriptedEvacuation>();
		m_world.RegisterComponent<domain::Guard>();
		m_world.RegisterComponent<domain::CleanupHazard>();
		m_world.RegisterComponent<domain::SpyVision>();
		m_world.RegisterComponent<gameplay::VisionSpies>();
		m_world.RegisterComponent<gameplay::Countermeasures>();
		m_world.EmplaceResource<gameplay::MissileReports>();
		m_world.EmplaceResource<gameplay::FlareLaunches>();
		m_world.RegisterComponent<domain::Repulsable>();
		m_world.RegisterComponent<gameplay::IndicatorColor>();
		m_world.RegisterComponent<domain::EmptiedWatch>();
		m_world.RegisterComponent<domain::RepulsorMark>();
		m_world.RegisterComponent<domain::AttackSquad>();
		m_world.RegisterComponent<domain::DifficultyBonus>();
		m_world.RegisterComponent<gameplay::PathCompleted>();
		m_world.RegisterComponent<gameplay::ScriptStatus>();
		m_world.RegisterComponent<gameplay::AiActivity>();
		m_world.RegisterComponent<domain::RebuildWorker>();
		m_world.RegisterComponent<gameplay::DeathCredit>();
		m_world.RegisterComponent<gameplay::Producer>();
		m_world.RegisterComponent<domain::CostModifying>();
		m_world.RegisterComponent<gameplay::Locomotion>();
		m_world.RegisterComponent<gameplay::MoveOrder>();
		m_world.RegisterComponent<gameplay::NavigationAgent>();
		m_world.RegisterComponent<gameplay::NavigationObstacle>();
		m_world.RegisterComponent<gameplay::Route>();
		m_world.RegisterComponent<gameplay::IgnoredObstacle>();
		m_world.RegisterComponent<gameplay::Targetable>();
		m_world.RegisterComponent<gameplay::Health>();
		m_world.RegisterComponent<gameplay::HealthFloor>();
		m_world.RegisterComponent<gameplay::DamageScalar>();
		m_world.RegisterComponent<gameplay::Subdual>();
		m_world.RegisterComponent<gameplay::StatusDamage>();
		m_world.RegisterComponent<gameplay::Poison>();
		m_world.RegisterComponent<gameplay::ConstructionProgress>();
		m_world.RegisterComponent<gameplay::Sale>();
		m_world.RegisterComponent<gameplay::Captured>();
		m_world.RegisterComponent<domain::SpecialObject>();
		m_world.RegisterComponent<domain::Bridge>();
		m_world.RegisterComponent<domain::InternetHack>();
		m_world.RegisterComponent<domain::BattlePlan>();
		m_world.RegisterComponent<domain::Hijacker>();
		m_world.RegisterComponent<domain::WarehouseCrippling>();
		m_world.RegisterComponent<domain::BattleBus>();
		m_world.RegisterComponent<domain::WeaponBonusPulse>();
		m_world.RegisterComponent<gameplay::TempWeaponBonus>();
		m_world.RegisterComponent<gameplay::SecondLife>();
		m_world.RegisterComponent<gameplay::SurfaceLayer>();
		m_world.RegisterComponent<domain::BridgeTower>();
		m_world.RegisterComponent<domain::StickyBomb>();
		m_world.RegisterComponent<gameplay::UnderConstruction>();
		m_world.RegisterComponent<gameplay::Builder>();
		m_world.RegisterComponent<domain::CommandSetOverride>();
		m_world.RegisterComponent<gameplay::Garrison>();
		m_world.RegisterComponent<gameplay::Tunnel>();
		m_world.RegisterComponent<gameplay::HealPad>();
		m_world.RegisterComponent<gameplay::GarrisonPoints>();
		m_world.RegisterComponent<gameplay::TransportFirePoints>();
		m_world.RegisterComponent<domain::SteeringLook>();
		m_world.RegisterComponent<domain::RadarDish>();
		m_world.RegisterComponent<domain::ControlRods>();
		m_world.RegisterComponent<gameplay::Experience>();
		m_world.RegisterComponent<gameplay::Horde>();
		m_world.RegisterComponent<gameplay::Armament>();
		m_world.RegisterComponent<gameplay::FiringTracker>();
		m_world.RegisterComponent<gameplay::WeaponBonusConditions>();
		m_world.RegisterComponent<gameplay::AttackTarget>();
		m_world.RegisterComponent<gameplay::Turret>();
		m_world.RegisterComponent<gameplay::AltTurret>();
		m_world.RegisterComponent<gameplay::WeaponSlots>();
		m_world.RegisterComponent<gameplay::PointDefense>();
		m_world.RegisterComponent<gameplay::ProjectileFlight>();
		m_world.RegisterComponent<gameplay::MissileFlight>();
		m_world.RegisterComponent<gameplay::Aggression>();
		m_world.RegisterComponent<gameplay::AttackMove>();
		m_world.RegisterComponent<gameplay::OffMap>();
		m_world.RegisterComponent<gameplay::Transport>();
		m_world.RegisterComponent<gameplay::Upgradable>();
		m_world.RegisterComponent<gameplay::Slaved>();
		m_world.RegisterComponent<gameplay::Spawner>();
		m_world.RegisterComponent<gameplay::Disabled>();
		m_world.RegisterComponent<gameplay::StatusFlags>();
		m_world.RegisterComponent<gameplay::UndetectedDefector>();
		m_world.RegisterComponent<gameplay::Parachute>();
		m_world.RegisterComponent<gameplay::ParachuteRider>();
		m_world.RegisterComponent<gameplay::PendingDamage>();
		m_world.RegisterComponent<gameplay::Carried>();
		m_world.RegisterComponent<gameplay::VictoryRole>();
		m_world.RegisterComponent<gameplay::BoundingVolume>();
		m_world.RegisterComponent<gameplay::Wanderer>();
		m_world.RegisterComponent<gameplay::WanderAnchor>();
		m_world.RegisterComponent<gameplay::FaceTarget>();
		m_world.RegisterComponent<gameplay::ExitIntent>();
		m_world.RegisterComponent<gameplay::MovePath>();
		m_world.RegisterComponent<gameplay::RallyPoint>();
		m_world.RegisterComponent<gameplay::BodyCollision>();
		m_world.RegisterComponent<domain::PilotSeeker>();
		m_world.RegisterComponent<domain::HealSeeker>();
		m_world.RegisterComponent<domain::TechBuilding>();
		m_world.RegisterComponent<gameplay::AssistedTargeting>();
		m_world.RegisterComponent<gameplay::Assisting>();
		m_world.RegisterComponent<gameplay::CollideWeapon>();
		m_world.RegisterComponent<gameplay::Passenger>();
		m_world.RegisterComponent<gameplay::Boarding>();
		m_world.RegisterComponent<gameplay::CargoSize>();
		m_world.RegisterComponent<gameplay::Delivery>();
		m_world.RegisterComponent<gameplay::Descent>();
		m_world.RegisterComponent<gameplay::Appearance>();
		m_world.RegisterComponent<gameplay::Mortality>();
		m_world.RegisterComponent<gameplay::Dying>();
		m_world.RegisterComponent<gameplay::Crash>();
		m_world.RegisterComponent<gameplay::Collapse>();
		m_world.RegisterComponent<gameplay::DrawOffset>();
		m_world.RegisterComponent<gameplay::PhysicsBody>();
		m_world.RegisterComponent<gameplay::Attitude>();
		m_world.RegisterComponent<gameplay::Lifetime>();
		m_world.RegisterComponent<gameplay::ModelOverride>();
		m_world.RegisterComponent<gameplay::DebrisLook>();
		m_world.RegisterComponent<gameplay::BounceSound>();
		m_world.RegisterComponent<gameplay::HealLock>();
		m_world.RegisterComponent<gameplay::SelfHealing>();
		m_world.RegisterComponent<gameplay::AreaHealing>();
		m_world.RegisterComponent<gameplay::Flammable>();
		m_world.RegisterComponent<gameplay::FireSpread>();
		m_world.RegisterComponent<gameplay::AutoFire>();
		m_world.RegisterComponent<gameplay::DamageReaction>();
		m_world.RegisterComponent<gameplay::BlastWave>();
		m_world.RegisterComponent<gameplay::Scorched>();
		m_world.RegisterComponent<gameplay::Overcharge>();
		m_world.RegisterComponent<domain::HeldAboard>();
		m_world.RegisterComponent<gameplay::Mount>();
		m_world.RegisterComponent<gameplay::Mounted>();
		m_world.RegisterComponent<gameplay::PropagandaTower>();
		m_world.RegisterComponent<gameplay::PropagandaInfluence>();
		m_world.RegisterComponent<gameplay::Minefield>();
		m_world.RegisterComponent<gameplay::MineSafe>();
		m_world.RegisterComponent<gameplay::DemoTrap>();
		m_world.RegisterComponent<gameplay::EmpPulse>();
		m_world.RegisterComponent<gameplay::AutoDeposit>();
		m_world.RegisterComponent<gameplay::PartOverrides>();
		m_world.RegisterComponent<gameplay::AreaPresence>();
		m_world.RegisterComponent<gameplay::EmpTraits>();
		m_world.RegisterComponent<gameplay::DisabledUntil>();
		m_world.RegisterComponent<domain::MinefieldGenerator>();
		m_world.RegisterComponent<gameplay::Stealth>();
		m_world.RegisterComponent<gameplay::StealthDetector>();
		m_world.RegisterComponent<gameplay::GrantStealth>();
		m_world.RegisterComponent<gameplay::Collider>();
		m_world.RegisterComponent<gameplay::Topple>();
		m_world.RegisterComponent<gameplay::Squishable>();
		m_world.RegisterComponent<gameplay::ProductionQueue>();
		m_world.RegisterComponent<gameplay::ProductionDoors>();
		m_world.RegisterComponent<gameplay::EnergySource>();
		m_world.RegisterComponent<gameplay::Powered>();
		m_world.RegisterComponent<gameplay::Airfield>();
		m_world.RegisterComponent<gameplay::Jet>();
		m_world.RegisterComponent<gameplay::Dock>();
		m_world.RegisterComponent<gameplay::Docking>();
		m_world.RegisterComponent<gameplay::RepairDock>();
		m_world.RegisterComponent<gameplay::ProductionExitGate>();
		m_world.RegisterComponent<gameplay::HiveBody>();
		m_world.RegisterComponent<domain::CooldownCreations>();
		m_world.RegisterComponent<gameplay::Deploy>();
		m_world.RegisterComponent<domain::LauncherDoor>();
		m_world.RegisterComponent<domain::ParticleCannon>();
		m_world.RegisterComponent<domain::MobMember>();
		m_world.RegisterComponent<domain::SpectreGunship>();
		m_world.EmplaceResource<domain::GunshipEvents>();
		m_world.EmplaceResource<domain::RetaliationModes>();
		m_world.EmplaceResource<domain::BattlePlanPlayers>();
		m_world.EmplaceResource<domain::MobEvents>();
		m_world.EmplaceResource<domain::ParticleCannonEvents>();
		m_world.EmplaceResource<gameplay::TemporaryWeaponFires>();
		m_world.RegisterComponent<gameplay::NeutronFlight>();
		m_world.EmplaceResource<gameplay::NeutronEffects>();
		m_world.EmplaceResource<domain::LauncherDoorEffects>();
		m_world.EmplaceResource<domain::CooldownCreationEvents>();
		m_world.EmplaceResource<gameplay::DockRepairs>();
		m_world.RegisterComponent<gameplay::Harvester>();
		m_world.RegisterComponent<gameplay::ResourceStore>();
		m_world.RegisterComponent<gameplay::ResourceDepot>();
		m_world.RegisterComponent<gameplay::Loadout>();
		m_world.RegisterComponent<domain::Crate>();
		// Presentation state rides on the same entities, in side tables.
		if (presentationComponents)
			presentationComponents(m_world);
		m_world.FinalizeComponents();

		const auto &terrain = m_level.terrain;
		const Fixed playableWidth = terrain.cellSize * Fixed::FromInt(static_cast<std::int64_t>(terrain.width) - 1 - 2 * terrain.border);
		const Fixed playableHeight = terrain.cellSize * Fixed::FromInt(static_cast<std::int64_t>(terrain.height) - 1 - 2 * terrain.border);
		m_spatial = gameplay::SpatialIndex({playableWidth, playableHeight}, Fixed::FromInt(100));

		// The rest of the systems' data: settings, seeds and the tick's channels.
		m_world.EmplaceResource<gameplay::RandomSeed>(gameplay::RandomSeed{m_seed});
		const content::GameData &globals = m_templates.Content().gameData;
		m_world.EmplaceResource<gameplay::PhysicsSettings>(content::ZeroHourPhysicsSettings(globals.gravity));
		m_world.EmplaceResource<gameplay::CollisionSettings>(gameplay::CollisionSettings{globals.structureStiffness, globals.structureRubbleHeight});
		// Hurt to GameData's MovementPenaltyDamageState, a body moves on its damaged rates (ActiveBody::calcDamageState: at
		// or below the state's threshold; RUBBLE: out of health; PRISTINE: always).
		{
			const std::array<Fixed, 4> ratios{Fixed{}, globals.unitDamaged, globals.unitReallyDamaged, Fixed{}};
			m_world.EmplaceResource<gameplay::MovementPenalty>(
				gameplay::MovementPenalty{ratios[std::min<std::uint32_t>(globals.movementPenaltyState, 3u)], globals.movementPenaltyState == 0});
		}
		m_world.EmplaceResource<gameplay::FallDamage>();
		m_world.EmplaceResource<gameplay::ShockWaves>();
		m_world.EmplaceResource<gameplay::GarrisonHits>();
		m_world.EmplaceResource<gameplay::GarrisonClears>();
		m_world.EmplaceResource<gameplay::Landings>();
		m_world.EmplaceResource<gameplay::ParachuteOpenings>();
		m_world.EmplaceResource<gameplay::ExperienceAwards>();
		m_world.EmplaceResource<gameplay::Assists>();
		m_world.EmplaceResource<gameplay::JetDamage>();
		m_world.EmplaceResource<gameplay::PlayerEnergy>();
		m_world.EmplaceResource<gameplay::EnergySettings>(
			gameplay::EnergySettings{globals.lowEnergyPenaltyModifier, globals.minLowEnergyProductionSpeed, globals.maxLowEnergyProductionSpeed});
		m_world.EmplaceResource<gameplay::EnergyShares>();
		m_world.EmplaceResource<gameplay::DropSettings>();
		m_world.EmplaceResource<domain::AppearanceSettings>(domain::AppearanceSettings{globals.unitDamaged, globals.unitReallyDamaged});
		m_world.EmplaceResource<gameplay::SpatialGather>();
		m_world.EmplaceResource<gameplay::IncomingDamage>();
		m_world.EmplaceResource<gameplay::Detonations>();
		m_world.EmplaceResource<gameplay::MissileDetonations>();
		m_world.EmplaceResource<gameplay::PointDefenseShots>();
		m_world.EmplaceResource<gameplay::Deaths>();
		m_world.EmplaceResource<gameplay::Hits>();
		m_world.EmplaceResource<gameplay::SecondLives>();
		m_world.EmplaceResource<gameplay::SubdualChanges>();
		m_world.EmplaceResource<gameplay::ParticleClears>();
		m_world.EmplaceResource<gameplay::Expirations>();
		m_world.EmplaceResource<gameplay::DeliveriesDone>();
		m_world.EmplaceResource<gameplay::BoardRequests>();
		m_world.EmplaceResource<gameplay::ExitRequests>();
		m_world.EmplaceResource<gameplay::DropExits>();
		m_world.EmplaceResource<gameplay::RiderExits>();
		m_world.EmplaceResource<gameplay::IntentExits>();
		m_world.EmplaceResource<gameplay::SpawnRequests>();
		m_world.EmplaceResource<gameplay::SaleSettings>();
		m_world.EmplaceResource<gameplay::SalesDone>();
		m_world.EmplaceResource<gameplay::ConstructionsDone>();
		m_world.EmplaceResource<gameplay::Promotions>();
		m_world.EmplaceResource<gameplay::Removals>();
		m_world.EmplaceResource<gameplay::HealOffers>();
		m_world.EmplaceResource<gameplay::HealPulses>();
		m_world.EmplaceResource<gameplay::DetectionOffers>();
		m_world.EmplaceResource<gameplay::Detections>();
		m_world.EmplaceResource<gameplay::DetectorPings>();
		m_world.EmplaceResource<gameplay::GrantOffers>();
		m_world.EmplaceResource<gameplay::StealthGrants>();
		m_world.EmplaceResource<gameplay::FireSettings>(content::ZeroHourFireSettings());
		m_world.EmplaceResource<gameplay::BurnDamage>();
		m_world.EmplaceResource<gameplay::IgnitionOffers>();
		m_world.EmplaceResource<gameplay::Ignitions>();
		m_world.EmplaceResource<gameplay::SpreadTries>();
		m_world.EmplaceResource<gameplay::Deletions>();
		m_world.EmplaceResource<gameplay::AutoShots>();
		m_world.EmplaceResource<gameplay::ColliderIndex>(Engine::Math::FixedVector2{playableWidth, playableHeight}, Fixed::FromInt(100));
		m_world.EmplaceResource<gameplay::ColliderGather>();
		m_world.EmplaceResource<gameplay::ContactOffers>();
		m_world.EmplaceResource<gameplay::Contacts>();
		m_world.EmplaceResource<gameplay::BlastWaves>();
		m_world.EmplaceResource<gameplay::OverchargeEvents>();
		m_world.EmplaceResource<gameplay::PropagandaScans>();
		m_world.EmplaceResource<gameplay::PropagandaHeals>();
		m_world.EmplaceResource<domain::MinefieldEffects>();
		m_world.EmplaceResource<domain::StickyBombEvents>();
		m_world.EmplaceResource<domain::StickyBombCues>();
		m_world.EmplaceResource<domain::BridgeEvents>();
		m_world.EmplaceResource<domain::BridgeCues>();
		m_world.EmplaceResource<domain::HackEvents>();
		m_world.EmplaceResource<domain::HackCues>();
		m_world.EmplaceResource<domain::BattlePlanEvents>();
		m_world.EmplaceResource<domain::BattlePlanCues>();
		m_world.EmplaceResource<domain::HijackerEvents>();
		m_world.EmplaceResource<domain::WarehouseCripplingEvents>();
		m_world.EmplaceResource<domain::BattleBusEvents>();
		m_world.EmplaceResource<domain::BattleBusCues>();
		m_world.EmplaceResource<domain::WeaponBonusPulses>();
		m_world.EmplaceResource<gameplay::DeckSurfaces>();
		m_world.EmplaceResource<gameplay::DisableRequests>();
		m_world.EmplaceResource<gameplay::EmpStrikes>();
		m_world.EmplaceResource<gameplay::AutoDeposits>();
		// MinefieldBehavior's drain: DAMAGE_UNRESISTABLE, DEATH_NORMAL, over LOGICFRAMES_PER_SECOND.
		m_world.EmplaceResource<gameplay::MineSettings>(gameplay::MineSettings{content::DamageTypeIndex("UNRESISTABLE").value_or(0),
			content::DeathTypeIndex("NORMAL").value_or(0), m_step.TicksPerSecond()});
		// OverchargeBehavior::update: DAMAGE_PENALTY, DEATH_NORMAL, over LOGICFRAMES_PER_SECOND.
		m_world.EmplaceResource<gameplay::OverchargeSettings>(gameplay::OverchargeSettings{content::DamageTypeIndex("PENALTY").value_or(0),
			content::DeathTypeIndex("NORMAL").value_or(0), m_step.TicksPerSecond()});
		m_world.EmplaceResource<gameplay::ToppleSettings>(gameplay::ToppleSettings{*content::DeathTypeIndex("TOPPLED")});
		m_world.EmplaceResource<gameplay::ToppleEvents>();
		m_world.EmplaceResource<gameplay::CrushSettings>(gameplay::CrushSettings{*content::DamageTypeIndex("CRUSH"), *content::DeathTypeIndex("CRUSHED")});
		m_world.EmplaceResource<gameplay::CrushDamage>();
		m_world.EmplaceResource<gameplay::ProductionDone>();
		m_world.EmplaceResource<gameplay::HarvestRoster>();
		m_world.EmplaceResource<gameplay::HarvestEvents>();
		m_world.EmplaceResource<domain::CrateTouches>();
		m_world.EmplaceResource<domain::CratePickups>();
		m_registry.Register(m_physics);
		m_registry.Register(m_shockWaves);
		m_registry.Register(m_garrisonClears);
		m_registry.Register(m_passedBonuses);
		// Riders' passed-on bonuses before the weapons aim and fire.
		m_registry.OrderBefore<gameplay::PassedBonusSystem, gameplay::TargetingSystem>();
		m_registry.OrderBefore<gameplay::PassedBonusSystem, gameplay::WeaponSystem>();
		// Projectiles that clear garrisons: decided between their flight and the tick's impacts.
		m_registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::GarrisonClearSystem>();
		m_registry.OrderBefore<gameplay::GarrisonClearSystem, gameplay::ImpactSystem>();
		// Direct garrison killing after the impacts, before the damage.
		m_registry.Register(m_garrisonKillDamage);
		m_registry.OrderBefore<gameplay::ImpactSystem, gameplay::GarrisonKillDamageSystem>();
		m_registry.OrderBefore<gameplay::GarrisonKillDamageSystem, gameplay::HealthSystem>();
		// Shock waves push what the tick's impacts reached.
		m_registry.OrderBefore<gameplay::ImpactSystem, gameplay::ShockWaveSystem>();
		m_registry.Register(m_powerPauses);
		m_world.EmplaceResource<gameplay::PlayerRadar>();
		m_registry.Register(m_radarCoverage);
		// The shroud after the tick's deaths and removals (the partition update closes the original's frame).
		m_world.EmplaceResource<gameplay::ShroudMap>();
		m_registry.Register(m_vision);
		m_registry.OrderBefore<gameplay::DeathSystem, gameplay::VisionSystem>();
		m_registry.OrderBefore<gameplay::RemovalSystem, gameplay::VisionSystem>();
		m_registry.Register(m_heightDies);
		m_registry.Register(m_fallingDamage);
		m_registry.Register(m_jetDamage);
		m_registry.Register(m_index);
		// How each player sees each object through the shroud, for the step's targeting (in the spatial index).
		m_registry.Register(m_objectShroud);
		m_registry.OrderBefore<gameplay::ObjectShroudSystem, gameplay::SpatialIndexSystem>();
		// From where the pre-step movers leave things (the original asks with current positions).
		m_registry.OrderBefore<gameplay::DescentSystem, gameplay::ObjectShroudSystem>();
		m_registry.OrderBefore<gameplay::PhysicsSystem, gameplay::ObjectShroudSystem>();
		m_registry.Register(m_targeting);
		m_registry.Register(m_attackMoves);
		m_registry.Register(m_boarding);
		m_registry.Register(m_unloading);
		m_registry.Register(m_descent);
		m_registry.Register(m_delivery);
		m_registry.Register(m_movement);
		m_registry.Register(m_obstacles);
		m_registry.OrderBefore<gameplay::ObstacleSystem, gameplay::DescentSystem>();
		m_registry.Register(m_routes);
		m_registry.OrderBefore<gameplay::RouteRequestSystem, gameplay::MovementSystem>();
		m_registry.OrderBefore<gameplay::RouteRequestSystem, gameplay::TargetingSystem>();
		m_registry.OrderBefore<gameplay::RouteRequestSystem, gameplay::DeliverySystem>();
		m_registry.OrderBefore<gameplay::RouteRequestSystem, gameplay::JetSystem>();
		m_registry.Register(m_turrets);
		m_registry.Register(m_altTurrets);
		m_registry.Register(m_firing);
		// The trackers cool down before the weapons fire.
		m_registry.Register(m_firingTracker);
		m_registry.OrderBefore<gameplay::FiringTrackerSystem, gameplay::WeaponSystem>();
		m_registry.Register(m_projectiles);
		m_registry.Register(m_launches);
		m_registry.Register(m_missiles);
		m_registry.Register(m_pointDefense);
		m_registry.Register(m_projectileBodies);
		m_registry.Register(m_upgradeTriggers);
		m_registry.Register(m_upgradeEffects);
		m_registry.Register(m_researchCompletion);
		m_registry.OrderBefore<domain::UpgradeEffectSystem, gameplay::RemovalSystem>();
		m_registry.Register(m_productionRefunds);
		m_registry.Register(m_slaves);
		m_registry.Register(m_bonusRetime);
		m_registry.OrderBefore<domain::UpgradeEffectSystem, gameplay::WeaponBonusRetimeSystem>();
		m_registry.OrderBefore<gameplay::WeaponBonusRetimeSystem, domain::AppearanceSystem>();
		m_registry.OrderBefore<gameplay::SlavedSystem, gameplay::TargetingSystem>();
		m_registry.OrderBefore<gameplay::SlavedSystem, gameplay::DeliverySystem>();
		m_registry.OrderBefore<gameplay::SlavedSystem, gameplay::JetSystem>();
		m_registry.OrderBefore<domain::ProductionRefundSystem, gameplay::RemovalSystem>();
		m_registry.OrderBefore<domain::ProductionRefundSystem, gameplay::ProductionSystem>();
		m_registry.Register(m_impacts);
		m_registry.Register(m_health);
		// Subdual damage shed and weighed after the tick's damage, its disables given with the others.
		m_registry.Register(m_subdual);
		m_registry.OrderBefore<gameplay::HealthSystem, gameplay::SubdualSystem>();
		m_registry.OrderBefore<gameplay::SubdualSystem, gameplay::DisableFollowSystem>();
		// Statuses from STATUS damage given and healed after the tick's damage.
		m_registry.Register(m_statusDamage);
		m_registry.OrderBefore<gameplay::HealthSystem, gameplay::StatusDamageSystem>();
		// Missiles subdued this tick are jammed.
		m_registry.Register(m_missileJam);
		m_registry.OrderBefore<gameplay::SubdualSystem, gameplay::MissileJamSystem>();
		m_registry.Register(m_veterancy);
		m_registry.Register(m_hordes);
		m_registry.OrderBefore<gameplay::HordeSystem, gameplay::WeaponSystem>();
		m_registry.OrderBefore<gameplay::HordeSystem, gameplay::SlavedSystem>();
		m_registry.OrderBefore<gameplay::HordeSystem, gameplay::JetSystem>();
		m_registry.OrderBefore<gameplay::HealthSystem, gameplay::VeterancySystem>();
		m_registry.OrderBefore<gameplay::VeterancySystem, gameplay::DeathSystem>();
		m_registry.OrderBefore<gameplay::VeterancySystem, gameplay::SelfHealingSystem>();
		m_registry.OrderBefore<gameplay::VeterancySystem, gameplay::HealingSystem>();
		m_registry.Register(m_lifetime);
		m_registry.Register(m_selfHealing);
		m_registry.Register(m_areaHealing);
		m_registry.Register(m_healing);
		m_registry.Register(m_flammability);
		m_registry.Register(m_fireSpread);
		m_registry.Register(m_autoFire);
		m_registry.Register(m_damageReaction);
		m_registry.OrderBefore<gameplay::HealthSystem, gameplay::DamageReactionSystem>();
		m_registry.OrderBefore<gameplay::AutoFireSystem, gameplay::DamageReactionSystem>();
		m_registry.OrderBefore<gameplay::DamageReactionSystem, gameplay::HealingSystem>();
		m_registry.OrderBefore<gameplay::DamageReactionSystem, gameplay::SelfHealingSystem>();
		m_registry.Register(m_stealth);
		m_registry.Register(m_defectors);
		m_registry.Register(m_parachutes);
		m_registry.Register(m_parachuteLandings);
		m_registry.Register(m_pilotSeekers);
		m_registry.Register(m_healSeekers);
		m_registry.Register(m_specialAbilities);
		m_registry.Register(m_stickyBombs);
		m_registry.Register(m_bridgeDamage);
		m_registry.Register(m_internetHacks);
		m_registry.Register(m_battlePlans);
		m_registry.Register(m_hijackers);
		m_registry.Register(m_warehouseCripplings);
		m_registry.Register(m_battleBuses);
		m_registry.Register(m_weaponBonusPulses);
		m_registry.OrderBefore<gameplay::HordeSystem, domain::WeaponBonusPulseSystem>();
		m_registry.OrderBefore<gameplay::HordeSystem, domain::WeaponBonusPulseSystem>();
		m_registry.OrderBefore<gameplay::HordeSystem, domain::WeaponBonusPulseSystem>();
		m_registry.OrderBefore<gameplay::HordeSystem, domain::WeaponBonusPulseSystem>();
		m_registry.OrderBefore<gameplay::HordeSystem, domain::WeaponBonusPulseSystem>();
		m_registry.OrderBefore<gameplay::HordeSystem, domain::WeaponBonusPulseSystem>();
		m_registry.OrderBefore<gameplay::HordeSystem, domain::WeaponBonusPulseSystem>();
		m_registry.OrderBefore<domain::StickyBombSystem, domain::WeaponBonusPulseSystem>();
		m_registry.OrderBefore<gameplay::NeutronFlightSystem, domain::WeaponBonusPulseSystem>();
		m_registry.Register(m_tempWeaponBonuses);
		m_registry.OrderBefore<domain::StickyBombSystem, domain::BattleBusSystem>();
		m_registry.OrderBefore<gameplay::NeutronFlightSystem, domain::BattleBusSystem>();
		m_registry.OrderBefore<domain::StickyBombSystem, domain::WarehouseCripplingSystem>();
		m_registry.OrderBefore<domain::BridgeDamageSystem, domain::WarehouseCripplingSystem>();
		m_registry.OrderBefore<domain::DroneRepairSystem, domain::WarehouseCripplingSystem>();
		m_registry.OrderBefore<domain::HealSeekSystem, domain::WarehouseCripplingSystem>();
		m_registry.OrderBefore<gameplay::ConstructionSystem, domain::WarehouseCripplingSystem>();
		m_registry.OrderBefore<gameplay::SubdualSystem, domain::WarehouseCripplingSystem>();
		m_registry.OrderBefore<domain::StickyBombSystem, domain::HijackerSystem>();
		m_registry.OrderBefore<gameplay::NeutronFlightSystem, domain::HijackerSystem>();
		m_registry.OrderBefore<gameplay::DeliverySystem, domain::BattlePlanSystem>();
		m_registry.OrderBefore<domain::InternetHackSystem, domain::BattlePlanSystem>();
		m_registry.OrderBefore<domain::BattlePlanSystem, gameplay::WeaponSystem>();
		m_registry.OrderBefore<domain::BattlePlanSystem, gameplay::TurretSystem>();
		m_registry.OrderBefore<domain::BattlePlanSystem, gameplay::JetSystem>();
		m_registry.OrderBefore<domain::InternetHackSystem, gameplay::MovementSystem>();
		m_registry.OrderBefore<domain::InternetHackSystem, domain::CleanupHazardSystem>();
		m_registry.OrderBefore<domain::InternetHackSystem, domain::ScriptedEvacuationSystem>();
		m_registry.OrderBefore<domain::InternetHackSystem, gameplay::DockSystem>();
		m_registry.OrderBefore<domain::InternetHackSystem, domain::MobMemberSystem>();
		m_registry.OrderBefore<domain::InternetHackSystem, domain::HealSeekSystem>();
		m_registry.OrderBefore<domain::InternetHackSystem, gameplay::ConstructionSystem>();
		m_registry.OrderBefore<gameplay::HealthSystem, domain::BridgeDamageSystem>();
		m_registry.OrderBefore<domain::DroneRepairSystem, domain::BridgeDamageSystem>();
		m_registry.OrderBefore<gameplay::MinefieldSystem, domain::BridgeDamageSystem>();
		m_registry.OrderBefore<gameplay::MovementSystem, domain::StickyBombSystem>();
		m_registry.OrderBefore<gameplay::MissileJamSystem, domain::StickyBombSystem>();
		m_registry.OrderBefore<domain::SpectreGunshipSystem, domain::StickyBombSystem>();
		m_registry.OrderBefore<domain::MobMemberSystem, domain::StickyBombSystem>();
		m_registry.OrderBefore<domain::DroneRepairSystem, domain::StickyBombSystem>();
		m_registry.OrderBefore<gameplay::NeutronFlightSystem, domain::StickyBombSystem>();
		m_registry.Register(m_commandButtonHunts);
		m_registry.Register(m_pilotKills);
		m_registry.Register(m_tunnelGuards);
		m_registry.Register(m_scriptedEvacuations);
		m_registry.Register(m_guards);
		m_registry.Register(m_cleanupHazards);
		m_registry.Register(m_spyVisions);
		m_registry.Register(m_countermeasures);
		m_registry.Register(m_repulsion);
		m_registry.OrderBefore<domain::RepulsionSystem, domain::CommandButtonHuntSystem>();
		m_registry.OrderBefore<domain::RepulsionSystem, gameplay::WanderSystem>();
		m_registry.OrderBefore<domain::RepulsionSystem, gameplay::MovePathSystem>();
		m_registry.OrderBefore<domain::RepulsionSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<domain::RepulsionSystem, gameplay::DockSystem>();
		m_registry.Register(m_attackSquads);
		m_registry.OrderBefore<domain::CommandButtonHuntSystem, domain::SpecialAbilitySystem>();
		m_registry.OrderBefore<gameplay::SpatialIndexSystem, domain::CommandButtonHuntSystem>();
		m_registry.OrderBefore<gameplay::MovementSystem, domain::SpecialAbilitySystem>();
		m_registry.OrderBefore<gameplay::RouteRequestSystem, domain::SpecialAbilitySystem>();
		m_registry.Register(m_locomotorDamage);
		m_registry.Register(m_wanderers);
		m_registry.Register(m_movePaths);
		m_registry.Register(m_faceTargets);
		m_registry.OrderBefore<gameplay::FaceTargetSystem, domain::RepulsionSystem>();
		m_registry.OrderBefore<gameplay::FaceTargetSystem, gameplay::WanderSystem>();
		m_registry.OrderBefore<gameplay::FaceTargetSystem, gameplay::MovePathSystem>();
		m_registry.OrderBefore<gameplay::FaceTargetSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<gameplay::FaceTargetSystem, gameplay::DockSystem>();
		m_registry.OrderBefore<gameplay::FaceTargetSystem, gameplay::MovementSystem>();
		m_registry.Register(m_riderRegen);
		m_registry.Register(m_bodyCollisions);
		// Bodies pushed apart once everything moved and the colliders are indexed; crash weapons land with the tick's impacts.
		m_registry.OrderBefore<gameplay::ColliderIndexSystem, gameplay::BodyCollisionSystem>();
		m_registry.OrderBefore<gameplay::MovementSystem, gameplay::BodyCollisionSystem>();
		m_registry.OrderBefore<gameplay::BodyCollisionSystem, gameplay::ImpactSystem>();
		m_registry.OrderBefore<gameplay::CollideWeaponSystem, gameplay::BodyCollisionSystem>();
		m_registry.OrderBefore<gameplay::PointDefenseSystem, gameplay::BodyCollisionSystem>();
		m_registry.OrderBefore<gameplay::MissileFlightSystem, gameplay::BodyCollisionSystem>();
		m_registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::BodyCollisionSystem>();
		m_registry.OrderBefore<gameplay::HealPadSystem, gameplay::RiderRegenSystem>();
		m_registry.OrderBefore<domain::HealSeekSystem, gameplay::RiderRegenSystem>();
		m_registry.OrderBefore<gameplay::WanderSystem, gameplay::MovementSystem>();
		m_registry.OrderBefore<gameplay::MovePathSystem, gameplay::MovementSystem>();
		m_registry.OrderBefore<gameplay::WanderSystem, gameplay::MovePathSystem>();
		m_registry.OrderBefore<gameplay::MovePathSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<gameplay::MovePathSystem, gameplay::DockSystem>();
		m_registry.OrderBefore<gameplay::WanderSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<gameplay::WanderSystem, gameplay::DockSystem>();
		m_registry.OrderBefore<gameplay::LocomotorDamageSystem, gameplay::MovementSystem>();
		m_registry.OrderBefore<gameplay::LocomotorDamageSystem, gameplay::SlavedSystem>();
		m_registry.OrderBefore<gameplay::LocomotorDamageSystem, gameplay::UnloadingSystem>();
		m_registry.Register(m_crashCollisions);
		// A spiralling wreck's collisions after it moves, with this tick's colliders.
		m_registry.OrderBefore<gameplay::SlowDeathSystem, gameplay::CrashCollisionSystem>();
		m_registry.OrderBefore<gameplay::ColliderIndexSystem, gameplay::CrashCollisionSystem>();
		m_registry.OrderBefore<gameplay::CrashCollisionSystem, domain::AppearanceSystem>();
		m_registry.Register(m_techBuildings);
		m_registry.OrderBefore<gameplay::DeathSystem, domain::TechBuildingSystem>();
		m_registry.OrderBefore<domain::TechBuildingSystem, gameplay::RemovalSystem>();
		m_registry.Register(m_healPads);
		// Seekers look once the spatial index is up; pads heal alongside the tick's other healing, before the tick's
		// riders move.
		m_registry.OrderBefore<gameplay::SpatialIndexSystem, domain::HealSeekSystem>();
		m_registry.OrderBefore<gameplay::HealPadSystem, domain::HealSeekSystem>();
		m_registry.OrderBefore<gameplay::HealingSystem, domain::HealSeekSystem>();
		m_registry.OrderBefore<gameplay::SaleSystem, domain::HealSeekSystem>();
		m_registry.OrderBefore<domain::PilotSeekSystem, domain::HealSeekSystem>();
		m_registry.OrderBefore<gameplay::MissileFlightSystem, domain::HealSeekSystem>();
		m_registry.OrderBefore<gameplay::ProjectileFlightSystem, domain::HealSeekSystem>();
		m_registry.OrderBefore<gameplay::JetSystem, domain::HealSeekSystem>();
		m_registry.OrderBefore<gameplay::UnloadingSystem, domain::HealSeekSystem>();
		m_registry.OrderBefore<gameplay::DockSystem, domain::HealSeekSystem>();
		m_registry.OrderBefore<gameplay::HealingSystem, gameplay::HealPadSystem>();
		m_registry.OrderBefore<gameplay::HealPadSystem, gameplay::CargoTransferSystem>();
		m_registry.OrderBefore<gameplay::StealthDetectorSystem, gameplay::HealPadSystem>();
		m_registry.OrderBefore<gameplay::AssistSystem, gameplay::HealPadSystem>();
		m_registry.Register(m_assists);
		m_registry.Register(m_collideWeapons);
		// Collide weapons fire at those who moved into them, after movement, landing with the tick's impacts.
		m_registry.OrderBefore<gameplay::ColliderIndexSystem, gameplay::CollideWeaponSystem>();
		m_registry.OrderBefore<gameplay::MovementSystem, gameplay::CollideWeaponSystem>();
		m_registry.OrderBefore<gameplay::CollideWeaponSystem, gameplay::ImpactSystem>();
		m_registry.OrderBefore<gameplay::PointDefenseSystem, gameplay::CollideWeaponSystem>();
		m_registry.OrderBefore<gameplay::MissileFlightSystem, gameplay::CollideWeaponSystem>();
		m_registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::CollideWeaponSystem>();
		// Those asked to help join in after the tick's shots.
		m_registry.OrderBefore<gameplay::WeaponSystem, gameplay::AssistSystem>();
		m_registry.OrderBefore<gameplay::HealingSystem, gameplay::AssistSystem>();
		m_registry.OrderBefore<gameplay::SaleSystem, gameplay::AssistSystem>();
		m_registry.OrderBefore<gameplay::MissileFlightSystem, gameplay::AssistSystem>();
		m_registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::AssistSystem>();
		// Pilots look for vehicles in this tick's world and bring their levels before the veterancy pass.
		m_registry.OrderBefore<gameplay::SpatialIndexSystem, domain::PilotSeekSystem>();
		m_registry.OrderBefore<domain::PilotSeekSystem, gameplay::VeterancySystem>();
		m_registry.OrderBefore<gameplay::HealthSystem, domain::PilotSeekSystem>();
		m_registry.OrderBefore<gameplay::MissileFlightSystem, domain::PilotSeekSystem>();
		m_registry.OrderBefore<gameplay::ProjectileFlightSystem, domain::PilotSeekSystem>();
		m_registry.OrderBefore<gameplay::JetSystem, domain::PilotSeekSystem>();
		m_registry.OrderBefore<gameplay::UnloadingSystem, domain::PilotSeekSystem>();
		m_registry.OrderBefore<gameplay::DockSystem, domain::PilotSeekSystem>();
		// A parachute's locomotor drives it before physics steps it; it lands, opens and places its rider after, before the
		// tick's queries.
		m_registry.OrderBefore<gameplay::DescentSystem, gameplay::ParachuteSystem>();
		m_registry.OrderBefore<gameplay::ParachuteSystem, gameplay::PhysicsSystem>();
		m_registry.OrderBefore<gameplay::PhysicsSystem, gameplay::ParachuteLandingSystem>();
		m_registry.OrderBefore<gameplay::ParachuteLandingSystem, gameplay::SpatialIndexSystem>();
		m_registry.Register(m_freeFalls);
		m_registry.Register(m_parachuteLosses);
		m_registry.Register(m_pendingDamage);
		// Free fall once physics has moved the body; a lost chute's rider let go after the tick's damage; queued damage
		// with the tick's impacts.
		m_registry.OrderBefore<gameplay::PhysicsSystem, gameplay::FreeFallSystem>();
		m_registry.OrderBefore<gameplay::ParachuteLandingSystem, gameplay::FreeFallSystem>();
		m_registry.OrderBefore<gameplay::FreeFallSystem, gameplay::FallingDamageSystem>();
		m_registry.OrderBefore<gameplay::FreeFallSystem, gameplay::EnergySystem>();
		m_registry.OrderBefore<gameplay::FreeFallSystem, gameplay::StealthSystem>();
		m_registry.OrderBefore<gameplay::ImpactSystem, gameplay::PendingDamageSystem>();
		m_registry.OrderBefore<gameplay::FallingDamageSystem, gameplay::PendingDamageSystem>();
		m_registry.OrderBefore<gameplay::PendingDamageSystem, gameplay::HealingSystem>();
		m_registry.OrderBefore<gameplay::CrushSystem, gameplay::PendingDamageSystem>();
		m_registry.OrderBefore<gameplay::FlammabilitySystem, gameplay::PendingDamageSystem>();
		m_registry.OrderBefore<gameplay::DeathSystem, gameplay::ParachuteLossSystem>();
		m_registry.OrderBefore<gameplay::ParachuteLossSystem, gameplay::SpawnerSystem>();
		m_registry.OrderBefore<gameplay::ParachuteLossSystem, gameplay::SlowDeathSystem>();
		m_registry.OrderBefore<gameplay::ParachuteLossSystem, gameplay::LoadoutSystem>();
		m_registry.OrderBefore<gameplay::DefectorSystem, gameplay::SpatialIndexSystem>();
		m_registry.OrderBefore<gameplay::StealthSystem, gameplay::DefectorSystem>();
		m_registry.Register(m_stealthDetectors);
		m_registry.Register(m_stealthReveal);
		m_registry.Register(m_stealthGrants);
		m_registry.Register(m_colliderIndex);
		m_registry.Register(m_contacts);
		m_registry.Register(m_topple);
		m_registry.Register(m_blastWaves);
		m_registry.Register(m_blastDamage);
		m_registry.Register(m_scorch);
		m_registry.Register(m_overchargeDrain);
		m_registry.Register(m_overchargeLimit);
		m_registry.Register(m_mounts);
		m_registry.Register(m_propagandaScan);
		m_registry.Register(m_propagandaInfluence);
		m_registry.Register(m_mineDrain);
		m_registry.Register(m_minefields);
		m_registry.Register(m_demoTraps);
		m_registry.Register(m_empPulses);
		m_registry.Register(m_autoDeposits);
		m_registry.Register(m_areaPresence);
		m_registry.Register(m_disableExpiry);
		m_registry.Register(m_disableFollow);
		m_registry.Register(m_disableApply);
		// A mine drains with the tick's damage and follows its health once it is taken.
		m_registry.OrderBefore<gameplay::ImpactSystem, gameplay::MineDrainSystem>();
		m_registry.OrderBefore<gameplay::MineDrainSystem, gameplay::PendingDamageSystem>();
		m_registry.OrderBefore<gameplay::HealthSystem, gameplay::MinefieldSystem>();
		m_registry.OrderBefore<domain::CommandButtonHuntSystem, domain::AttackSquadSystem>();
		m_registry.OrderBefore<gameplay::EmpPulseSystem, domain::AttackSquadSystem>();
		m_registry.OrderBefore<domain::AttackSquadSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<domain::TunnelGuardSystem, domain::AttackSquadSystem>();
		m_registry.OrderBefore<gameplay::SpatialIndexSystem, domain::TunnelGuardSystem>();
		m_registry.OrderBefore<domain::TunnelGuardSystem, gameplay::DemoTrapSystem>();
		m_registry.OrderBefore<domain::TunnelGuardSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<domain::DeploySystem, gameplay::AttackMoveSystem>();
		m_registry.OrderBefore<gameplay::JetSystem, gameplay::AttackMoveSystem>();
		m_registry.OrderBefore<gameplay::UnloadingSystem, gameplay::AttackMoveSystem>();
		m_registry.OrderBefore<gameplay::FiringTrackerSystem, gameplay::PassedBonusSystem>();
		m_registry.OrderBefore<gameplay::FiringTrackerSystem, domain::CleanupHazardSystem>();
		m_registry.OrderBefore<gameplay::FiringTrackerSystem, domain::CooldownCreationSystem>();
		m_registry.OrderBefore<gameplay::FiringTrackerSystem, domain::RepulsionSystem>();
		m_registry.OrderBefore<gameplay::FiringTrackerSystem, domain::TunnelGuardSystem>();
		m_registry.OrderBefore<gameplay::HiveDamageSystem, gameplay::GarrisonKillDamageSystem>();
		m_registry.OrderBefore<gameplay::PendingDamageSystem, gameplay::GarrisonKillDamageSystem>();
		m_registry.OrderBefore<domain::ProjectileBodySystem, gameplay::MissileJamSystem>();
		m_registry.OrderBefore<gameplay::NeutronFlightSystem, gameplay::MissileJamSystem>();
		m_registry.OrderBefore<gameplay::SubdualSystem, gameplay::MinefieldSystem>();
		m_registry.OrderBefore<gameplay::RemovalSystem, gameplay::PassengerRideSystem>();
		m_registry.OrderBefore<gameplay::PassedBonusSystem, domain::ScriptedEvacuationSystem>();
		m_registry.OrderBefore<gameplay::PassedBonusSystem, gameplay::HordeSystem>();
		m_registry.OrderBefore<gameplay::VisionSystem, gameplay::CrashCollisionSystem>();
		m_registry.OrderBefore<gameplay::StealthRevealSystem, gameplay::ShockWaveSystem>();
		m_registry.OrderBefore<gameplay::NeutronFlightSystem, domain::SpectreGunshipSystem>();
		m_registry.OrderBefore<gameplay::NeutronFlightSystem, domain::MobMemberSystem>();
		m_registry.OrderBefore<domain::TunnelGuardSystem, domain::MobMemberSystem>();
		m_registry.OrderBefore<gameplay::SpawnerSystem, gameplay::ProductionSystem>();
		m_registry.OrderBefore<gameplay::SaleSystem, domain::ParticleCannonSystem>();
		m_registry.OrderBefore<domain::ParticleCannonSystem, gameplay::HiveDamageSystem>();
		m_registry.OrderBefore<gameplay::PendingDamageSystem, domain::ParticleCannonSystem>();
		m_registry.OrderBefore<gameplay::JetSystem, domain::ParticleCannonSystem>();
		m_registry.OrderBefore<domain::LauncherDoorSystem, gameplay::NeutronFlightSystem>();
		m_registry.OrderBefore<gameplay::GrantStealthSystem, gameplay::NeutronFlightSystem>();
		m_registry.OrderBefore<domain::HealSeekSystem, gameplay::NeutronFlightSystem>();
		m_registry.OrderBefore<gameplay::ConstructionSystem, gameplay::NeutronFlightSystem>();
		m_registry.OrderBefore<gameplay::DockSystem, gameplay::NeutronFlightSystem>();
		m_registry.OrderBefore<domain::TunnelGuardSystem, gameplay::NeutronFlightSystem>();
		m_registry.OrderBefore<gameplay::MinefieldSystem, domain::LauncherDoorSystem>();
		m_registry.OrderBefore<gameplay::ProjectileFlightSystem, domain::LauncherDoorSystem>();
		m_registry.OrderBefore<gameplay::JetSystem, domain::LauncherDoorSystem>();
		m_registry.OrderBefore<gameplay::JetSystem, domain::DeploySystem>();
		m_registry.OrderBefore<gameplay::UnloadingSystem, domain::DeploySystem>();
		m_registry.OrderBefore<domain::CooldownCreationSystem, gameplay::DemoTrapSystem>();
		m_registry.OrderBefore<domain::CooldownCreationSystem, gameplay::JetSystem>();
		m_registry.OrderBefore<gameplay::PendingDamageSystem, gameplay::HiveDamageSystem>();
		m_registry.OrderBefore<gameplay::JetSystem, gameplay::HiveDamageSystem>();
		m_registry.OrderBefore<gameplay::RiderRegenSystem, domain::DroneRepairSystem>();
		m_registry.OrderBefore<gameplay::LocomotorDamageSystem, gameplay::RepairDockSystem>();
		m_registry.OrderBefore<domain::HealSeekSystem, domain::DroneRepairSystem>();
		m_registry.OrderBefore<gameplay::ConstructionSystem, domain::DroneRepairSystem>();
		m_registry.OrderBefore<gameplay::CountermeasuresSystem, gameplay::HealthSystem>();
		m_registry.OrderBefore<gameplay::CountermeasuresSystem, gameplay::MinefieldSystem>();
		m_registry.OrderBefore<gameplay::ProjectileLaunchSystem, gameplay::CountermeasuresSystem>();
		m_registry.OrderBefore<domain::SpyVisionSystem, gameplay::SlowDeathSystem>();
		m_registry.OrderBefore<domain::UpgradeEffectSystem, domain::SpyVisionSystem>();
		m_registry.OrderBefore<domain::CleanupHazardSystem, domain::GuardSystem>();
		m_registry.OrderBefore<domain::CleanupHazardSystem, domain::ScriptedEvacuationSystem>();
		m_registry.OrderBefore<domain::CleanupHazardSystem, domain::TunnelGuardSystem>();
		m_registry.OrderBefore<gameplay::SpatialIndexSystem, domain::CleanupHazardSystem>();
		m_registry.OrderBefore<domain::GuardSystem, domain::ScriptedEvacuationSystem>();
		m_registry.OrderBefore<domain::GuardSystem, domain::TunnelGuardSystem>();
		m_registry.OrderBefore<gameplay::SpatialIndexSystem, domain::GuardSystem>();
		m_registry.OrderBefore<domain::GuardSystem, domain::AttackSquadSystem>();
		m_registry.OrderBefore<domain::GuardSystem, gameplay::DemoTrapSystem>();
		m_registry.OrderBefore<domain::GuardSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<domain::ScriptedEvacuationSystem, gameplay::LocomotorDamageSystem>();
		m_registry.OrderBefore<domain::ScriptedEvacuationSystem, gameplay::FaceTargetSystem>();
		m_registry.OrderBefore<domain::ScriptedEvacuationSystem, domain::RepulsionSystem>();
		m_registry.OrderBefore<domain::ScriptedEvacuationSystem, gameplay::WanderSystem>();
		m_registry.OrderBefore<domain::ScriptedEvacuationSystem, gameplay::MovePathSystem>();
		m_registry.OrderBefore<domain::ScriptedEvacuationSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<domain::ScriptedEvacuationSystem, gameplay::DockSystem>();
		m_registry.OrderBefore<domain::PilotKillSystem, gameplay::SelfHealingSystem>();
		m_registry.OrderBefore<domain::PilotKillSystem, gameplay::HealingSystem>();
		m_registry.OrderBefore<gameplay::MinefieldSystem, domain::PilotKillSystem>();
		m_registry.OrderBefore<gameplay::EmpPulseSystem, domain::CommandButtonHuntSystem>();
		m_registry.OrderBefore<domain::CommandButtonHuntSystem, gameplay::WanderSystem>();
		m_registry.OrderBefore<domain::CommandButtonHuntSystem, gameplay::MovePathSystem>();
		m_registry.OrderBefore<domain::CommandButtonHuntSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<domain::CommandButtonHuntSystem, gameplay::DockSystem>();
		m_registry.OrderBefore<domain::SpecialAbilitySystem, gameplay::ContactSystem>();
		m_registry.OrderBefore<domain::SpecialAbilitySystem, gameplay::AltTurretSystem>();
		m_registry.OrderBefore<domain::SpecialAbilitySystem, gameplay::TurretSystem>();
		m_registry.OrderBefore<domain::SpecialAbilitySystem, gameplay::ProjectileFlightSystem>();
		m_registry.OrderBefore<domain::SpecialAbilitySystem, gameplay::GrantStealthSystem>();
		m_registry.OrderBefore<domain::SpecialAbilitySystem, gameplay::ConstructionSystem>();
		m_registry.OrderBefore<gameplay::AreaPresenceSystem, gameplay::HeightDieSystem>();
		m_registry.OrderBefore<gameplay::AreaPresenceSystem, gameplay::SlowDeathSystem>();
		m_registry.OrderBefore<gameplay::AutoDepositSystem, gameplay::SaleSystem>();
		m_registry.OrderBefore<gameplay::AutoDepositSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<gameplay::AutoDepositSystem, gameplay::JetSystem>();
		m_registry.OrderBefore<gameplay::DisableFollowSystem, gameplay::SpawnerSystem>();
		m_registry.OrderBefore<gameplay::DemoTrapSystem, gameplay::EmpPulseSystem>();
		m_registry.OrderBefore<gameplay::DisableExpirySystem, gameplay::ParachuteSystem>();
		m_registry.OrderBefore<gameplay::DisableExpirySystem, gameplay::ParachuteLandingSystem>();
		m_registry.OrderBefore<gameplay::EmpPulseSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<gameplay::EmpPulseSystem, gameplay::JetSystem>();
		m_registry.OrderBefore<gameplay::DisableExpirySystem, gameplay::FreeFallSystem>();
		m_registry.OrderBefore<gameplay::DemoTrapSystem, gameplay::SaleSystem>();
		m_registry.OrderBefore<gameplay::DemoTrapSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<gameplay::DemoTrapSystem, gameplay::JetSystem>();
		m_registry.OrderBefore<gameplay::MineDrainSystem, gameplay::OverchargeDrainSystem>();
		m_registry.OrderBefore<gameplay::MineDrainSystem, gameplay::JetDamageSystem>();
		m_registry.OrderBefore<gameplay::MineDrainSystem, gameplay::FallingDamageSystem>();
		m_registry.OrderBefore<gameplay::MinefieldSystem, gameplay::OverchargeLimitSystem>();
		m_registry.OrderBefore<gameplay::MinefieldSystem, gameplay::DamageReactionSystem>();
		m_registry.OrderBefore<gameplay::MinefieldSystem, domain::PilotSeekSystem>();
		m_registry.OrderBefore<gameplay::MinefieldSystem, gameplay::GrantStealthSystem>();
		m_registry.OrderBefore<gameplay::MinefieldSystem, gameplay::ConstructionSystem>();
		// Propaganda heals join the tick's heal pulses once area healing has gathered them, before they are applied.
		m_registry.OrderBefore<gameplay::AreaHealingSystem, gameplay::PropagandaInfluenceSystem>();
		m_registry.OrderBefore<gameplay::PropagandaInfluenceSystem, gameplay::HealingSystem>();
		m_registry.OrderBefore<gameplay::PropagandaScanSystem, gameplay::SaleSystem>();
		m_registry.OrderBefore<gameplay::PropagandaScanSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<gameplay::PropagandaScanSystem, gameplay::JetSystem>();
		m_registry.OrderBefore<gameplay::PropagandaInfluenceSystem, gameplay::ConstructionSystem>();
		// Mounted things ride on and die with their carriers, with the tick's deaths.
		m_registry.OrderBefore<gameplay::MountSystem, gameplay::DeathSystem>();
		// An overcharge drains with the tick's damage and gives out once it is taken.
		m_registry.OrderBefore<gameplay::ImpactSystem, gameplay::OverchargeDrainSystem>();
		m_registry.OrderBefore<gameplay::OverchargeDrainSystem, gameplay::PendingDamageSystem>();
		m_registry.OrderBefore<gameplay::HealthSystem, gameplay::OverchargeLimitSystem>();
		m_registry.OrderBefore<gameplay::MountSystem, gameplay::VictorySystem>();
		m_registry.OrderBefore<gameplay::OverchargeDrainSystem, gameplay::BlastDamageSystem>();
		m_registry.OrderBefore<gameplay::OverchargeDrainSystem, gameplay::JetDamageSystem>();
		m_registry.OrderBefore<gameplay::OverchargeLimitSystem, gameplay::SelfHealingSystem>();
		m_registry.OrderBefore<gameplay::OverchargeLimitSystem, gameplay::HealingSystem>();
		m_registry.OrderBefore<gameplay::OverchargeDrainSystem, gameplay::FallingDamageSystem>();
		// A missile's blasts push things over before the tick's toppling; their damage joins the tick's after the impacts.
		m_registry.OrderBefore<gameplay::BlastWaveSystem, gameplay::ToppleSystem>();
		m_registry.OrderBefore<gameplay::BlastWaveSystem, gameplay::JetSystem>();
		m_registry.OrderBefore<gameplay::ScorchSystem, gameplay::JetSystem>();
		m_registry.OrderBefore<gameplay::BlastDamageSystem, gameplay::JetDamageSystem>();
		m_registry.OrderBefore<gameplay::BlastDamageSystem, gameplay::FallingDamageSystem>();
		m_registry.OrderBefore<gameplay::ScorchSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<gameplay::BlastWaveSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<gameplay::ImpactSystem, gameplay::BlastDamageSystem>();
		m_registry.OrderBefore<gameplay::BlastDamageSystem, gameplay::PendingDamageSystem>();
		m_registry.Register(m_crush);
		m_registry.Register(m_production);
		m_registry.Register(m_energy);
		m_registry.Register(m_brownOut);
		m_registry.Register(m_jets);
		// Supply trucks: their rounds, then their docking, before they are routed and moved.
		// Weapon and armor sets follow their flags after the tick's upgrades and promotions.
		m_registry.Register(m_loadouts);
		m_registry.OrderBefore<domain::UpgradeEffectSystem, gameplay::LoadoutSystem>();
		m_registry.OrderBefore<gameplay::VeterancySystem, gameplay::LoadoutSystem>();
		m_registry.OrderBefore<gameplay::LoadoutSystem, gameplay::WeaponBonusRetimeSystem>();
		// Crates are run into where the tick's movers ended up.
		m_registry.Register(m_crateTouches);
		m_registry.OrderBefore<gameplay::ColliderIndexSystem, domain::CrateTouchSystem>();
		m_registry.OrderBefore<domain::CrateTouchSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<domain::CrateTouchSystem, gameplay::JetSystem>();
		m_registry.Register(m_harvestRoster);
		m_registry.Register(m_harvest);
		m_registry.Register(m_docks);
		m_registry.Register(m_repairDocks);
		m_registry.Register(m_hiveDamage);
		m_registry.Register(m_cooldownCreations);
		m_registry.Register(m_deploys);
		m_registry.Register(m_launcherDoors);
		m_registry.Register(m_particleCannons);
		m_registry.Register(m_mobMembers);
		m_registry.Register(m_spectreGunships);
		m_registry.OrderBefore<domain::ParticleCannonSystem, gameplay::HealthSystem>();
		m_registry.Register(m_neutronFlights);
		m_registry.OrderBefore<domain::DeploySystem, gameplay::MovementSystem>();
		m_registry.OrderBefore<domain::DeploySystem, gameplay::TurretSystem>();
		m_registry.OrderBefore<domain::DeploySystem, gameplay::AltTurretSystem>();
		m_registry.OrderBefore<domain::CooldownCreationSystem, gameplay::WeaponSystem>();
		m_registry.OrderBefore<gameplay::HiveDamageSystem, gameplay::HealthSystem>();
		m_registry.Register(m_droneRepairs);
		m_registry.OrderBefore<gameplay::RepairDockSystem, gameplay::DockSystem>();
		m_registry.OrderBefore<gameplay::HarvestSystem, gameplay::RepairDockSystem>();
		m_registry.OrderBefore<gameplay::HarvestRosterSystem, gameplay::HarvestSystem>();
		m_registry.OrderBefore<gameplay::HarvestSystem, gameplay::DockSystem>();
		m_registry.OrderBefore<gameplay::DockSystem, gameplay::RouteRequestSystem>();
		m_registry.OrderBefore<gameplay::HarvestSystem, gameplay::HordeSystem>();
		m_registry.OrderBefore<gameplay::DockSystem, gameplay::SlavedSystem>();
		// Hiding is settled before the tick's queries.
		m_registry.OrderBefore<gameplay::StealthSystem, gameplay::SpatialIndexSystem>();
		// Detectors reveal after the tick's shots, for the next tick's targeting.
		m_registry.OrderBefore<gameplay::WeaponSystem, gameplay::StealthDetectorSystem>();
		m_registry.OrderBefore<gameplay::HealingSystem, gameplay::StealthDetectorSystem>();
		m_registry.OrderBefore<gameplay::WeaponSystem, gameplay::GrantStealthSystem>();
		m_registry.OrderBefore<gameplay::GrantStealthSystem, gameplay::LifetimeSystem>();
		// The look shows the doors as production left them this tick.
		m_registry.OrderBefore<gameplay::ProductionSystem, domain::AppearanceSystem>();
		m_registry.OrderBefore<gameplay::RemovalSystem, domain::AppearanceSystem>();
		// Bodies are indexed where they ended up.
		m_registry.OrderBefore<gameplay::PhysicsSystem, gameplay::ColliderIndexSystem>();
		// Things fall before this tick's shots are aimed.
		m_registry.OrderBefore<gameplay::ToppleSystem, gameplay::WeaponSystem>();
		// Projectiles fly on from where the tick's shots left them.
		m_registry.OrderBefore<gameplay::WeaponSystem, gameplay::ProjectileFlightSystem>();
		m_registry.OrderBefore<gameplay::WeaponSystem, gameplay::MissileFlightSystem>();
		m_registry.OrderBefore<gameplay::MissileFlightSystem, gameplay::FireSpreadSystem>();
		m_registry.OrderBefore<gameplay::MissileFlightSystem, gameplay::GrantStealthSystem>();
		m_registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::ImpactSystem>();
		m_registry.OrderBefore<gameplay::SpatialIndexSystem, gameplay::ProjectileFlightSystem>();
		m_registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::AutoFireSystem>();
		m_registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::FireSpreadSystem>();
		m_registry.OrderBefore<gameplay::ProjectileFlightSystem, gameplay::GrantStealthSystem>();
		// Turrets aim before this tick's shots.
		m_registry.OrderBefore<gameplay::TurretSystem, gameplay::WeaponSystem>();
		m_registry.OrderBefore<gameplay::ToppleSystem, gameplay::TurretSystem>();
		m_registry.OrderBefore<gameplay::AltTurretSystem, gameplay::WeaponSystem>();
		m_registry.OrderBefore<gameplay::ToppleSystem, gameplay::AltTurretSystem>();
		// Crushing joins the tick's damage after the impacts and the burning.
		m_registry.OrderBefore<gameplay::ImpactSystem, gameplay::CrushSystem>();
		m_registry.OrderBefore<gameplay::FlammabilitySystem, gameplay::CrushSystem>();
		// Jets take over their own movement after the containers have unloaded theirs.
		m_registry.OrderBefore<gameplay::UnloadingSystem, gameplay::JetSystem>();
		// Transports know who is on the way to them before they decide to come down.
		m_registry.OrderBefore<gameplay::BoardingSystem, gameplay::UnloadingSystem>();
		m_registry.Register(m_cargo);
		m_registry.Register(m_passengerRide);
		m_registry.OrderBefore<gameplay::SlowDeathSystem, gameplay::PassengerRideSystem>();
		m_registry.Register(m_death);
		m_registry.Register(m_victory);
		m_registry.OrderBefore<gameplay::VictorySystem, gameplay::DeathSystem>();
		// Things that hit the ground die with the tick's deaths, after the match looked at who is left.
		m_registry.OrderBefore<gameplay::VictorySystem, gameplay::HeightDieSystem>();
		// What was let go this tick falls from where it was put; its height death joins this tick's deaths.
		m_registry.OrderBefore<gameplay::CargoTransferSystem, gameplay::HeightDieSystem>();
		m_registry.OrderBefore<gameplay::HeightDieSystem, gameplay::DeathSystem>();
		m_registry.Register(m_slowDeath);
		m_registry.Register(m_removal);
		m_registry.Register(m_appearance);
		// Turn looks follow this tick's steering, over the conditions the appearance system keeps.
		m_registry.Register(m_steeringLooks);
		m_registry.OrderBefore<gameplay::MovementSystem, domain::SteeringLookSystem>();
		m_registry.OrderBefore<domain::AppearanceSystem, domain::SteeringLookSystem>();
		// Poison hurts join this tick's damage (after fire's).
		m_registry.Register(m_poison);
		m_registry.Register(m_sale);
		m_registry.OrderBefore<gameplay::SaleSystem, gameplay::SelfHealingSystem>();
		m_registry.Register(m_construction);
		// Repairs add to the heal pulses area healing gathered, before they are applied.
		m_registry.OrderBefore<gameplay::AreaHealingSystem, gameplay::ConstructionSystem>();
		m_registry.OrderBefore<gameplay::ConstructionSystem, gameplay::HealingSystem>();
		m_registry.OrderBefore<gameplay::FlammabilitySystem, gameplay::PoisonSystem>();
		m_registry.OrderBefore<gameplay::PoisonSystem, gameplay::CrushSystem>();
		// Spawners see this tick's deaths (their own and their spawns').
		m_registry.Register(m_spawners);
		m_registry.OrderBefore<gameplay::DeathSystem, gameplay::SpawnerSystem>();
		m_registry.OrderBefore<gameplay::SpawnerSystem, gameplay::SlowDeathSystem>();
		m_registry.OrderBefore<gameplay::SpawnerSystem, gameplay::LoadoutSystem>();
		m_registry.OrderBefore<domain::SteeringLookSystem, gameplay::SnapshotSystem>();
		// Radar dishes and control rods, over the conditions the appearance system keeps.
		m_registry.Register(m_extensionLooks);
		m_registry.OrderBefore<domain::AppearanceSystem, domain::ExtensionLookSystem>();
		m_registry.OrderBefore<domain::ExtensionLookSystem, gameplay::SnapshotSystem>();
		m_registry.OrderBefore<domain::SteeringLookSystem, domain::ExtensionLookSystem>();
		m_registry.Register(m_panicLooks);
		m_registry.OrderBefore<domain::SteeringLookSystem, domain::PanicLookSystem>();
		m_registry.OrderBefore<domain::PanicLookSystem, gameplay::SnapshotSystem>();
		m_registry.OrderBefore<domain::PanicLookSystem, domain::ExtensionLookSystem>();
		m_registry.Register(m_snapshot);
		// Bodies move before this tick's spatial index is built.
		m_registry.OrderBefore<gameplay::PhysicsSystem, gameplay::SpatialIndexSystem>();
		m_registry.OrderBefore<gameplay::DescentSystem, gameplay::PhysicsSystem>();
		// Fire reads this tick's impacts and adds its burning before health applies them.
		m_registry.OrderBefore<gameplay::ImpactSystem, gameplay::FlammabilitySystem>();
		m_registry.OrderBefore<gameplay::ImpactSystem, gameplay::FallingDamageSystem>();
		m_registry.OrderBefore<gameplay::FallingDamageSystem, gameplay::FlammabilitySystem>();
		// Jets circling a dead airfield hurt after the jets' step and the tick's impacts, before falls.
		m_registry.OrderBefore<gameplay::JetSystem, gameplay::JetDamageSystem>();
		m_registry.OrderBefore<gameplay::ImpactSystem, gameplay::JetDamageSystem>();
		m_registry.OrderBefore<gameplay::JetDamageSystem, gameplay::FallingDamageSystem>();
		m_registry.OrderBefore<gameplay::JetDamageSystem, gameplay::HealthSystem>();
		// Fire spreads from where things are after this tick's moves.
		m_registry.OrderBefore<gameplay::MovementSystem, gameplay::FireSpreadSystem>();
		m_registry.OrderBefore<gameplay::WeaponSystem, gameplay::FireSpreadSystem>();
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
	void EnableProfiling(bool enabled) { m_scheduler->EnableProfiling(enabled); }
	std::vector<ecs::Scheduler::SystemTiming> Profile() const { return m_scheduler->Profile(); }
	std::uint64_t WaveNanos() const noexcept { return m_scheduler->WaveNanos(); }
	std::size_t WaveCount() const noexcept { return m_scheduler->WaveCount(); }
	bool IsBatchSystem(std::string_view name) const { return m_scheduler->IsBatch(name); }

private:

	// What this tick's shots create: a weapon's fire creation list where it
	// fired, its detonation creation list where it landed (napalm fire fields).
	void CarryOutShots()
	{
		const auto teamOf = [&](ecs::Entity entity) {
			const auto *member = m_world.IsAlive(entity) ? m_world.Get<gameplay::TeamMember>(entity) : nullptr;
			return member != nullptr ? member->team : gameplay::NoTeam;
		};
		m_fired.ForEach([&](const gameplay::Shot &shot) {
			if (const auto *weapon = m_templates.WeaponContentAt(shot.weapon); weapon != nullptr && !weapon->fireOCL.empty())
				domain::RunCreationList(m_game, weapon->fireOCL, {shot.origin, Engine::Math::Heading((shot.aim - shot.origin).XY()), teamOf(shot.source), shot.source});
		});
		for (const gameplay::Impact &impact : m_shots.Impacts())
			if (const auto *weapon = m_templates.WeaponContentAt(impact.weapon); weapon != nullptr && !weapon->detonationOCL.empty())
				domain::RunCreationList(m_game, weapon->detonationOCL, {impact.position, {}, teamOf(impact.source), impact.source});
	}

	// Factories bring out what they finished; the AI players queue their teams' next units.
	void CarryOutProduction()
	{
		// Object::scoreTheKill: the general's points for this tick's kills.
		const std::vector<gameplay::SkillPointAward> awards = std::move(m_skillPointAwards.list);
		m_skillPointAwards.list.clear();
		for (const gameplay::SkillPointAward &award : awards)
		{
			domain::AddSkillPoints(m_game, award.player, award.points);
			domain::DoBountyForKill(m_game, award.player, award.killer, award.victimDefinition, award.victimPlayer);
		}
		// Object::scoreTheKill: the score keepers hear of this tick's kills.
		domain::ScoreKills(m_game);
		// Player::onStructureConstructionComplete: the computer players hear of their finished structures.
		for (const gameplay::ConstructionDone &done : m_world.Resource<gameplay::ConstructionsDone>().list)
		{
			domain::OnBuildComplete(m_game, done.structure); // DozerAIUpdate: the structure's create modules
			domain::ScoreStructureComplete(m_game, done.structure, done.rebuild); // its score keeper: built (not a rebuild)
		}
		// Player::onStructureConstructionComplete: EVA hears of a superweapon put up.
		for (const gameplay::ConstructionDone &done : m_world.Resource<gameplay::ConstructionsDone>().list)
			domain::NoticeSuperweaponDetected(m_game, done.structure);
		for (const gameplay::ConstructionDone &done : m_world.Resource<gameplay::ConstructionsDone>().list)
			if (const auto *owner = m_world.IsAlive(done.structure) ? m_world.Get<gameplay::Owner>(done.structure) : nullptr)
				if (domain::AiPlayer *ai = m_aiPlayers.Of(owner->player))
					domain::OnAiStructureProduced(m_game, *ai, done.structure);
		std::vector<gameplay::Produced> produced;
		m_world.Resource<gameplay::ProductionDone>().ForEach([&](const gameplay::Produced &done) { produced.push_back(done); });
		for (const gameplay::Produced &done : produced)
			if (done.kind == gameplay::ProductionKind::Unit) // research completes within the tick (ResearchCompletionSystem)
			{
				const std::vector<ecs::Entity> units = domain::OnProduced(m_game, done);
				for (const ecs::Entity unit : units)
					domain::ScoreUnitCreated(m_game, unit); // Player::onUnitCreated: its score keeper
				// ProductionUpdate: the first of it says it is ready (VoiceCreate).
				if (!units.empty())
					m_productionNotices.created.push_back(units.front());
				for (const ecs::Entity unit : units)
					if (const auto *owner = m_world.Get<gameplay::Owner>(done.factory))
						if (domain::AiPlayer *ai = m_aiPlayers.Of(owner->player))
							domain::OnAiUnitProduced(m_game, *ai, done.factory, unit); // Player::onUnitCreated
			}
		domain::AssignAirfields(m_game);
	}

	// Spreading fire throws its embers (OCLEmbers) where it burns.
	void CarryOutFireSpread()
	{
		m_world.Resource<gameplay::SpreadTries>().ForEach([&](const gameplay::SpreadTry &spread) {
			const auto *definition = m_world.IsAlive(spread.entity) ? m_world.Get<gameplay::DefinitionRef>(spread.entity) : nullptr;
			if (definition == nullptr)
				return;
			const auto fire = content::ReadObjectFireSpread(m_templates.DefinitionAt(definition->index), m_step);
			if (!fire || fire->embers.empty())
				return;
			const auto *member = m_world.Get<gameplay::TeamMember>(spread.entity);
			const auto facing = m_world.Get<gameplay::Transform>(spread.entity)->facing;
			domain::RunCreationList(m_game, fire->embers, {spread.position, facing, member != nullptr ? member->team : gameplay::NoTeam});
		});
	}

	// Spawns asked for this tick, made in the order asked.
	void CarryOutSpawns()
	{
		const std::vector<gameplay::SpawnRequest> requests = m_world.Resource<gameplay::SpawnRequests>().list;
		for (const gameplay::SpawnRequest &request : requests)
		{
			const ecs::Entity spawn = domain::SpawnFrom(m_game, request);
			// SpawnBehavior::createSpawn: Player::onUnitCreated(parent, spawn).
			domain::ScoreUnitCreated(m_game, spawn);
			if (const auto *owner = m_world.IsAlive(spawn) ? m_world.Get<gameplay::Owner>(spawn) : nullptr)
				if (domain::AiPlayer *ai = m_aiPlayers.Of(owner->player))
					domain::OnAiUnitProduced(m_game, *ai, request.spawner, spawn);
		}
		// computeAggregateStates: a spawner that is its spawns and they share the higher veterancy (a spawn above it
		// raises it; one below it is raised), in the spawns' order.
		{
			std::vector<std::pair<ecs::Entity, gameplay::Spawner>> aggregates;
			ecs::Query<ecs::Read<gameplay::Spawner>> spawners(m_world);
			spawners.ForEachChunk([&](auto chunk) {
				const auto rows = chunk.template Get<gameplay::Spawner>();
				const auto entities = chunk.Entities();
				for (std::size_t row = 0; row < rows.size(); ++row)
					if (rows[row].aggregateHealth)
						aggregates.emplace_back(entities[row], rows[row]);
			});
			for (const auto &[entity, spawner] : aggregates)
			{
				const auto *own = m_world.Get<gameplay::Experience>(entity);
				if (own == nullptr)
					continue;
				for (std::size_t index = 0; index < spawner.spawnedCount; ++index)
				{
					const ecs::Entity spawn = spawner.spawned[index];
					const auto *theirs = m_world.IsAlive(spawn) ? m_world.Get<gameplay::Experience>(spawn) : nullptr;
					if (theirs == nullptr)
						continue;
					const std::uint8_t level = m_world.Get<gameplay::Experience>(entity)->level;
					if (theirs->level > level)
						domain::PlaceAtVeterancy(m_game, entity, theirs->level, true);
					else if (theirs->level < level)
						domain::PlaceAtVeterancy(m_game, spawn, level, true);
				}
			}
		}
		// A spawner that was only its spawns goes with the last of them (destroyObject: gone, not killed).
		for (const ecs::Entity emptied : m_world.Resource<gameplay::SpawnRequests>().emptied)
			if (m_world.IsAlive(emptied))
			{
				if (!m_world.Has<gameplay::Lifetime>(emptied))
					m_world.Add<gameplay::Lifetime>(emptied);
				*m_world.Get<gameplay::Lifetime>(emptied) = gameplay::Lifetime{m_game.tick, 1, 0};
			}
	}

	// Crates run into this tick go to the first that may take them.
	void CarryOutCratePickups()
	{
		auto &pickups = m_world.Resource<domain::CratePickups>();
		pickups.list.clear();
		std::vector<domain::CrateTouch> touches;
		m_world.Resource<domain::CrateTouches>().AppendTo(touches);
		if (!touches.empty())
			domain::PickUpCrates(m_game, touches, pickups);
		// Terrorists at the vehicles they make car bombs (their ConvertToCarBombCrateCollide).
		domain::ApplyCarBombs(m_game);
		// Hijackers at the vehicles they take (ConvertToHijackedVehicleCrateCollide).
		domain::ApplyHijacks(m_game);
	}

	// GenerateMinefieldBehavior::onDie (GenerateOnlyOnDeath): those that died this tick lay their minefields where they were.
	void CarryOutMinefieldDeaths()
	{
		for (const gameplay::Casualty &casualty : m_casualties.list)
		{
			if (casualty.departure != gameplay::Departure::Killed)
				continue;
			const auto generator = content::ReadMinefieldGenerator(m_templates.DefinitionAt(casualty.definition), m_templates.Content().gameData);
			if (generator && generator->onDeath)
				domain::PlaceMines(m_game, {casualty.entity, casualty.definition, casualty.transform, casualty.team}, false);
		}
	}

	// A felled tree leaves its stump standing where it stood (ToppleUpdate::applyTopplingForce: burned if the tree was).
	void CarryOutTopples()
	{
		m_world.Resource<gameplay::ToppleEvents>().ForEach([&](const gameplay::ToppleEvent &event) {
			if (event.kind != gameplay::ToppleEvent::Kind::Started || !m_world.IsAlive(event.entity))
				return;
			const auto *definition = m_world.Get<gameplay::DefinitionRef>(event.entity);
			if (definition == nullptr)
				return;
			const auto topple = content::ReadObjectTopple(m_templates.DefinitionAt(definition->index));
			if (!topple || topple->stump.empty())
				return;
			const auto *member = m_world.Get<gameplay::TeamMember>(event.entity);
			const ecs::Entity stump = domain::SpawnObject(m_game, topple->stump, event.position.XY(), event.facing, member != nullptr ? member->team : 0u, "");
			if (m_world.Get<gameplay::Scorched>(event.entity) != nullptr && m_world.IsAlive(stump) && m_world.Get<gameplay::Scorched>(stump) == nullptr)
				m_world.Add<gameplay::Scorched>(stump);
		});
	}

	// ObjectCreationUpgrade::upgradeImplementation: each upgraded object's creation list, from where it stands.
	void ApplyCooldownCreations()
	{
		std::vector<domain::CooldownCreationEvent> events;
		m_world.Resource<domain::CooldownCreationEvents>().AppendTo(events);
		m_world.Resource<domain::CooldownCreationEvents>().Reset(0);
		for (const domain::CooldownCreationEvent &event : events)
		{
			const auto *at = m_world.IsAlive(event.entity) ? m_world.Get<gameplay::Transform>(event.entity) : nullptr;
			if (at == nullptr || event.creation == 0xFFFFFFFFu)
				continue;
			const auto *member = m_world.Get<gameplay::TeamMember>(event.entity);
			const auto *level = m_world.Get<gameplay::Experience>(event.entity);
			domain::RunCreationList(m_game, m_templates.DeathEffectName(gameplay::DeathEffectKind::Objects, event.creation),
				{at->position, at->facing, member != nullptr ? member->team : 0xFFFFFFFFu, event.entity, level != nullptr ? level->level : 0u, event.lifetimeTicks});
		}
	}

	void CarryOutUpgradeCreations()
	{
		const std::vector<domain::UpgradeCreation> creations = m_world.Resource<domain::UpgradeCreations>().list;
		for (const domain::UpgradeCreation &creation : creations)
		{
			// GenerateMinefieldBehavior::upgradeImplementation: it lays its minefield.
			if (creation.minefield)
			{
				const auto *ref = m_world.IsAlive(creation.entity) ? m_world.Get<gameplay::DefinitionRef>(creation.entity) : nullptr;
				const auto *at = ref != nullptr ? m_world.Get<gameplay::Transform>(creation.entity) : nullptr;
				if (at != nullptr)
				{
					const auto *member = m_world.Get<gameplay::TeamMember>(creation.entity);
					domain::PlaceMines(m_game, {creation.entity, ref->index, *at, member != nullptr ? member->team : 0xFFFFFFFFu}, false);
				}
				continue;
			}
			// ReplaceObjectUpgrade::upgradeImplementation: it goes (destroyObject: no death) and its replacement is made where
			// it was, on its team, as if just built (onBuildComplete, onStructureConstructionComplete: not a rebuild).
			if (creation.replacement != 0xFFFFFFFFu)
			{
				const auto *at = m_world.IsAlive(creation.entity) ? m_world.Get<gameplay::Transform>(creation.entity) : nullptr;
				const auto *member = at != nullptr ? m_world.Get<gameplay::TeamMember>(creation.entity) : nullptr;
				if (member == nullptr)
					continue;
				const gameplay::Transform where = *at;
				const std::uint32_t team = member->team;
				domain::RetireNow(m_game, {creation.entity});
				const ecs::Entity made = domain::SpawnObject(m_game, std::string(m_templates.DeathEffectName(gameplay::DeathEffectKind::Spawn, creation.replacement)),
					where.position.XY(), where.facing, team, "");
				if (!m_world.IsAlive(made))
					continue;
				m_world.Get<gameplay::Transform>(made)->position = where.position;
				domain::OnBuildComplete(m_game, made);
				domain::ScoreStructureComplete(m_game, made, false);
				domain::NoticeSuperweaponDetected(m_game, made);
				if (const auto *owner = m_world.Get<gameplay::Owner>(made))
					if (domain::AiPlayer *ai = m_aiPlayers.Of(owner->player))
						domain::OnAiStructureProduced(m_game, *ai, made);
				continue;
			}
			// GrantScienceUpgrade: its controlling player gets the science.
			if (creation.science != 0xFFFFFFFFu)
			{
				if (const auto *owner = m_world.IsAlive(creation.entity) ? m_world.Get<gameplay::Owner>(creation.entity) : nullptr)
					domain::GrantScience(m_game, owner->player, creation.science);
				continue;
			}
			const auto *transform = m_world.IsAlive(creation.entity) ? m_world.Get<gameplay::Transform>(creation.entity) : nullptr;
			if (transform == nullptr)
				continue;
			const auto *member = m_world.Get<gameplay::TeamMember>(creation.entity);
			domain::RunCreationList(m_game, m_templates.DeathEffectName(gameplay::DeathEffectKind::Objects, creation.creation),
				{transform->position, transform->facing, member != nullptr ? member->team : 0xFFFFFFFFu, creation.entity});
		}
	}

	// CreateObjectDie TransferPreviousHealth: what the dead made takes its subdual damage first (SUBDUAL_UNRESISTABLE: a
	// body that can be subdued adds it up to its cap, subdued at once when it reaches its maximum health, its helper
	// woken), then its damage (UNRESISTABLE, from its last attacker) and those attacking it (AIUpdateInterface::transferAttack).
	void TransferPreviousHealth(const gameplay::DeathEvent &event, ecs::Entity made)
	{
		if (gameplay::Subdual *subdual = m_world.Get<gameplay::Subdual>(made); subdual != nullptr && event.transferSubdual > Fixed{})
		{
			subdual->damage = std::clamp(subdual->damage + event.transferSubdual, Fixed{}, subdual->cap);
			subdual->touchTick = m_tick;
			subdual->hitTick = m_tick;
			subdual->awake = 1;
			subdual->gaining = 1;
			subdual->countdown = subdual->healTicks;
			if (const gameplay::Health *health = m_world.Get<gameplay::Health>(made); health != nullptr && health->maximum <= subdual->damage && subdual->subdued == 0)
			{
				subdual->subdued = 1;
				m_world.Resource<gameplay::DisableRequests>().list.push_back({made, gameplay::disabled_type::Subdued, 0, gameplay::DisabledForever});
			}
		}
		if (gameplay::Health *health = m_world.Get<gameplay::Health>(made); health != nullptr && event.transferDamage > Fixed{})
		{
			health->current -= event.transferDamage;
			health->lastAttacker = event.transferSource;
			health->lastDamageTick = m_tick;
			if (gameplay::IsDead(*health))
			{
				health->current = {};
				m_kills.entities.push_back(made);
			}
		}
		ecs::Query<ecs::Write<gameplay::AttackTarget>> attackers(m_world);
		attackers.ForEachChunk([&](auto chunk) {
			for (gameplay::AttackTarget &target : chunk.template Get<gameplay::AttackTarget>())
				if (target.target == event.entity)
					target.target = made;
		});
	}

	// What this tick's deaths leave behind, in their deterministic order:
	// objects from creation lists (wrecks, debris, pilots) and rubble now, and death
	// weapons going off where they died next tick.
	void CarryOutDeaths()
	{
		// Object::onDie: a victory-counting structure or an infantry or vehicle lost, not by its own hand, is EVA's.
		for (const gameplay::Casualty &casualty : m_casualties.list)
		{
			if (casualty.departure != gameplay::Departure::Killed || casualty.killer == casualty.entity || casualty.team >= m_roster.TeamCount() ||
				casualty.definition >= m_templates.DefinitionCount())
				continue;
			const content::ObjectDefinition &kind = m_templates.DefinitionAt(casualty.definition);
			const std::uint32_t owner = m_roster.TeamAt(casualty.team).owner;
			if (kind.Is("STRUCTURE") && kind.Is("MP_COUNT_FOR_VICTORY"))
				m_evaNotices.list.push_back({domain::EvaCue::BuildingLost, domain::EvaWeapon::None, owner});
			else if (kind.Is("INFANTRY") || kind.Is("VEHICLE"))
				m_evaNotices.list.push_back({domain::EvaCue::UnitLost, domain::EvaWeapon::None, owner});
		}
		for (const gameplay::DeathEvent &event : DeathEvents())
		{
			const std::string_view name = m_templates.DeathEffectName(event.kind, event.id);
			if (event.kind == gameplay::DeathEffectKind::Objects)
			{
				const ecs::Entity made = domain::RunCreationList(m_game, name, {event.position, event.facing, event.team, event.entity, event.veterancy});
				if (event.transfer && m_world.IsAlive(made))
					TransferPreviousHealth(event, made);
			}
			else if (event.kind == gameplay::DeathEffectKind::Replace)
				domain::ExposeRebuildHole(m_game, event.entity, event.definition, event.underConstruction, std::string(name), event.position, event.facing,
					event.team, m_names.Released(event.entity));
			else if (event.kind == gameplay::DeathEffectKind::Notice)
			{
				// SpecialPowerCompletionDie::notifyScriptEngine: with a creator, its player's scripts hear the power completed.
				if (event.credit != ecs::Entity{} && event.team < m_roster.TeamCount())
					m_scriptRecords.CompletedPower(m_roster.TeamAt(event.team).owner, std::string(name), event.credit);
			}
			else if (event.kind == gameplay::DeathEffectKind::Release)
				domain::RemoveObjectUpgrade(m_game, event.credit, name); // UpgradeDie::onDie: its producer's
			else if (event.kind == gameplay::DeathEffectKind::Loot)
				domain::DropCrate(m_game, name, event.killer, event.team, event.veterancy, event.position);
			else if (event.kind == gameplay::DeathEffectKind::Spawn)
				domain::SpawnObject(m_game, std::string(name), event.position.XY(), event.facing, event.team == gameplay::NoTeam ? 0u : event.team, "");
			else if (event.kind == gameplay::DeathEffectKind::Weapon)
			{
				const std::uint32_t weapon = m_templates.Weapon(std::string(name));
				if (weapon == gameplay::WeaponCatalog::None)
					continue;
				const std::uint32_t player = event.team < m_roster.TeamCount() ? m_roster.TeamAt(event.team).owner : 0u;
				m_shots.Add({event.entity, {}, weapon, player, event.position, event.position, m_tick, m_tick + 1});
			}
		}
	}

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
	gameplay::PhysicsSystem m_physics;
	gameplay::ShockWaveSystem m_shockWaves;
	gameplay::GarrisonClearSystem m_garrisonClears;
	gameplay::GarrisonKillDamageSystem m_garrisonKillDamage;
	gameplay::PassedBonusSystem m_passedBonuses;
	gameplay::SpecialPowerPauseSystem m_powerPauses;
	gameplay::RadarCoverageSystem m_radarCoverage;
	gameplay::VisionSystem m_vision;
	gameplay::HeightDieSystem m_heightDies;
	gameplay::FallingDamageSystem m_fallingDamage;
	gameplay::JetDamageSystem m_jetDamage;
	gameplay::SpatialIndexSystem m_index;
	gameplay::ObjectShroudSystem m_objectShroud;
	gameplay::TargetingSystem m_targeting;
	gameplay::AttackMoveSystem m_attackMoves;
	gameplay::BoardingSystem m_boarding;
	gameplay::UnloadingSystem m_unloading;
	gameplay::DescentSystem m_descent;
	gameplay::DeliverySystem m_delivery;
	gameplay::MovementSystem m_movement;
	gameplay::ObstacleSystem m_obstacles;
	gameplay::RouteRequestSystem m_routes;
	gameplay::TurretSystem m_turrets;
	gameplay::AltTurretSystem m_altTurrets;
	gameplay::WeaponSystem m_firing;
	gameplay::FiringTrackerSystem m_firingTracker;
	gameplay::ProjectileFlightSystem m_projectiles;
	gameplay::ProjectileLaunchSystem m_launches;
	gameplay::MissileFlightSystem m_missiles;
	gameplay::PointDefenseSystem m_pointDefense;
	domain::ProjectileBodySystem m_projectileBodies;
	gameplay::UpgradeSystem m_upgradeTriggers;
	domain::UpgradeEffectSystem m_upgradeEffects;
	domain::ResearchCompletionSystem m_researchCompletion;
	domain::ProductionRefundSystem m_productionRefunds;
	gameplay::SlavedSystem m_slaves;
	gameplay::SpawnerSystem m_spawners;
	gameplay::PoisonSystem m_poison;
	gameplay::SaleSystem m_sale;
	gameplay::ConstructionSystem m_construction;
	gameplay::WeaponBonusRetimeSystem m_bonusRetime;
	gameplay::ImpactSystem m_impacts;
	gameplay::HealthSystem m_health;
	gameplay::SubdualSystem m_subdual;
	gameplay::StatusDamageSystem m_statusDamage;
	gameplay::MissileJamSystem m_missileJam;
	gameplay::VeterancySystem m_veterancy;
	gameplay::HordeSystem m_hordes;
	gameplay::LifetimeSystem m_lifetime;
	gameplay::SelfHealingSystem m_selfHealing;
	gameplay::AreaHealingSystem m_areaHealing;
	gameplay::HealingSystem m_healing;
	gameplay::FlammabilitySystem m_flammability;
	gameplay::FireSpreadSystem m_fireSpread;
	gameplay::AutoFireSystem m_autoFire;
	gameplay::DamageReactionSystem m_damageReaction;
	gameplay::StealthSystem m_stealth;
	gameplay::DefectorSystem m_defectors;
	gameplay::ParachuteSystem m_parachutes;
	gameplay::ParachuteLandingSystem m_parachuteLandings;
	gameplay::FreeFallSystem m_freeFalls;
	gameplay::ParachuteLossSystem m_parachuteLosses;
	gameplay::PendingDamageSystem m_pendingDamage;
	domain::PilotSeekSystem m_pilotSeekers;
	domain::HealSeekSystem m_healSeekers;
	domain::SpecialAbilitySystem m_specialAbilities;
	domain::StickyBombSystem m_stickyBombs;
	domain::BridgeDamageSystem m_bridgeDamage;
	domain::InternetHackSystem m_internetHacks;
	domain::BattlePlanSystem m_battlePlans;
	domain::HijackerSystem m_hijackers;
	domain::WarehouseCripplingSystem m_warehouseCripplings;
	domain::BattleBusSystem m_battleBuses;
	domain::WeaponBonusPulseSystem m_weaponBonusPulses;
	gameplay::TempWeaponBonusSystem m_tempWeaponBonuses;
	domain::CommandButtonHuntSystem m_commandButtonHunts;
	domain::PilotKillSystem m_pilotKills;
	domain::TunnelGuardSystem m_tunnelGuards;
	domain::ScriptedEvacuationSystem m_scriptedEvacuations;
	domain::GuardSystem m_guards;
	domain::CleanupHazardSystem m_cleanupHazards;
	domain::SpyVisionSystem m_spyVisions;
	gameplay::CountermeasuresSystem m_countermeasures;
	domain::RepulsionSystem m_repulsion;
	domain::AttackSquadSystem m_attackSquads;
	// Each map player's side (its PlayerTemplate's Side), for the control bar's scheme.
	std::vector<std::string> m_playerSides;
	std::vector<std::string> m_playerTemplates;
	gameplay::LocomotorDamageSystem m_locomotorDamage;
	gameplay::WanderSystem m_wanderers;
	gameplay::MovePathSystem m_movePaths;
	gameplay::FaceTargetSystem m_faceTargets;
	gameplay::RiderRegenSystem m_riderRegen;
	gameplay::BodyCollisionSystem m_bodyCollisions;
	gameplay::CrashCollisionSystem m_crashCollisions;
	domain::TechBuildingSystem m_techBuildings;
	gameplay::HealPadSystem m_healPads;
	gameplay::AssistSystem m_assists;
	gameplay::CollideWeaponSystem m_collideWeapons;
	gameplay::StealthDetectorSystem m_stealthDetectors;
	gameplay::StealthRevealSystem m_stealthReveal;
	gameplay::GrantStealthSystem m_stealthGrants;
	gameplay::ColliderIndexSystem m_colliderIndex;
	gameplay::ContactSystem m_contacts;
	gameplay::ToppleSystem m_topple;
	gameplay::BlastWaveSystem m_blastWaves;
	gameplay::BlastDamageSystem m_blastDamage;
	gameplay::ScorchSystem m_scorch;
	gameplay::OverchargeDrainSystem m_overchargeDrain;
	gameplay::OverchargeLimitSystem m_overchargeLimit;
	gameplay::MountSystem m_mounts;
	gameplay::PropagandaScanSystem m_propagandaScan;
	gameplay::PropagandaInfluenceSystem m_propagandaInfluence;
	gameplay::MineDrainSystem m_mineDrain;
	gameplay::MinefieldSystem m_minefields;
	gameplay::DemoTrapSystem m_demoTraps;
	gameplay::EmpPulseSystem m_empPulses;
	gameplay::AutoDepositSystem m_autoDeposits;
	gameplay::AreaPresenceSystem m_areaPresence;
	gameplay::DisableExpirySystem m_disableExpiry;
	gameplay::DisableFollowSystem m_disableFollow;
	gameplay::DisableApplySystem m_disableApply;
	gameplay::CrushSystem m_crush;
	gameplay::ProductionSystem m_production;
	gameplay::EnergySystem m_energy;
	gameplay::BrownOutSystem m_brownOut;
	gameplay::JetSystem m_jets;
	gameplay::LoadoutSystem m_loadouts;
	domain::CrateTouchSystem m_crateTouches;
	gameplay::HarvestRosterSystem m_harvestRoster;
	gameplay::HarvestSystem m_harvest;
	gameplay::DockSystem m_docks;
	gameplay::RepairDockSystem m_repairDocks;
	gameplay::HiveDamageSystem m_hiveDamage;
	domain::CooldownCreationSystem m_cooldownCreations;
	domain::DeploySystem m_deploys;
	domain::LauncherDoorSystem m_launcherDoors;
	domain::ParticleCannonSystem m_particleCannons;
	domain::MobMemberSystem m_mobMembers;
	domain::SpectreGunshipSystem m_spectreGunships;
	gameplay::NeutronFlightSystem m_neutronFlights;
	domain::DroneRepairSystem m_droneRepairs;
	gameplay::CargoTransferSystem m_cargo;
	gameplay::PassengerRideSystem m_passengerRide;
	gameplay::DeathSystem m_death;
	gameplay::VictorySystem m_victory;
	gameplay::SlowDeathSystem m_slowDeath;
	gameplay::RemovalSystem m_removal;
	domain::AppearanceSystem m_appearance;
	domain::SteeringLookSystem m_steeringLooks;
	domain::PanicLookSystem m_panicLooks;
	domain::ExtensionLookSystem m_extensionLooks;
	gameplay::SnapshotSystem m_snapshot;
	ecs::SystemRegistry m_registry;
	std::size_t m_workers{1};
	std::optional<ecs::Scheduler> m_scheduler;

	engine::scripting::Vocabulary m_vocabulary;
	std::vector<scripting::CameraScriptCommand> m_cameraCommands;
	std::vector<scripting::ClientScriptCommand> m_clientCommands;
	std::optional<engine::scripting::ScriptRuntime> m_scripts;
};
}
