export module games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.aircraft.components.airfield_healing;
import games.generalszh.gameplay.containment.components.railed_transport;
import games.generalszh.gameplay.combat.components.checkpoint;
import games.generalszh.gameplay.flight_deck.components.flight_deck;
import engine.gameplay.rts.movement.components.floor_lift;
import games.generalszh.gameplay.effects.components.bone_fx;
import games.generalszh.gameplay.effects.components.radius_decal;
import games.generalszh.gameplay.railroad.components.railcar;
import engine.gameplay.common.spatial.components.dynamic_geometry;
import engine.gameplay.rts.containment.components.drop_homing;
import Engine.Core.Math.FixedRandom;
import games.generalszh.gameplay.combat.components.enemy_near;
import games.generalszh.gameplay.containment.components.assault_transport;
import engine.gameplay.rts.slaves.components.spawn_points;
import engine.gameplay.rts.vision.components.dynamic_clearing;
import engine.gameplay.rts.death.components.height_die;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import engine.gameplay.common.identity.components.object_id;
export import engine.gameplay.common.identity.components.producer;
import engine.gameplay.rts.docking.components.repair_dock;
import engine.gameplay.rts.slaves.components.hive_body;
import engine.gameplay.rts.combat.components.deploy;
import engine.gameplay.rts.movement.components.pursuit;
import engine.gameplay.rts.combat.components.sight_looker;
import engine.gameplay.rts.movement.components.attack_approach;
import engine.gameplay.rts.combat.resources.mood_ranges;
import games.generalszh.gameplay.stealth.components.supply_stealth_grant;
import games.generalszh.gameplay.combat.components.cooldown_creation;
import engine.gameplay.rts.production.components.production_exit_gate;
import engine.gameplay.common.identity.resources.object_ids;
import std;
import engine.gameplay.common.health.components.second_life;
import games.generalszh.gameplay.battleplans.algorithms.battle_plan_bonuses;
import engine.gameplay.rts.collision.components.body_collision;
import engine.gameplay.rts.blocking.components.blocking_unit;
import engine.gameplay.rts.blocking.components.blocked_state;
import engine.gameplay.rts.blocking.components.block_contact;
import engine.gameplay.rts.movement.components.wanderer;
import engine.gameplay.common.spatial.components.bounding_volume;
import games.generalszh.gameplay.teams.components.tech_building;
import engine.gameplay.rts.containment.components.heal_pad;
import games.generalszh.gameplay.containment.components.heal_seeker;
import engine.gameplay.rts.collision.components.collide_weapon;
import engine.gameplay.rts.combat.components.assisted_targeting;
import games.generalszh.gameplay.crates.components.pilot_seeker;
import games.generalszh.gameplay.containment.algorithms.parachuting;
import games.generalszh.content.objects.object_status;
import engine.gameplay.common.status.components.status_flags;
import engine.gameplay.common.appearance.components.draw_offset;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.common.areas.resources.area_activity;
export import games.generalszh.gameplay.combat.components.cleanup_hazard;
export import games.generalszh.gameplay.powers.components.spy_vision;
import games.generalszh.gameplay.production.components.cost_modifying;
import engine.gameplay.rts.combat.components.countermeasures;
import engine.gameplay.rts.combat.components.sneaky_target;
import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.rts.horde.components.horde;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.rts.navigation.components.pathfind_goal;
import engine.gameplay.rts.movement.components.move_goal;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.appearance.components.appearance;
import engine.gameplay.common.spatial.components.targetable;
import engine.gameplay.common.spatial.components.airborne_target;
import engine.gameplay.common.appearance.components.occlusion_safe;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.rts.vision.components.vision;
import engine.gameplay.common.health.components.inactive_body;
import engine.gameplay.rts.combat.components.damage_reaction;
import engine.gameplay.rts.vision.components.partition_footprint;
import engine.gameplay.common.spatial.components.object_shroud;
import games.generalszh.content.veterancy.create_modules;
import games.generalszh.content.combat.poison_content;
import engine.gameplay.rts.containment.components.garrison;
import engine.gameplay.rts.containment.components.tunnel;
import engine.gameplay.rts.containment.components.garrison_points;
import games.generalszh.content.locomotors.animation_steering;
import games.generalszh.content.slaves.slaved_content;
import engine.gameplay.rts.slaves.components.spawner;
import games.generalszh.content.upgrades.building_extensions;
import games.generalszh.gameplay.appearance.components.building_extensions;
import games.generalszh.gameplay.upgrades.algorithms.grant_upgrade;
import engine.gameplay.rts.upgrades.resources.player_upgrades;
import games.generalszh.gameplay.appearance.components.steering_look;
import games.generalszh.gameplay.veterancy.algorithms.veterancy_placement;
import engine.gameplay.rts.sciences.resources.player_sciences;
import engine.gameplay.common.health.components.health;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.common.physics.components.physics_body;
import engine.gameplay.common.spatial.components.attitude;
import engine.gameplay.common.lifetime.components.lifetime;
import engine.gameplay.rts.combat.components.auto_fire;
import engine.gameplay.rts.combat.components.turret;
import games.generalszh.content.physics.physics_content;
import games.generalszh.content.objects.model_conditions;
import games.generalszh.content.lifetime.lifetime_content;
import games.generalszh.content.healing.healing_content;
import games.generalszh.content.fire.fire_content;
import games.generalszh.content.stealth.stealth_content;
import engine.gameplay.rts.stealth.components.stealth_rider;
import games.generalszh.content.topple.topple_content;
import games.generalszh.content.production.production_content;
import engine.gameplay.rts.production.components.production_queue;
import engine.gameplay.rts.production.components.production_doors;
import engine.gameplay.rts.economy.components.energy_source;
import engine.gameplay.rts.economy.components.overcharge;
import engine.gameplay.rts.propaganda.components.propaganda;
import engine.gameplay.rts.mines.components.minefield;
import engine.gameplay.rts.mines.components.demo_trap;
import engine.gameplay.rts.emp.components.emp_pulse;
import games.generalszh.content.combat.emp_content;
import games.generalszh.content.water.wave_guide_content;
import games.generalszh.gameplay.waveguide.components.wave_guide;
import games.generalszh.gameplay.academy.algorithms.academy_records;
import games.generalszh.content.mines.mine_content;
import games.generalszh.content.economy.auto_deposit_content;
import engine.gameplay.rts.economy.components.auto_deposit;
import games.generalszh.gameplay.abilities.algorithms.ability_setup;
import games.generalszh.gameplay.abilities.components.command_button_hunt;
import engine.gameplay.common.status.components.ai_activity;
import engine.gameplay.common.areas.components.area_presence;
import games.generalszh.gameplay.mines.components.minefield_generator;
import games.generalszh.gameplay.mines.components.mine_clearer;
import games.generalszh.gameplay.construction.components.builder_boredom;
import engine.gameplay.common.health.components.health_floor;
import engine.gameplay.common.health.components.subdual;
import engine.gameplay.rts.combat.components.firing_tracker;
import engine.gameplay.common.spatial.components.body_extent;
import games.generalszh.gameplay.containment.components.initial_payload;
import games.generalszh.gameplay.containment.components.rider_change;
import games.generalszh.content.combat.weapon_bonus_content;
import engine.gameplay.rts.match.components.victory_role;
import engine.gameplay.rts.aircraft.components.airfield;
import engine.gameplay.rts.aircraft.components.jet;
import games.generalszh.content.aircraft.aircraft_content;
import engine.gameplay.rts.collision.components.collider;
import engine.gameplay.rts.collision.systems.crush_system;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.common.weapons.components.weapon_slots;
import engine.gameplay.rts.combat.components.point_defense;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.rts.movement.components.locomotor_choice;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.combat.components.aggression;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.combat.components.contained_definitions;
import engine.gameplay.rts.containment.components.cargo_size;
import games.generalszh.content.locomotors.locomotor_catalog;
import games.generalszh.content.containment.transport_content;
import games.generalszh.gameplay.containment.algorithms.put_inside;
import engine.gameplay.rts.upgrades.components.upgradable;
import engine.gameplay.rts.slaves.components.slaved;
import games.generalszh.content.slaves.slaved_content;
import games.generalszh.content.harvesting.harvest_content;
import games.generalszh.content.crates.crate_content;
import games.generalszh.gameplay.crates.resources.crates;
import engine.gameplay.rts.loadout.components.loadout;
import engine.gameplay.rts.loadout.algorithms.equip;
import games.generalszh.content.combat.loadout_content;
import engine.gameplay.rts.harvesting.components.harvester;
import engine.gameplay.rts.harvesting.components.resource_store;
import engine.gameplay.rts.docking.components.dock;
import engine.gameplay.rts.docking.components.dock_look;
import engine.gameplay.rts.docking.components.docking;
import games.generalszh.gameplay.world.algorithms.difficulty_bonuses;
import games.generalszh.gameplay.ai.components.repulsion;
import engine.gameplay.rts.death.resources.hulk_lifetime;

// Builds an entity from its object definition: what it is and where, who
// owns it, and, from the definition's modules, how it moves, what it can
// take and deal, and what it carries. The one place a Zero Hour object
// becomes components.
export namespace generalszh::gameplay
{
namespace gameplay = engine::gameplay;
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

// TeamTemplateInfo::m_initialTeamAttitude: the team's teamAggressiveness (AttitudeType), normal when unset; what its
// members' AI takes as they are made or join it (Object's constructor, Object::setTeam).
inline std::int8_t TeamAttitude(const GameWorld &game, std::uint32_t team)
{
	if (team >= game.roster.TeamCount())
		return gameplay::attitude::Normal;
	const auto value = game.teams.At(game.roster.PrototypeOf(team)).Get<std::int64_t>("teamAggressiveness");
	return value ? static_cast<std::int8_t>(*value) : gameplay::attitude::Normal;
}

// TeamPrototype::getAttackPriorityName: the attack priority set a script gave the team (updateTeamAttackPrioritySet),
// which its members' AI takes as they are made or join it (Object's constructor, Object::setTeam); 0: none.
inline std::uint16_t TeamPrioritySet(const GameWorld &game, std::uint32_t team)
{
	return team < game.roster.TeamCount() ? game.roster.TeamAt(game.roster.PrototypeOf(team)).prioritySet : std::uint16_t{0};
}

namespace detail
{
void ArmCombat(GameWorld &game, ecs::Entity entity, const content::ObjectDefinition &object, const content::ObjectCombat &combat, std::uint32_t weapon,
	const gameplay::LocomotorDefinition *locomotor);

void AddCombat(GameWorld &game, ecs::Entity entity, const content::ObjectDefinition &object, const gameplay::LocomotorDefinition *locomotor)
{
	const content::ObjectCombat combat = content::ReadObjectCombat(object, game.step);
	// Its rider shows (RiderChangeContain's initial payload: the bike with its Rebel).
	if (!combat.riderCondition.empty())
		if (const std::uint32_t bit = content::ModelConditionBit(combat.riderCondition); bit != content::NoCondition)
			if (auto *look = game.world.Get<gameplay::Appearance>(entity))
				look->Set(bit);
	// and its status (RiderChangeContain::onContaining: the rider's object status on the vehicle).
	if (const std::uint32_t status = content::ObjectStatusBit(combat.riderStatus); !combat.riderStatus.empty() && status != content::NoStatus)
	{
		game.world.Add<gameplay::StatusFlags>(entity);
		game.world.Get<gameplay::StatusFlags>(entity)->bits |= std::uint64_t{1} << status;
	}
	// InactiveBody: no health to hurt, effectively dead from the start.
	if (combat.inactiveBody)
		game.world.Add<gameplay::InactiveBody>(entity);
	if (!combat.maxHealth || *combat.maxHealth <= Fixed{})
		return;
	// Only things with a body can be found and hurt.
	std::uint32_t classes = 0;
	const std::pair<const char *, std::uint32_t> kinds[] = {{"STRUCTURE", gameplay::target_class::Structure},
		{"INFANTRY", gameplay::target_class::Infantry}, {"VEHICLE", gameplay::target_class::Vehicle}, {"AIRCRAFT", gameplay::target_class::Aircraft},
		{"PROJECTILE", gameplay::target_class::Projectile}, {"MINE", gameplay::target_class::Mine},
		{"SMALL_MISSILE", gameplay::target_class::SmallMissile}, {"BALLISTIC_MISSILE", gameplay::target_class::BallisticMissile},
		{"UNATTACKABLE", gameplay::target_class::Unattackable}, {"SHRUBBERY", gameplay::target_class::Shrubbery},
		{"BOOBY_TRAP", gameplay::target_class::Trap}, {"DEMOTRAP", gameplay::target_class::Trap}, {"HERO", gameplay::target_class::Hero},
		{"FS_BASE_DEFENSE", gameplay::target_class::BaseDefense}};
	for (const auto &[kind, bit] : kinds)
		if (object.Is(kind))
			classes |= bit;
	// Object::isNonFactionStructure.
	if (content::IsNonFactionStructure(object))
		classes |= gameplay::target_class::NonFactionStructure;
	auto &world = game.world;
	world.Add<gameplay::Targetable>(entity);
	// Its bounding circle (GeometryInfo::getBoundingCircleRadius): what the partition manager measures from.
	*world.Get<gameplay::Targetable>(entity) = {content::BoundingCircleRadius(object.geometry), classes};
	// GeometryInfo's heights: getMaxHeightAbovePosition, getBoundingSphereRadius, getZDeltaToCenterPosition.
	{
		const bool sphere = object.geometry.shape == content::GeometryShape::Sphere;
		world.Add<gameplay::BodyExtent>(entity);
		*world.Get<gameplay::BodyExtent>(entity) = {sphere ? object.geometry.majorRadius : object.geometry.height, content::BoundingSphereRadius(object.geometry),
			sphere ? Fixed{} : object.geometry.height / Fixed::FromInt(2), sphere ? object.geometry.majorRadius : Fixed{}};
	}
	world.Add<gameplay::Health>(entity);
	*world.Get<gameplay::Health>(entity) = {combat.initialHealth > Fixed{} ? combat.initialHealth : *combat.maxHealth, *combat.maxHealth,
		game.templates.Armor(combat.armor)};
	// ActiveBody::canBeSubdued: a SubdualDamageCap above 0 (its SubdualDamageHelper sheds it).
	if (combat.subdualCap > Fixed{})
	{
		world.Add<gameplay::Subdual>(entity);
		gameplay::Subdual &subdual = *world.Get<gameplay::Subdual>(entity);
		subdual.cap = combat.subdualCap;
		subdual.healAmount = combat.subdualHealAmount;
		subdual.healTicks = combat.subdualHealTicks;
	}
	// UndeadBody: its second life to come.
	if (combat.secondLifeMaxHealth)
	{
		world.Add<gameplay::SecondLife>(entity);
		world.Get<gameplay::SecondLife>(entity)->maximum = *combat.secondLifeMaxHealth;
	}
	if (combat.bodyFloor != content::ObjectCombat::BodyFloor::None)
	{
		world.Add<gameplay::HealthFloor>(entity);
		if (combat.bodyFloor == content::ObjectCombat::BodyFloor::Highlander)
			world.Get<gameplay::HealthFloor>(entity)->exempt = game.templates.weapons.unresistable;
	}
	// Poison stays in it (PoisonedBehavior).
	if (const auto poison = content::ReadObjectPoison(object, game.step))
	{
		world.Add<gameplay::Poison>(entity);
		*world.Get<gameplay::Poison>(entity) = *poison;
		world.Get<gameplay::Poison>(entity)->seenHealth = world.Get<gameplay::Health>(entity)->current;
	}
	// What can be hurt can be healed (by one area healer at a time), and may heal.
	world.Add<gameplay::HealLock>(entity);
	const content::ObjectHealing healing = content::ReadObjectHealing(object, game.step);
	// Healers start at random phases so they do not all heal on one tick (as the original); dormant (upgrade-triggered)
	// ones sleep until their upgrade, drawing nothing.
	const auto phase = [&](std::uint64_t delay) {
		return game.tick + static_cast<std::uint64_t>(Engine::Math::UniformInt(game.random, 1, static_cast<std::int64_t>(delay)));
	};
	// Each self-heal module is its own program (AutoHealBehavior's, including the veterancy heal every object inherits,
	// then BaseRegenerateUpdate's): one does not replace another.
	gameplay::SelfHealing self = healing.self;
	for (std::uint32_t index = 0; index < self.count; ++index)
		if (self.programs[index].dormant == 0)
			self.programs[index].nextTick = phase(self.programs[index].delay);
	// BaseRegenerateUpdate: once damage-free for BaseRegenDelay, every 3 frames 3 * max * BaseRegenHealthPercentPerSecond / 30
	// (from its first frame, with no random phase).
	if (game.templates.Content().gameData.baseRegenPerSecond > Fixed{} &&
		std::any_of(object.modules.begin(), object.modules.end(), [](const content::ModuleEntry &module) { return module.type == "BaseRegenerateUpdate"; }))
	{
		constexpr std::uint64_t HealRate = 3;
		const Fixed amount = Fixed::FromInt(HealRate) * world.Get<gameplay::Health>(entity)->maximum * game.templates.Content().gameData.baseRegenPerSecond /
			Fixed::FromInt(static_cast<std::int64_t>(game.step.TicksPerSecond()));
		// It regenerates while underpowered (BaseRegenerateUpdate::getDisabledTypesToProcess), not while held.
		self.Add({amount, HealRate, game.templates.Content().gameData.baseRegenDelayTicks, game.tick, gameplay::disabled_type::Underpowered, 1});
	}
	if (self.count > 0)
	{
		world.Add<gameplay::SelfHealing>(entity);
		*world.Get<gameplay::SelfHealing>(entity) = self;
	}
	if (healing.area.count > 0)
	{
		world.Add<gameplay::AreaHealing>(entity);
		gameplay::AreaHealing &area = *world.Get<gameplay::AreaHealing>(entity);
		area = healing.area;
		for (std::uint32_t index = 0; index < area.count; ++index)
			if ((area.programs[index].flags & gameplay::area_healing::Dormant) == 0)
				area.programs[index].nextTick = phase(area.programs[index].delay);
	}
	if (const auto fire = content::ReadObjectFire(object, game.step))
	{
		world.Add<gameplay::Flammable>(entity);
		*world.Get<gameplay::Flammable>(entity) = fire->flammable;
		// Burning trees and the like set their neighbours alight.
		if (const auto spread = content::ReadObjectFireSpread(object, game.step))
		{
			world.Add<gameplay::FireSpread>(entity);
			*world.Get<gameplay::FireSpread>(entity) = spread->spread;
		}
	}
	// What can be hurt can die, by its die modules.

	// FireWeaponWhenDamagedBehavior: its weapons by damage state, each ready at once (reloadAmmo).
	if (const auto reacting = content::ReadDamageReaction(object))
	{
		gameplay::DamageReaction reaction;
		const auto weaponOf = [&](const std::string &name) {
			const std::uint32_t found = name.empty() ? gameplay::WeaponCatalog::None : game.templates.Weapon(name);
			return found == gameplay::WeaponCatalog::None ? gameplay::ReactionWeapon::None : found;
		};
		for (std::size_t state = 0; state < 4; ++state)
		{
			reaction.reaction[state].weapon = weaponOf(reacting->reaction[state]);
			reaction.continuous[state].weapon = weaponOf(reacting->continuous[state]);
		}
		reaction.damageTypes = reacting->damageTypes;
		reaction.threshold = reacting->threshold;
		reaction.damaged = game.templates.Content().gameData.unitDamaged;
		reaction.reallyDamaged = game.templates.Content().gameData.unitReallyDamaged;
		reaction.active = reacting->startsActive ? 1 : 0;
		world.Add<gameplay::DamageReaction>(entity);
		*world.Get<gameplay::DamageReaction>(entity) = reaction;
	}
	const std::uint32_t weapon = game.templates.Weapon(combat.primaryWeapon);
	if (weapon == gameplay::WeaponCatalog::None)
		return;
	ArmCombat(game, entity, object, combat, weapon, locomotor);
}

// An armed object's combat: its weapon (PRIMARY `weapon`, the others its combat's), turrets, point defense and aggression.
void ArmCombat(GameWorld &game, ecs::Entity entity, const content::ObjectDefinition &object, const content::ObjectCombat &combat, std::uint32_t weapon,
	const gameplay::LocomotorDefinition *locomotor)
{
	auto &world = game.world;
	world.Add<gameplay::Armament>(entity);
	// Object::m_firingTracker: every armed object tracks its shots (continuous fire, looping fire sounds).
	world.Add<gameplay::FiringTracker>(entity);
	// Its weapon bonus conditions (Object::m_weaponBonusCondition): none yet.
	if (!world.Has<gameplay::WeaponBonusConditions>(entity))
		world.Add<gameplay::WeaponBonusConditions>(entity);
	world.Add<gameplay::AttackTarget>(entity);
	world.Add<gameplay::Aggression>(entity);
	const auto &definition = game.templates.weapons.At(weapon);
	*world.Get<gameplay::Armament>(entity) = {.weapon = weapon, .clip = definition.clipSize, .turnRate = locomotor != nullptr ? locomotor->turnRate : Engine::Math::TurnFromDegrees(6), .turret = combat.turret || locomotor == nullptr};
	if (const auto barrels = game.templates.Content().barrels.find(object.name); barrels != game.templates.Content().barrels.end())
		world.Get<gameplay::Armament>(entity)->barrels = static_cast<std::uint8_t>(std::min<std::uint32_t>(barrels->second, 255));
	if (combat.turret)
	{
		world.Add<gameplay::Turret>(entity);
		gameplay::Turret &turret = *world.Get<gameplay::Turret>(entity);
		turret.definition = combat.turretDefinition;
		turret.angle = turret.definition.naturalAngle;
		turret.pitch = turret.definition.naturalPitch;
		turret.enabled = !turret.definition.initiallyDisabled;
		// AttackStateMachine's CHASE_TARGET (AIAttackPursueTargetState): a mobile turret attacker may chase; it sets out to
		// crush infantry on its own unless KINDOF_DONT_AUTO_CRUSH_INFANTRY.
		if (locomotor != nullptr)
		{
			world.Add<gameplay::Pursuit>(entity);
			world.Get<gameplay::Pursuit>(entity)->autoCrush = object.Is("DONT_AUTO_CRUSH_INFANTRY") ? 0 : 1;
		}
	}
	// AttackUsesLineOfSight: a KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT attacker's mood look asks for CAN_SEE.
	if (game.templates.Content().aiData.attackUsesLineOfSight && object.Is("ATTACK_NEEDS_LINE_OF_SIGHT"))
	{
		world.Add<gameplay::SightLooker>(entity);
		world.Get<gameplay::SightLooker>(entity)->immobile = object.Is("IMMOBILE") ? 1 : 0;
	}
	// requestAttackPath: a mobile attacker's approach (AIAttackApproachTargetState), on the ground searching for a spot it may
	// fire from (findAttackPath).
	if (locomotor != nullptr)
		world.Add<gameplay::AttackApproach>(entity);
	// A weapon set beyond PRIMARY: each slot fires on its own, aimed by the turret that controls it.
	if (!combat.slotWeapons[1].empty() || !combat.slotWeapons[2].empty())
	{
		world.Add<gameplay::WeaponSlots>(entity);
		gameplay::WeaponSlots &set = *world.Get<gameplay::WeaponSlots>(entity);
		for (std::size_t index = 0; index < gameplay::WeaponSlotCount; ++index)
		{
			gameplay::WeaponSlot &slot = set.slots[index];
			slot.weapon = index == 0 ? weapon : game.templates.Weapon(combat.slotWeapons[index]);
			if (slot.weapon != gameplay::WeaponCatalog::None)
				slot.clip = game.templates.weapons.At(slot.weapon).clipSize;
			const std::uint8_t bit = static_cast<std::uint8_t>(1u << index);
			slot.aim = combat.altTurret && (combat.altTurretSlots & bit) != 0 ? gameplay::SlotAim::AltTurret
				: combat.turret && (combat.turretSlots & bit) != 0 ? gameplay::SlotAim::Turret : gameplay::SlotAim::Body;
			slot.sources = combat.slotRules.sources[index];
			slot.preferred = combat.slotRules.preferred[index];
		}
		set.slots[0].barrels = world.Get<gameplay::Armament>(entity)->barrels;
		set.sharedReload = combat.slotRules.sharedReload ? 1 : 0;
		// The other slots' barrels from their own bones (validateWeaponBarrelInfo per slot).
		if (const auto layout = game.templates.Content().launchLayouts.find(object.name); layout != game.templates.Content().launchLayouts.end())
			for (std::size_t index = 1; index < gameplay::WeaponSlotCount; ++index)
				set.slots[index].barrels = static_cast<std::uint8_t>(std::clamp<std::size_t>(layout->second.barrels[index].size(), 1, 255));
	}
	if (combat.altTurret)
	{
		world.Add<gameplay::AltTurret>(entity);
		gameplay::AltTurret &alt = *world.Get<gameplay::AltTurret>(entity);
		alt.turret.definition = combat.altTurretDefinition;
		alt.turret.angle = alt.turret.definition.naturalAngle;
		alt.turret.pitch = alt.turret.definition.naturalPitch;
		alt.turret.enabled = !alt.turret.definition.initiallyDisabled;
		alt.linked = combat.turretsLinked;
	}
	// A point defense laser shooting down incoming missiles (PointDefenseLaserUpdate).
	if (!combat.pointDefenseWeapon.empty())
		if (const std::uint32_t laser = game.templates.Weapon(combat.pointDefenseWeapon); laser != gameplay::WeaponCatalog::None)
		{
			world.Add<gameplay::PointDefense>(entity);
			*world.Get<gameplay::PointDefense>(entity) = {{laser, combat.pointDefensePrimary, combat.pointDefenseSecondary, combat.pointDefenseScanTicks,
				combat.pointDefenseRange}};
		}
	gameplay::Aggression aggression;
	aggression.autoAcquire = combat.autoAcquire;
	aggression.notWhileAttacking = combat.acquireNotWhileAttacking;
	// canAutoAcquireWhileStealthed: AutoAcquireEnemiesWhenIdle Stealthed, or stealth a special power grants (GrantedBySpecialPower).
	const auto stealthRules = content::ReadObjectStealth(object, game.step);
	aggression.acquireStealthed = combat.acquireStealthed ||
		(stealthRules && stealthRules->stealth.Option(gameplay::stealth_option::GrantedBySpecialPower));
	aggression.attackBuildings = combat.attackBuildings;
	aggression.scanInterval = combat.scanInterval;
	// AIUpdateInterface::onObjectCreated: its AI starts idle (AIIdleState::onEnter, resetNextMoodCheckTime): its first
	// look ForceIdleMSEC on, the one after randomly offset.
	aggression.nextScan = game.tick + world.Resource<gameplay::MoodRanges>().forceIdleTicks;
	aggression.moodFlags = gameplay::mood_flag::SeenIdle | gameplay::mood_flag::OffsetNext;
	aggression.scanRange = std::max(object.visionRange, definition.attackRange);
	aggression.vision = object.visionRange;
	aggression.attitude = TeamAttitude(game, world.Get<gameplay::TeamMember>(entity)->team);
	aggression.prioritySet = TeamPrioritySet(game, world.Get<gameplay::TeamMember>(entity)->team);
	aggression.guardRadius = aggression.scanRange;
	aggression.guardCenter = world.Get<gameplay::Transform>(entity)->position.XY();
	*world.Get<gameplay::Aggression>(entity) = aggression;
}
}

// WeaponSet::updateWeaponSet on an object made without weapons (the set it began with empty): once its flags pick a set
// with a PRIMARY weapon, it is armed as the factory arms an object (detail::ArmCombat), with that set's weapons.
void ArmFromLoadout(GameWorld &game, ecs::Entity entity)
{
	auto &world = game.world;
	if (!world.IsAlive(entity) || world.Has<gameplay::Armament>(entity))
		return;
	auto *loadout = world.Get<gameplay::Loadout>(entity);
	const auto *ref = world.Get<gameplay::DefinitionRef>(entity);
	const auto *sets = loadout != nullptr && ref != nullptr ? game.templates.loadouts.Of(ref->index) : nullptr;
	if (sets == nullptr || sets->weaponSets.empty())
		return;
	const std::uint16_t best = gameplay::BestSet(sets->weaponSets, loadout->weaponFlags);
	const auto &weapons = sets->weaponSets[best].weapons;
	if (weapons[0] == gameplay::WeaponCatalog::None)
		return;
	const content::ObjectDefinition &object = game.templates.DefinitionAt(ref->index);
	content::ObjectCombat combat = content::ReadObjectCombat(object, game.step);
	combat.slotWeapons = {};
	const auto *motion = world.Get<gameplay::Locomotion>(entity);
	detail::ArmCombat(game, entity, object, combat, weapons[0], motion != nullptr ? &motion->locomotor : nullptr);
	if (auto added = gameplay::EquipWeapons(*world.Get<gameplay::Armament>(entity), world.Get<gameplay::WeaponSlots>(entity), weapons, game.templates.weapons,
			sets->weaponSets[best].lockShared, sets->weaponSets[best].rules))
	{
		world.Add<gameplay::WeaponSlots>(entity);
		*world.Get<gameplay::WeaponSlots>(entity) = *added;
	}
	loadout->weaponSet = best;
}

// The tick's LoadoutArmings: what was without weapons and now has a set to arm it with (a dozer taking up its
// MINE_CLEARING_DETAIL set), armed as ArmFromLoadout.
inline void ApplyLoadoutArmings(GameWorld &game)
{
	auto *armings = game.world.FindResource<gameplay::LoadoutArmings>();
	if (armings == nullptr)
		return;
	std::vector<ecs::Entity> entities;
	armings->AppendTo(entities);
	armings->Reset(0);
	for (const ecs::Entity entity : entities)
		ArmFromLoadout(game, entity);
}

// Object::setProducer: what made `made` (none: nothing).
inline void SetProducer(GameWorld &game, ecs::Entity made, ecs::Entity producer)
{
	if (!game.world.IsAlive(made))
		return;
	if (!game.world.Has<engine::gameplay::Producer>(made))
		game.world.Add<engine::gameplay::Producer>(made);
	game.world.Get<engine::gameplay::Producer>(made)->entity = producer;
}

// Creates `type` (or one of its build variations, chosen at random as the
// original) on `team`, named `name` for scripts. Invalid when unknown.
namespace object_factory_detail
{
// GrantUpgradeCreate: a player upgrade for its player (addUpgrade complete), else the object's own (giveUpgrade).
inline void GrantUpgrade(GameWorld &game, ecs::Entity entity, const std::string &name)
{
	const content::GameContent &content = game.templates.Content();
	if (const auto upgrade = content.upgrades.Find(name))
	{
		if (content.upgrades.upgrades[*upgrade].player)
			game.world.Resource<engine::gameplay::PlayerUpgrades>().Grant(game.world.Get<engine::gameplay::Owner>(entity)->player, *upgrade);
		else
			GiveObjectUpgrade(game, entity, name);
		// The player's academy records it (recordUpgrade, granted).
		if (const auto *owner = game.world.Get<engine::gameplay::Owner>(entity))
			RecordAcademyUpgrade(game, owner->player, static_cast<std::uint32_t>(*upgrade), true);
	}
}
}

ecs::Entity SpawnObject(GameWorld &game, const std::string &type, FixedVector2 position, Engine::Math::TurnAngle facing, std::uint32_t team,
	const std::string &name, bool completesTeam = true, bool underConstruction = false)
{
	const content::GameContent &content = game.templates.Content();
	const content::ObjectDefinition *object = content.objects.Find(type);
	if (object != nullptr && !object->buildVariations.empty())
	{
		const auto pick = Engine::Math::UniformInt(game.random, 0, static_cast<std::int64_t>(object->buildVariations.size()) - 1);
		object = content.objects.Find(object->buildVariations[static_cast<std::size_t>(pick)]);
	}
	if (object == nullptr)
		return {};
	auto &world = game.world;
	const ecs::Entity entity = world.Create();
	world.Add<gameplay::Transform>(entity);
	world.Add<gameplay::DefinitionRef>(entity);
	// setTriggerAreaFlagsForChangeInPosition: projectiles and inert things never trigger areas; anything else made changes
	// what the areas hold (updateObjectsChangedTriggerAreas).
	if (!object->Is("PROJECTILE") && !object->Is("INERT"))
	{
		world.Add<gameplay::AreaPresence>(entity);
		if (auto *activity = world.FindResource<gameplay::AreaActivity>())
			activity->lastChange = game.tick;
	}
	world.Add<gameplay::TeamMember>(entity);
	world.Add<gameplay::Owner>(entity);
	world.Add<gameplay::Appearance>(entity);
	*world.Get<gameplay::DefinitionRef>(entity) = {game.templates.Definition(*object)};
	*world.Get<gameplay::TeamMember>(entity) = {team};
	*world.Get<gameplay::Owner>(entity) = {game.roster.TeamAt(team).owner};
	// Object::initObject: a MINE, BOOBY_TRAP or DEMOTRAP made counts on the neutral player's academy (recordMine).
	if (object->Is("MINE") || object->Is("BOOBY_TRAP") || object->Is("DEMOTRAP"))
		if (const auto neutral = NeutralPlayer(game))
			RecordAcademy(game, *neutral, AcademyCount::Mine);
	// What it sees of the shroud (ShroudClearingRange, ShroudRevealToAllRange, KINDOF_REVEAL_TO_ALL; its footprint while
	// under construction).
	world.Add<gameplay::Vision>(entity);
	{
		gameplay::Vision &vision = *world.Get<gameplay::Vision>(entity);
		vision.clearingRange = object->ClearingRange();
		vision.revealToAllRange = object->shroudRevealToAllRange;
		vision.footprintRange = content::BoundingCircleRadius(object->geometry);
		vision.revealToAll = object->Is("REVEAL_TO_ALL") ? 1u : 0u;
	}
	// EnemyNearUpdate: its first look a random 0 to ScanDelayTime ticks away (GameLogicRandomValue).
	if (const std::uint32_t *scan = game.templates.EnemyNearOf(game.templates.Definition(*object)))
	{
		world.Add<EnemyNear>(entity);
		world.Get<EnemyNear>(entity)->scanDelay = static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, static_cast<std::int64_t>(*scan)));
	}
	// CheckpointUpdate's constructor: its gate as wide as its geometry's minor radius.
	if (game.templates.CheckpointOf(game.templates.Definition(*object)))
	{
		world.Add<Checkpoint>(entity);
		auto &gate = *world.Get<Checkpoint>(entity);
		gate.minorRadius = gate.maxMinorRadius = object->geometry.minorRadius;
	}
	// SmartBombTargetHomingUpdate: told its spot as it is dropped, it steers on to it (CourseCorrectionScalar).
	for (const content::ModuleEntry &module : object->modules)
		if (module.block != nullptr && module.type == "SmartBombTargetHomingUpdate")
		{
			world.Add<gameplay::DropHoming>(entity);
			// (0.99 unless given.)
			world.Get<gameplay::DropHoming>(entity)->keep = Engine::Math::Fixed::FromRatio(99, 100);
			if (const auto *scalar = module.block->Find("CourseCorrectionScalar"))
				world.Get<gameplay::DropHoming>(entity)->keep = engine::config::values::ParseFixed(scalar->Value()).value_or(Engine::Math::Fixed::FromRatio(99, 100));
			break;
		}
	// DynamicGeometryInfoUpdate (or a firestorm's): its size to change from InitialDelay (at least a tick) over TransitionTime
	// (parseDurationUnsignedInt: milliseconds, rounded up), from its initial to its final height and radii.
	for (const content::ModuleEntry &module : object->modules)
		if (module.block != nullptr && (module.type == "DynamicGeometryInfoUpdate" || module.type == "FirestormDynamicGeometryInfoUpdate"))
		{
			engine::config::Diagnostics diagnostics;
			engine::config::BindContext bind{diagnostics, game.step};
			const auto fixed = [&](std::string_view key) {
				const auto *node = module.block->Find(key);
				return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{}) : Engine::Math::Fixed{};
			};
			const auto ticks = [&](std::string_view key, std::uint64_t fallback) {
				const auto *node = module.block->Find(key);
				return node != nullptr ? engine::config::ReadDurationTicks(*node, bind).value_or(fallback) : fallback;
			};
			gameplay::DynamicGeometry geometry;
			geometry.delayLeft = static_cast<std::uint32_t>(std::max<std::uint64_t>(ticks("InitialDelay", 0), 1));
			geometry.transitionTime = static_cast<std::uint32_t>(ticks("TransitionTime", 1));
			if (const auto *node = module.block->Find("ReverseAtTransitionTime"))
				geometry.reverse = engine::config::values::ParseBool(node->Value()).value_or(false) ? 1 : 0;
			geometry.shape = object->geometry.shape == content::GeometryShape::Sphere ? gameplay::geometry_shape::Sphere
				: object->geometry.shape == content::GeometryShape::Box ? gameplay::geometry_shape::Box : gameplay::geometry_shape::Cylinder;
			geometry.initialHeight = fixed("InitialHeight");
			geometry.initialMajor = fixed("InitialMajorRadius");
			geometry.initialMinor = fixed("InitialMinorRadius");
			geometry.finalHeight = fixed("FinalHeight");
			geometry.finalMajor = fixed("FinalMajorRadius");
			geometry.finalMinor = fixed("FinalMinorRadius");
			geometry.height = object->geometry.height;
			geometry.major = object->geometry.majorRadius;
			geometry.minor = object->geometry.minorRadius;
			world.Add<gameplay::DynamicGeometry>(entity);
			*world.Get<gameplay::DynamicGeometry>(entity) = geometry;
			if (module.type == "FirestormDynamicGeometryInfoUpdate")
				world.Add<Firestorm>(entity);
			break;
		}
	// LeafletDropBehavior: its leaflets to start on its first update.
	if (game.templates.LeafletDropOf(game.templates.Definition(*object)) != nullptr)
		world.Add<LeafletDrop>(entity);
	// AssaultTransportAIUpdate: its riders fight around it (MembersGetHealedAtLifeRatio, 0 unless given).
	for (const content::ModuleEntry &module : object->modules)
		if (module.block != nullptr && module.type == "AssaultTransportAIUpdate")
		{
			world.Add<AssaultTransport>(entity);
			AssaultTransport &assault = *world.Get<AssaultTransport>(entity);
			assault.healAtLifeRatio = {};
			if (const auto *ratio = module.block->Find("MembersGetHealedAtLifeRatio"))
				assault.healAtLifeRatio = engine::config::values::ParseFixed(ratio->Value()).value_or(Engine::Math::Fixed{});
			break;
		}
	// SpawnPointProductionExitUpdate: its places (its SpawnPoint bones, at most MAX_SPAWN_POINTS).
	if (const auto *places = game.templates.SpawnPointsOf(game.templates.Definition(*object)))
	{
		world.Add<gameplay::SpawnPoints>(entity);
		world.Get<gameplay::SpawnPoints>(entity)->count = static_cast<std::uint32_t>(std::min<std::size_t>(places->size(), gameplay::SpawnPoints::Capacity));
	}
	// DynamicShroudClearingRangeUpdate: its clearing range to swell and fade from the one it was made with.
	if (const auto *clearings = world.FindResource<gameplay::DynamicClearingCatalog>())
		if (const auto *how = clearings->Of(game.templates.Definition(*object)))
		{
			world.Add<gameplay::DynamicClearing>(entity);
			*world.Get<gameplay::DynamicClearing>(entity) = gameplay::StartDynamicClearing(*how, object->ClearingRange(), game.tick);
		}
	// The partition cells it touches (its geometry), and how each player sees it through the shroud (ALWAYS_VISIBLE: clear).
	world.Add<gameplay::PartitionFootprint>(entity);
	{
		gameplay::PartitionFootprint &footprint = *world.Get<gameplay::PartitionFootprint>(entity);
		const bool box = object->geometry.shape == content::GeometryShape::Box;
		footprint.box = box ? 1u : 0u;
		footprint.major = object->geometry.majorRadius;
		footprint.minor = box ? object->geometry.minorRadius : object->geometry.majorRadius;
		footprint.smallGeometry = object->geometry.small ? 1u : 0u;
		footprint.alwaysVisible = object->Is("ALWAYS_VISIBLE") ? 1u : 0u;
	}
	world.Add<gameplay::ObjectShroud>(entity);
	if (auto *ids = world.FindResource<gameplay::ObjectIds>())
	{
		world.Add<gameplay::ObjectId>(entity);
		*world.Get<gameplay::ObjectId>(entity) = {ids->Allocate()};
	}
	// Its experience (every object has an ExperienceTracker): regular, trainable as its template says.
	world.Add<gameplay::Experience>(entity);
	world.Get<gameplay::Experience>(entity)->trainable = object->trainable;
	// PropagandaTowerBehavior: its ENTHUSIASTIC (and with UpgradeRequired, SUBLIMINAL) rousing and healing; it stops
	// for the neutral player (the one with no name).
	if (const auto propaganda = content::ReadPropagandaTower(*object, game.step))
	{
		gameplay::PropagandaTower tower;
		tower.radius = propaganda->radius;
		tower.delay = propaganda->delay;
		tower.heal = propaganda->heal;
		tower.upgradedHeal = propaganda->upgradedHeal;
		tower.affectsSelf = propaganda->affectsSelf ? 1 : 0;
		tower.bonus = content::weapon_bonus::Enthusiastic;
		tower.upgradedBonus = content::weapon_bonus::Subliminal;
		if (!propaganda->upgrade.empty())
			if (const auto upgrade = game.templates.Content().upgrades.Find(propaganda->upgrade))
				tower.upgrade = static_cast<std::uint32_t>(*upgrade);
		if (!propaganda->pulseFX.empty())
			tower.pulseEffect = game.templates.PlayedEffect(gameplay::DeathEffectKind::Effect, propaganda->pulseFX);
		if (!propaganda->upgradedPulseFX.empty())
			tower.upgradedPulseEffect = game.templates.PlayedEffect(gameplay::DeathEffectKind::Effect, propaganda->upgradedPulseFX);
		for (std::uint32_t player = 0; player < game.roster.PlayerCount(); ++player)
			if (game.roster.PlayerAt(player).name.empty())
				tower.neutralPlayer = player;
		world.Add<gameplay::PropagandaTower>(entity);
		*world.Get<gameplay::PropagandaTower>(entity) = tower;
	}
	// MinefieldBehavior: its virtual mines, all live; a regenerating mine keeps MIN_HEALTH (its onDamage floor); its
	// producer is checked from its first update (m_nextDeathCheckFrame 0).
	if (const auto mine = content::ReadMinefield(*object, game.step))
	{
		gameplay::Minefield data;
		data.weapon = mine->weapon.empty() ? gameplay::Minefield::NoWeapon : game.templates.Weapon(mine->weapon);
		data.total = data.remaining = mine->virtualMines;
		data.detonatedBy = mine->detonatedBy;
		data.workersDetonate = mine->workersDetonate ? 1 : 0;
		data.regenerates = mine->regenerates ? 1 : 0;
		data.stopsRegen = mine->stopsRegen ? 1 : 0;
		data.checkRate = mine->checkRate;
		data.nextCheck = game.tick;
		data.drain = mine->drainPercent;
		data.repeatThreshold = mine->repeatThreshold;
		data.radius = content::BoundingCircleRadius(object->geometry);
		if (const auto *body = world.Get<gameplay::Health>(entity))
			data.lastHealth = body->current;
		world.Add<gameplay::Minefield>(entity);
		*world.Get<gameplay::Minefield>(entity) = data;
		// MinefieldBehavior's constructor: OBJECT_STATUS_NO_ATTACK_FROM_AI.
		if (auto *target = world.Get<gameplay::Targetable>(entity))
			target->classes |= gameplay::target_class::NoAttackFromAi;
		if (mine->regenerates)
		{
			if (!world.Has<gameplay::HealthFloor>(entity))
				world.Add<gameplay::HealthFloor>(entity);
			*world.Get<gameplay::HealthFloor>(entity) = gameplay::HealthFloor{gameplay::HealthFloor::NoExemptType, 0, Fixed::FromRatio(1, 10)};
		}
	}
	// DemoTrapUpdate::onObjectCreated: locked to its default mode's slot (proximity or manual).
	if (const auto trap = content::ReadDemoTrap(*object, game.step))
	{
		gameplay::DemoTrap data;
		data.range = trap->range;
		data.scanTicks = trap->scanTicks;
		data.detonationSlot = trap->detonationSlot;
		data.proximitySlot = trap->proximitySlot;
		data.manualSlot = trap->manualSlot;
		data.friendlyDetonation = trap->friendlyDetonation ? 1 : 0;
		data.detonateWhenKilled = trap->detonateWhenKilled ? 1 : 0;
		data.defaultSlot = trap->proximityByDefault ? trap->proximitySlot : trap->manualSlot;
		data.weapon = trap->weapon.empty() ? gameplay::DemoTrap::NoWeapon : game.templates.Weapon(trap->weapon);
		for (const std::string &kind : trap->ignoreKinds)
			data.ignoreClasses |= kind == "PROJECTILE" ? gameplay::target_class::Projectile : kind == "UNATTACKABLE" ? gameplay::target_class::Unattackable
				: kind == "INFANTRY" ? gameplay::target_class::Infantry : kind == "VEHICLE" ? gameplay::target_class::Vehicle
				: kind == "AIRCRAFT" ? gameplay::target_class::Aircraft : kind == "STRUCTURE" ? gameplay::target_class::Structure
				: kind == "MINE" ? gameplay::target_class::Mine : 0u;
		world.Add<gameplay::DemoTrap>(entity);
		*world.Get<gameplay::DemoTrap>(entity) = data;
		if (auto *slots = world.Get<gameplay::WeaponSlots>(entity))
			slots->locked = trap->proximityByDefault ? trap->proximitySlot : trap->manualSlot;
	}
	// AutoDepositUpdate: its first payday a period from now, its player seen (becomingTeamMember on creation: no bonus).
	if (const auto deposit = content::ReadAutoDeposit(*object, game.step))
	{
		gameplay::AutoDeposit made;
		made.period = deposit->periodTicks;
		made.nextTick = game.tick + deposit->periodTicks;
		made.amount = deposit->amount;
		made.captureBonus = deposit->captureBonus;
		made.actualMoney = deposit->actualMoney ? 1 : 0;
		if (!deposit->boosts.empty())
			if (const auto upgrade = content.upgrades.Find(deposit->boosts.front().upgrade))
			{
				made.boostUpgrade = static_cast<std::uint32_t>(*upgrade);
				made.boost = deposit->boosts.front().amount;
			}
		if (const auto *owner = world.Get<gameplay::Owner>(entity))
			made.player = owner->player;
		world.Add<gameplay::AutoDeposit>(entity);
		*world.Get<gameplay::AutoDeposit>(entity) = made;
	}
	// HackInternetAIUpdate: idle till told to hack.
	if (game.templates.InternetHackOf(game.templates.Definition(*object)) != nullptr)
	{
		world.Add<InternetHack>(entity);
		if (!world.Has<gameplay::AiActivity>(entity))
			world.Add<gameplay::AiActivity>(entity);
	}
	// SpecialAbilityUpdate modules: packed up and idle, its AI not yet busy with any.
	if (const auto abilities = content::ReadSpecialAbilities(*object, game.step); !abilities.empty())
	{
		world.Add<SpecialAbilities>(entity);
		*world.Get<SpecialAbilities>(entity) = MakeSpecialAbilities(content, abilities);
		if (!world.Has<gameplay::AiActivity>(entity))
			world.Add<gameplay::AiActivity>(entity);
	}
	// CommandButtonHuntUpdate: asleep until a script sets its button.
	if (const auto hunt = content::ReadCommandButtonHunt(*object, game.step))
	{
		CommandButtonHunt made;
		made.scanTicks = hunt->scanTicks;
		made.scanRange = hunt->scanRange;
		world.Add<CommandButtonHunt>(entity);
		*world.Get<CommandButtonHunt>(entity) = made;
		if (!world.Has<gameplay::AiActivity>(entity))
			world.Add<gameplay::AiActivity>(entity);
	}
	// EMPUpdate: its pulse on StartFadeTime and its end on Lifetime, from now.
	if (const auto pulse = content::ReadEmpPulse(*object, game.step))
	{
		world.Add<gameplay::EmpPulse>(entity);
		*world.Get<gameplay::EmpPulse>(entity) = gameplay::EmpPulse{game.tick + pulse->fadeTicks, game.tick + pulse->lifeTicks, pulse->disabledTicks, pulse->radius, Engine::Math::Fixed::One(), {},
			static_cast<std::uint8_t>(pulse->sparesAllies ? 1 : 0), static_cast<std::uint8_t>(pulse->sparesOwnBuildings ? 1 : 0)};
		// EMPUpdate::EMPUpdate: the size it grows toward, then a random facing (a creation list then turns it its way).
		world.Get<gameplay::EmpPulse>(entity)->targetScale = Engine::Math::UniformFixed(game.random, pulse->targetScaleMin, pulse->targetScaleMax);
		facing = Engine::Math::TurnAngle{static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, 0xFFFFFFFFll))};
	}
	// WaveGuideUpdate: a flood wave, its module data kept with its state (it disables itself on its first update).
	if (const auto guide = content::ReadWaveGuide(*object, game.step))
	{
		world.Add<WaveGuide>(entity);
		WaveGuide &made = *world.Get<WaveGuide>(entity);
		made.delay = guide->delayFrames;
		made.ySize = guide->ySize;
		made.spacing = guide->linearWaveSpacing;
		made.bend = guide->waveBendMagnitude;
		made.preferredHeight = guide->preferredHeight;
		made.shoreline = guide->shorelineEffectDistance;
		made.damageRadius = guide->damageRadius;
		made.damageAmount = guide->damageAmount;
		made.toppleForce = guide->toppleForce;
		made.splashFrequency = guide->randomSplashSoundFrequency;
	}
	// What an EMP pulse asks of its victims: EMP_HARDENED, SPAWNS_ARE_THE_WEAPONS, a faction structure (KINDOFMASK_FS:
	// the FS_ kinds but FS_POWER).
	{
		std::uint32_t traits = 0;
		if (object->Is("EMP_HARDENED"))
			traits |= gameplay::emp_trait::Hardened;
		if (object->Is("SPAWNS_ARE_THE_WEAPONS"))
			traits |= gameplay::emp_trait::SpawnsAreWeapons;
		for (const char *kind : {"FS_FACTORY", "FS_BASE_DEFENSE", "FS_TECHNOLOGY", "FS_SUPPLY_DROPZONE", "FS_SUPERWEAPON", "FS_BLACK_MARKET",
				 "FS_SUPPLY_CENTER", "FS_STRATEGY_CENTER", "FS_FAKE", "FS_INTERNET_CENTER", "FS_ADVANCED_TECH", "FS_BARRACKS", "FS_WARFACTORY",
				 "FS_AIRFIELD"})
			if (object->Is(kind))
				traits |= gameplay::emp_trait::FactionStructure;
		if (traits != 0)
		{
			world.Add<gameplay::EmpTraits>(entity);
			world.Get<gameplay::EmpTraits>(entity)->flags = traits;
		}
	}
	// GenerateMinefieldBehavior: its minefield, not yet laid.
	if (content::ReadMinefieldGenerator(*object, game.templates.Content().gameData))
		world.Add<MinefieldGenerator>(entity);
	// DozerAIUpdate, WorkerAIUpdate: it clears mines (MINE_CLEARING_DETAIL) when not at work.
	for (const content::ModuleEntry &module : object->modules)
		if (module.block != nullptr && (module.type == "DozerAIUpdate" || module.type == "WorkerAIUpdate"))
		{
			world.Add<MineClearer>(entity);
			// DozerPrimaryIdleState: BoredTime (ms, INI::parseDurationUnsignedInt: frames rounded up) and BoredRange, its idle
			// stretch starting now (the state entered as it is made).
			BuilderBoredom boredom{game.tick};
			if (const auto *time = module.block->Find("BoredTime"))
				if (const auto ms = engine::config::values::ParseFixed(time->Value()))
					boredom.boredTicks = static_cast<std::uint64_t>(std::max<std::int64_t>(
						(*ms * Fixed::FromInt(static_cast<std::int64_t>(game.step.TicksPerSecond())) / Fixed::FromInt(1000)).Ceil(), 0));
			if (const auto *range = module.block->Find("BoredRange"))
				boredom.boredRange = engine::config::values::ParseFixed(range->Value()).value_or(Fixed{});
			world.Add<BuilderBoredom>(entity);
			*world.Get<BuilderBoredom>(entity) = boredom;
			break;
		}
	// MinefieldBehavior::onCollide: workers (infantry dozers) set off only mines that say so.
	if (object->Is("INFANTRY") && object->Is("DOZER"))
		world.Add<gameplay::MineSafe>(entity);
	// PropagandaTowerBehavior::update keeps its effect only on scoring kinds.
	if (object->Is("SCORE") || object->Is("SCORE_CREATE") || object->Is("SCORE_DESTROY") || object->Is("MP_COUNT_FOR_VICTORY"))
	{
		world.Add<gameplay::PropagandaInfluence>(entity);
		if (!world.Has<gameplay::WeaponBonusConditions>(entity))
			world.Add<gameplay::WeaponBonusConditions>(entity);
	}
	// A parachute's locomotors drive its body (ParachuteSystem), not a move order.
	const std::uint32_t parachute = game.templates.ParachuteOf(world.Get<gameplay::DefinitionRef>(entity)->index);
	const gameplay::LocomotorDefinition *locomotor =
		parachute == ObjectTemplates::NoParachute ? content::ObjectLocomotor(*object, content.locomotors) : nullptr;
	Fixed z = game.ground.At(position);
	if (locomotor != nullptr)
	{
		world.Add<gameplay::Locomotion>(entity);
		world.Add<gameplay::MoveOrder>(entity);
		*world.Get<gameplay::Locomotion>(entity) = gameplay::MakeLocomotion(*locomotor);
		// Locomotor::Locomotor: the donut timer set 2.5 seconds on; its major radius for three point turns.
		world.Get<gameplay::Locomotion>(entity)->donutTimer = game.tick + game.step.TicksPerSecond() * 5 / 2;
		world.Get<gameplay::Locomotion>(entity)->majorRadius = object->geometry.majorRadius;
		// Its bounding circle (the distance it turns about its TurnPivotOffset is a fraction of).
		world.Get<gameplay::Locomotion>(entity)->boundingRadius = content::BoundingCircleRadius(object->geometry);
		// A set of more than one locomotor (a cliff climber's ground and cliff ones): which it moves on is chosen for where
		// it stands each tick (LocomotorChoiceSystem).
		if (const auto set = content::ObjectLocomotors(*object, content.locomotors); set.size() > 1)
		{
			world.Add<gameplay::LocomotorChoice>(entity);
			*world.Get<gameplay::LocomotorChoice>(entity) = gameplay::MakeLocomotorChoice(set);
		}
		if (locomotor->wanderWidth != Fixed{})
			gameplay::StartWander(*world.Get<gameplay::Locomotion>(entity),
				[&](std::int64_t low, std::int64_t high) { return Engine::Math::UniformInt(game.random, low, high); });
		// A runner (EnableRepulsors, CAN_BE_REPULSED): its vision range and its wander states' look period (10 + (id & 7)).
		if (content.aiData.enableRepulsors && object->Is("CAN_BE_REPULSED"))
		{
			world.Add<Repulsable>(entity);
			Repulsable &runner = *world.Get<Repulsable>(entity);
			runner.vision = object->visionRange;
			const auto *id = world.Get<gameplay::ObjectId>(entity);
			runner.waitFrames = static_cast<std::uint8_t>(10 + (id != nullptr ? id->value & 7u : 0u));
		}
		// WanderAIUpdate: idle, it heads off somewhere near.
		if (std::ranges::any_of(object->modules, [](const content::ModuleEntry &module) { return module.type == "WanderAIUpdate"; }))
			world.Add<gameplay::Wanderer>(entity);
		if (locomotor->height == gameplay::HeightBehavior::SeaLevel)
			z = game.ground.Surface(position);
	}
	// Its locomotor makes it an airborne target over its AirborneTargetingHeight (AIUpdateInterface::doLocomotor); a
	// parachute's own locomotor does the same for it.
	if (const gameplay::LocomotorDefinition *flown =
			locomotor != nullptr ? locomotor : (parachute != ObjectTemplates::NoParachute ? content::ObjectLocomotor(*object, content.locomotors) : nullptr))
	{
		world.Add<gameplay::AirborneTarget>(entity);
		world.Get<gameplay::AirborneTarget>(entity)->height = flown->airborneTargetingHeight;
	}
	// Object::Object: m_safeOcclusionFrame its OcclusionDelay on (every shipped thing takes GameData's
	// DefaultOcclusionDelay: none sets its own).
	{
		const std::uint64_t delay = content.gameData.defaultOcclusionDelayTicks;
		world.Add<gameplay::OcclusionSafe>(entity);
		*world.Get<gameplay::OcclusionSafe>(entity) = {game.tick + delay, delay};
	}
	*world.Get<gameplay::Transform>(entity) = {{position.x, position.y, z}, facing};
	detail::AddCombat(game, entity, *object, locomotor);
	// Several weapon or armor sets: its flags pick among them (its rider's WEAPON_RIDER set to begin with).
	if (const auto *sets = game.templates.loadouts.Of(world.Get<gameplay::DefinitionRef>(entity)->index);
		sets != nullptr && (sets->weaponSets.size() > 1 || sets->armorSets.size() > 1))
	{
		gameplay::Loadout loadout;
		loadout.weaponFlags = content::SetFlag(content::WeaponSetFlagNames, content::ReadObjectCombat(*object, game.step).riderWeaponCondition);
		// The set it was just armed from (WeaponSet::updateWeaponSet at creation): a change from it arms afresh.
		if (!sets->weaponSets.empty())
			loadout.weaponSet = gameplay::BestSet(sets->weaponSets, loadout.weaponFlags);
		world.Add<gameplay::Loadout>(entity);
		*world.Get<gameplay::Loadout>(entity) = loadout;
	}
	// Things moved by forces: those without a locomotor (hulks, debris, crates).
	// Its physics (PhysicsBehavior). A unit's locomotor moves it while its AI runs; physics alone once
	// that stops (disabled). A drone at rest is killed only dead or unmanned.
	if (auto body = content::ReadObjectPhysics(*object, game.step, world.Resource<gameplay::PhysicsSettings>()))
	{
		if (locomotor != nullptr)
			body->flags |= gameplay::physics_flag::Locomotive;
		if (object->Is("DRONE"))
			body->flags |= gameplay::physics_flag::Drone;
		world.Add<gameplay::PhysicsBody>(entity);
		if (!world.Has<gameplay::Attitude>(entity))
			world.Add<gameplay::Attitude>(entity);
		*world.Get<gameplay::PhysicsBody>(entity) = *body;
	}
	if (parachute != ObjectTemplates::NoParachute)
		StartParachute(game, entity, parachute);
	// FireWeaponCollide: its weapon fired at what runs into it (a burning tree's flames).
	if (const auto collide = content::ReadCollideWeapon(*object))
	{
		world.Add<gameplay::CollideWeapon>(entity);
		*world.Get<gameplay::CollideWeapon>(entity) = {game.templates.Weapon(collide->weapon), collide->requiresAflame ? std::uint8_t{1} : std::uint8_t{0},
			collide->fireOnce ? std::uint8_t{1} : std::uint8_t{0}, 0, 0};
	}
	// RadiusDecalUpdate: an AttackNugget may lay its decal (none yet).
	if (std::ranges::any_of(object->modules, [](const content::ModuleEntry &module) { return module.type == "RadiusDecalUpdate"; }))
		world.Add<RadiusDecal>(entity);
	// RailroadBehavior: a locomotive leads and sets off speeding up; a car coasts until something pulls it.
	if (const RailroadConfig *rail = game.templates.RailroadOf(world.Get<gameplay::DefinitionRef>(entity)->index))
	{
		world.Add<Railcar>(entity);
		Railcar &car = *world.Get<Railcar>(entity);
		car.locomotive = rail->locomotive ? 1 : 0;
		car.lead = car.locomotive;
		car.state = rail->locomotive ? ConductorState::Accelerate : ConductorState::Coast;
	}
	// TechBuildingBehavior: CAPTURED while a playable side holds it; neutral once it dies.
	if (std::any_of(object->modules.begin(), object->modules.end(), [](const content::ModuleEntry &module) { return module.type == "TechBuildingBehavior"; }))
		world.Add<TechBuilding>(entity);
	// AssistedTargetingUpdate: it joins in when its own kind asks.
	if (const auto assist = content::ReadAssistedTargeting(*object))
	{
		world.Add<gameplay::AssistedTargeting>(entity);
		*world.Get<gameplay::AssistedTargeting>(entity) = {assist->slot, 0, 0, assist->clip};
	}
	// CountermeasuresBehavior: its flares all loaded, off until its upgrade (on at once when StartsActive).
	if (const auto decoys = content::ReadCountermeasures(*object, game.step))
	{
		gameplay::Countermeasures made;
		made.volleySize = decoys->volleySize;
		made.volleys = decoys->volleys;
		made.available = decoys->volleySize * decoys->volleys;
		made.volleyTicks = decoys->volleyTicks;
		made.reloadTicks = decoys->reloadTicks;
		made.decoyTicks = decoys->decoyTicks;
		made.reactionTicks = decoys->reactionTicks;
		made.evasionRate = decoys->evasionRate;
		made.velocityFactor = decoys->velocityFactor;
		made.arc = Engine::Math::TurnFromDegrees(decoys->arcDegrees);
		made.mustReloadAtAirfield = decoys->mustReloadAtAirfield ? 1 : 0;
		made.upgraded = decoys->startsActive ? 1 : 0;
		if (const content::ObjectDefinition *flare = content.objects.Find(decoys->flare))
			made.flareDefinition = game.templates.Definition(*flare);
		world.Add<gameplay::Countermeasures>(entity);
		*world.Get<gameplay::Countermeasures>(entity) = made;
	}
	// SpyVisionUpdate modules: asleep until an upgrade, a power or their own timers turn them on.
	if (const auto spies = content::ReadSpyVisions(*object, game.step); !spies.empty())
	{
		world.Add<SpyVision>(entity);
		SpyVision &spy = *world.Get<SpyVision>(entity);
		spy.count = static_cast<std::uint8_t>(std::min<std::size_t>(spies.size(), spy.modules.size()));
		spy.player = world.Get<gameplay::Owner>(entity)->player;
	}
	// CleanupHazardUpdate: it cleans up hazards on its own, with its VETERAN weapon set (onObjectCreated).
	if (const auto cleanup = content::ReadCleanupHazard(*object, game.step); cleanup && world.Has<gameplay::MoveOrder>(entity) && world.Has<gameplay::Armament>(entity))
	{
		world.Add<CleanupHazard>(entity);
		CleanupHazard &cleaner = *world.Get<CleanupHazard>(entity);
		cleaner.scanTicks = cleanup->scanTicks;
		cleaner.scanRange = cleanup->scanRange;
		cleaner.slot = cleanup->weaponSlot;
		if (auto *loadout = world.Get<gameplay::Loadout>(entity))
			loadout->weaponFlags |= content::SetFlag(content::WeaponSetFlagNames, "VETERAN");
		// Its AI goes busy on an area job (aiBusy).
		if (!world.Has<gameplay::AiActivity>(entity))
			world.Add<gameplay::AiActivity>(entity);
	}
	// AutoFindHealingUpdate: it goes to be healed on its own.
	if (const auto healing = content::ReadAutoFindHealing(*object, game.step); healing && world.Has<gameplay::MoveOrder>(entity))
	{
		world.Add<HealSeeker>(entity);
		*world.Get<HealSeeker>(entity) = {healing->scanTicks, 0, healing->range, healing->neverHeal};
	}
	// PilotFindVehicleUpdate with its VeterancyCrateCollide: a pilot looking for a vehicle to join.
	if (const auto finder = content::ReadPilotFindVehicle(*object); finder && world.Has<gameplay::MoveOrder>(entity))
	{
		PilotSeeker seeker;
		seeker.scanTicks = finder->scanTicks;
		seeker.range = finder->range;
		seeker.minHealth = finder->minHealth;
		if (const auto collide = content::ReadCrateCollide(*object); collide && collide->kind == content::CrateKind::Veterancy)
		{
			seeker.required = collide->required;
			seeker.forbidden = collide->forbidden;
			seeker.addsOwnerVeterancy = collide->addsOwnerVeterancy ? 1u : 0u;
			seeker.isPilot = collide->isPilot ? 1u : 0u;
		}
		world.Add<PilotSeeker>(entity);
		*world.Get<PilotSeeker>(entity) = seeker;
	}
	// Everything dies by its die modules (things with no body too: trees, props, when felled or deleted by script).
	world.Add<gameplay::Mortality>(entity);
	*world.Get<gameplay::Mortality>(entity) = {game.templates.DeathOf(world.Get<gameplay::DefinitionRef>(entity)->index)};
	// What takes up room, and how it crushes and is crushed.
	if (!object->Is("NO_COLLIDE") && object->geometry.majorRadius > Fixed{})
	{
		world.Add<gameplay::Collider>(entity);
		*world.Get<gameplay::Collider>(entity) = {std::max(object->geometry.majorRadius, object->geometry.minorRadius),
			static_cast<std::uint32_t>(std::clamp(object->crusherLevel, 0, 255)), static_cast<std::uint32_t>(std::clamp(object->crushableLevel, 0, 255))};
	}
	// Its 3D bounds (GeometryInfo), for what stands firm (IMMOBILE; trees are snags: SHRUBBERY) and for a helicopter
	// that may spiral down into them (HelicopterSlowDeathBehavior).
	const bool immobile = object->Is("IMMOBILE");
	const bool collides = world.Has<gameplay::PhysicsBody>(entity) && world.Has<gameplay::Collider>(entity);
	if (immobile || collides ||
		std::ranges::any_of(object->modules, [](const content::ModuleEntry &module) { return module.type == "HelicopterSlowDeathBehavior" || module.type == "JetSlowDeathBehavior"; }))
	{
		const Fixed height = object->geometry.height;
		const bool sphere = object->geometry.shape == content::GeometryShape::Sphere;
		gameplay::BoundingVolume volume;
		volume.sphereRadius = content::BoundingSphereRadius(object->geometry);
		volume.centerLift = sphere ? Fixed{} : height / Fixed::FromInt(2);
		volume.below = sphere ? object->geometry.majorRadius : Fixed{};
		volume.above = sphere ? object->geometry.majorRadius : height;
		volume.circleRadius = content::BoundingCircleRadius(object->geometry);
		volume.immobile = immobile ? 1u : 0u;
		volume.snag = object->Is("SHRUBBERY") ? 1u : 0u;
		volume.structure = object->Is("STRUCTURE") ? 1u : 0u;
		world.Add<gameplay::BoundingVolume>(entity);
		*world.Get<gameplay::BoundingVolume>(entity) = volume;
	}
	// How its body answers running into others (PhysicsBehavior::onCollide): a body with physics that takes up room.
	if (collides)
	{
		gameplay::BodyCollision response;
		const auto *physics = std::ranges::find_if(object->modules, [](const content::ModuleEntry &module) { return module.type == "PhysicsBehavior" && module.block != nullptr; }) != object->modules.end()
			? &*std::ranges::find_if(object->modules, [](const content::ModuleEntry &module) { return module.type == "PhysicsBehavior" && module.block != nullptr; }) : nullptr;
		const auto text = [&](std::string_view key, std::string_view fallback) -> std::string {
			if (physics != nullptr)
				if (const auto *node = physics->block->Find(key))
					return std::string(node->Value());
			return std::string(fallback);
		};
		const std::string force = text("AllowCollideForce", "Yes");
		if (!force.empty() && (force[0] == 'N' || force[0] == 'n'))
			response.flags |= gameplay::body_collision_flag::NoForce;
		if (game.templates.HasAI(world.Get<gameplay::DefinitionRef>(entity)->index))
			response.flags |= gameplay::body_collision_flag::HasAI;
		if (object->Is("VEHICLE"))
			response.flags |= gameplay::body_collision_flag::Vehicle;
		response.buildingCrashWeapon = game.templates.Weapon(text("VehicleCrashesIntoBuildingWeaponTemplate", "VehicleCrashesIntoBuildingWeapon"));
		response.otherCrashWeapon = game.templates.Weapon(text("VehicleCrashesIntoNonBuildingWeaponTemplate", "VehicleCrashesIntoNonBuildingWeapon"));
		world.Add<gameplay::BodyCollision>(entity);
		*world.Get<gameplay::BodyCollision>(entity) = response;
		// A mobile unit's AI weighs the units it runs into (AIUpdateInterface::processCollision): what it is for the path
		// priority, and what it keeps about being held up.
		if (response.Has(gameplay::body_collision_flag::HasAI) && world.Get<gameplay::Locomotion>(entity) != nullptr)
		{
			gameplay::BlockingUnit unit;
			unit.kinds = static_cast<std::uint8_t>((object->Is("INFANTRY") ? gameplay::blocking_kind::Infantry : 0u) |
				(object->Is("VEHICLE") ? gameplay::blocking_kind::Vehicle : 0u) | (object->Is("DOZER") ? gameplay::blocking_kind::Dozer : 0u) | (object->Is("HARVESTER") ? gameplay::blocking_kind::Harvester : 0u) | (object->Is("NO_COLLIDE") ? gameplay::blocking_kind::NoCollide : 0u));
			world.Add<gameplay::BlockingUnit>(entity);
			*world.Get<gameplay::BlockingUnit>(entity) = unit;
			world.Add<gameplay::BlockedState>(entity);
			world.Add<gameplay::BlockContact>(entity);
		}
	}
	// Infantry that any crusher squishes.
	if (object->FindModule(content::ModuleSlot::Behavior) != nullptr)
		for (const content::ModuleEntry &module : object->modules)
			if (module.type == "SquishCollide")
			{
				world.Add<gameplay::Squishable>(entity);
				break;
			}
	// Airfields: their parking places and runways, in the world.
	if (const auto layout = content.parking.find(object->name); layout != content.parking.end())
	{
		world.Add<gameplay::Airfield>(entity);
		gameplay::Airfield &field = *world.Get<gameplay::Airfield>(entity);
		const auto &frame = *world.Get<gameplay::Transform>(entity);
		const Fixed c = Engine::Math::Cos(frame.facing), s = Engine::Math::Sin(frame.facing);
		const auto place = [&](const Engine::Math::FixedVector3 &local) {
			return Engine::Math::FixedVector3{frame.position.x + local.x * c - local.y * s, frame.position.y + local.x * s + local.y * c, frame.position.z + local.z};
		};
		for (const auto &space : layout->second.spaces)
			if (field.spaceCount < gameplay::Airfield::MaxSpaces)
				field.spaces[field.spaceCount++] = {place(space.hangar.position), place(space.parking.position), place(space.prep.position),
					frame.facing + space.hangar.facing, frame.facing + space.parking.facing, space.runway, frame.facing + space.apronFacing};
		for (const auto &runway : layout->second.runways)
			if (field.runwayCount < gameplay::Airfield::MaxRunways)
			{
				gameplay::RunwayPath &strip = field.runways[field.runwayCount++];
				strip.start = place(runway.start);
				strip.end = place(runway.end);
				strip.landing = runway.landing ? 1u : 0u;
				strip.landStart = place(runway.landStart);
				strip.landEnd = place(runway.landEnd);
				for (const auto &point : runway.taxi)
					if (strip.taxiCount < gameplay::RunwayPath::MaxTaxi)
						strip.taxi[strip.taxiCount++] = place(point);
				for (const auto &point : runway.creation)
					if (strip.creationCount < gameplay::RunwayPath::MaxCreation)
						strip.creation[strip.creationCount++] = place(point.position);
			}
		field.deckHeight = layout->second.deckHeight;
		field.frontRow = layout->second.frontRow ? 1u : 0u;
		if (layout->second.helipad)
		{
			field.hasHelipad = 1;
			field.helipad = place(layout->second.helipad->position);
			field.helipadFacing = frame.facing + layout->second.helipad->facing;
		}
		field.approachHeight = layout->second.approachHeight;
		// ParkingPlaceBehavior HealAmountPerSecond: it repairs its parked jets.
		for (const content::ModuleEntry &module : object->modules)
			if (module.type == "ParkingPlaceBehavior" && module.block != nullptr)
				if (const auto *heal = module.block->Find("HealAmountPerSecond"))
					if (const Fixed perSecond = engine::config::values::ParseFixed(heal->Value()).value_or(Fixed{}); perSecond > Fixed{})
					{
						world.Add<AirfieldHealing>(entity);
						world.Get<AirfieldHealing>(entity)->perSecond = perSecond;
					}
	}
	// A flight deck (an aircraft carrier): its jets, launches and queue.
	if (game.templates.FlightDeckOf(game.world.Get<gameplay::DefinitionRef>(entity)->index) != nullptr)
		world.Add<FlightDeck>(entity);
	// Crates: what runs into them may pick them up (their CrateCollide).
	if (content::ReadCrateCollide(*object))
		world.Add<Crate>(entity);
	// Supply docks: their points in the world, and whether they hold boxes (warehouses) or take them (supply centres).
	if (const auto layout = content.docks.find(object->name); layout != content.docks.end())
	{
		const content::DockLayout &source = layout->second;
		world.Add<gameplay::Dock>(entity);
		world.Add<gameplay::DockLook>(entity);
		gameplay::Dock &dock = *world.Get<gameplay::Dock>(entity);
		const auto &frame = *world.Get<gameplay::Transform>(entity);
		const Fixed c = Engine::Math::Cos(frame.facing), s = Engine::Math::Sin(frame.facing);
		const auto place = [&](const Engine::Math::FixedVector3 &local) {
			return FixedVector2{frame.position.x + local.x * c - local.y * s, frame.position.y + local.x * s + local.y * c};
		};
		dock.dynamic = source.dynamic;
		dock.approachCount = static_cast<std::uint8_t>(std::clamp<std::int32_t>(source.approachCount, 0, gameplay::Dock::MaxApproaches));
		for (const content::RestBone &bone : source.approach)
			if (dock.approachPoints < gameplay::Dock::MaxApproaches)
				dock.approach[dock.approachPoints++] = place(bone.position);
		dock.boneless = !source.enter.has_value();
		if (source.enter)
		{
			dock.enter = place(source.enter->position);
			dock.action = place(source.action.position);
			dock.exit = place(source.exit.position);
		}
		dock.majorRadius = object->geometry.majorRadius;
		dock.passthrough = source.passthrough;
		dock.drawsIn = source.drawsIn;
		if (source.grantStealthTicks > 0)
		{
			world.Add<SupplyStealthGrant>(entity);
			world.Get<SupplyStealthGrant>(entity)->deliveryTicks = source.grantStealthTicks;
		}
		if (source.warehouse)
		{
			world.Add<gameplay::ResourceStore>(entity);
			*world.Get<gameplay::ResourceStore>(entity) = source.store;
		}
		else if (source.repair)
		{
			world.Add<gameplay::RepairDock>(entity);
			world.Get<gameplay::RepairDock>(entity)->fullHealTicks = source.fullHealTicks;
		}
		else if (!source.railed)
			world.Add<gameplay::ResourceDepot>(entity);
	}
	// RailedTransportAIUpdate::loadWaypointData: the paths whose <prefix>StartNN and <prefix>EndNN are both on the map,
	// NN from 01 to 32 (kept in order; the original counted them but kept each at its own number).
	if (const RailedTransportConfig *railed = game.templates.RailedTransportOf(world.Get<gameplay::DefinitionRef>(entity)->index))
	{
		world.Add<RailedTransport>(entity);
		RailedTransport &ferry = *world.Get<RailedTransport>(entity);
		for (std::size_t index = 0; index < RailedTransport::MaxPaths; ++index)
		{
			const std::uint32_t from = game.waypoints.Find(std::format("{}Start{:02}", railed->content.pathPrefix, index + 1));
			const std::uint32_t to = game.waypoints.Find(std::format("{}End{:02}", railed->content.pathPrefix, index + 1));
			if (from != gameplay::WaypointGraph::None && to != gameplay::WaypointGraph::None)
				ferry.paths[ferry.pathCount++] = {from, to};
		}
	}
	// MobMemberSlavedUpdate: no nexus until one spawns it (onEnslave); its count to its first look a random 0..20.
	if (game.templates.MobMemberOf(world.Get<gameplay::DefinitionRef>(entity)->index) != nullptr)
	{
		const auto wait = static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, 20));
		world.Add<MobMember>(entity);
		world.Get<MobMember>(entity)->framesToWait = wait;
	}
	// SpectreGunshipUpdate: idle until its attack starts.
	if (game.templates.SpectreGunshipOf(world.Get<gameplay::DefinitionRef>(entity)->index) != nullptr)
		world.Add<SpectreGunship>(entity);
	// ParticleUplinkCannonUpdate: idle.
	if (game.templates.ParticleCannonOf(world.Get<gameplay::DefinitionRef>(entity)->index) != nullptr)
		world.Add<ParticleCannon>(entity);
	// MissileLauncherBuildingUpdate: its door closed.
	if (game.templates.LauncherDoorOf(world.Get<gameplay::DefinitionRef>(entity)->index) != nullptr)
		world.Add<LauncherDoor>(entity);
	// FireOCLAfterWeaponCooldownUpdate modules: watching nothing yet (m_valid false).
	if (!game.templates.cooldownCreations.Of(world.Get<gameplay::DefinitionRef>(entity)->index).empty())
		world.Add<CooldownCreations>(entity);
	// DeployStyleAIUpdate: packed, ready to move.
	if (const auto deploy = content::ReadDeployStyle(*object, game.step.TicksPerSecond()); deploy && world.Has<gameplay::MoveOrder>(entity))
	{
		world.Add<gameplay::Deploy>(entity);
		gameplay::Deploy &made = *world.Get<gameplay::Deploy>(entity);
		made.unpackTicks = deploy->unpackTicks;
		made.packTicks = deploy->packTicks;
		made.turretsOnlyWhenDeployed = deploy->turretsOnlyWhenDeployed;
		made.turretsMustCenter = deploy->turretsMustCenter;
		made.manualAnimations = deploy->manualAnimations;
	}
	// HiveStructureBody: damage it passes to its spawns.
	if (const auto hive = content::ReadHiveBody(*object))
	{
		world.Add<gameplay::HiveBody>(entity);
		*world.Get<gameplay::HiveBody>(entity) = {hive->propagate, hive->swallow};
	}
	// Vehicles may dock (aiDock: at a repair pad, ActionManager::canGetRepairedAt).
	if (object->Is("VEHICLE") && world.Has<gameplay::MoveOrder>(entity) && !world.Has<gameplay::Docking>(entity))
		world.Add<gameplay::Docking>(entity);
	// Supply trucks: their rounds between warehouses and supply centres.
	if (const auto harvester = content::ReadObjectHarvester(*object, game.step); harvester && world.Has<gameplay::MoveOrder>(entity))
	{
		world.Add<gameplay::Harvester>(entity);
		*world.Get<gameplay::Harvester>(entity) = *harvester;
		if (!world.Has<gameplay::Docking>(entity))
			world.Add<gameplay::Docking>(entity);
		if (const std::string_view boost = content::SupplyBoostUpgrade(*object); !boost.empty())
			if (const auto upgrade = content.upgrades.Find(boost))
				world.Get<gameplay::Harvester>(entity)->boostUpgrade = *upgrade;
	}
	// Jets: flying until an airfield takes them in (the ones built at one start in its hangar).
	if (const auto jet = content::ReadObjectJet(*object, content.locomotors, game.step))
	{
		// (Its drawn lift first: adding a component moves the entity, so before its Jet is written.)
		if (jet->minHeight > Engine::Math::Fixed{} && world.Get<gameplay::DrawOffset>(entity) == nullptr)
			world.Add<gameplay::DrawOffset>(entity);
		// (Its floor: raised while it is a flight deck's.)
		if (world.Get<gameplay::FloorLift>(entity) == nullptr)
			world.Add<gameplay::FloorLift>(entity);
		// (Its attackers' misses: getSneakyTargetingOffset, the Aurora's.)
		if (jet->sneakyOffset != Engine::Math::Fixed{} && world.Get<gameplay::SneakyTarget>(entity) == nullptr)
			world.Add<gameplay::SneakyTarget>(entity);
		world.Add<gameplay::Jet>(entity);
		gameplay::Jet &state = *world.Get<gameplay::Jet>(entity);
		state.flight = jet->flight;
		state.taxi = jet->taxi;
		state.lift = jet->lift;
		state.idleReturnTicks = jet->idleReturnTicks;
		state.takeoffPauseTicks = jet->takeoffPauseTicks;
		state.minHeight = jet->minHeight;
		state.parkingOffset = jet->parkingOffset;
		state.attack = jet->attack.value_or(jet->flight);
		state.returning = jet->returning.value_or(jet->flight);
		state.attackPersistTicks = jet->attackPersistTicks;
		state.missPersistTicks = jet->missPersistTicks;
		state.sneakyOffset = jet->sneakyOffset;
		state.outOfAmmoDamage = jet->outOfAmmoDamage;
		state.outOfAmmoDamageType = game.templates.weapons.unresistable;
		state.outOfAmmoDeathType = content::DeathTypeIndex("NORMAL").value_or(0);
		if (const std::uint32_t bit = content::ModelConditionBit("JETAFTERBURNER"); bit != content::NoCondition)
			state.afterburnerBit = bit;
		if (const std::uint32_t bit = content::ModelConditionBit("JETEXHAUST"); bit != content::NoCondition && bit < gameplay::Jet::NoExhaust)
			state.exhaustBit = static_cast<std::uint8_t>(bit);
		state.state = gameplay::JetState::Flying;
		state.since = state.idleSince = game.tick;
		if (!jet->needsRunway)
		{
			state.helicopter = 1;
			state.space = gameplay::Jet::NoSpace;
		}
		if (const auto *armament = world.Get<gameplay::Armament>(entity); armament != nullptr && armament->weapon != gameplay::WeaponCatalog::None)
		{
			state.reloadTicks = game.templates.weapons.At(armament->weapon).clipReload;
			state.clipSize = game.templates.weapons.At(armament->weapon).clipSize;
		}
	}
	// Factories.
	// Power plants and what runs on power.
	if (object->energyProduction != 0)
	{
		world.Add<gameplay::EnergySource>(entity);
		*world.Get<gameplay::EnergySource>(entity) = {object->energyProduction, object->energyBonus, 0};
	}
	// Its part in a match (VictoryConditions::hasSinglePlayerBeenDefeated: KINDOF_STRUCTURE with KINDOF_MP_COUNT_FOR_VICTORY
	// keeps its player in; Team::killTeam hands KINDOF_TECH_BUILDING to the neutral side).
	{
		std::uint8_t role = 0;
		if (object->Is("STRUCTURE") && object->Is("MP_COUNT_FOR_VICTORY"))
			role |= gameplay::victory_role::CountsForVictory;
		if (object->Is("TECH_BUILDING"))
			role |= gameplay::victory_role::NeutralOnDefeat;
		// RebuildHoleExposeDie whose hole counts for victory.
		for (const content::ModuleEntry &module : object->modules)
			if (module.type == "RebuildHoleExposeDie" && module.block != nullptr)
				if (const auto *hole = module.block->Find("HoleName"); hole != nullptr && !hole->values.empty())
					if (const auto *kind = content.objects.Find(hole->Value()); kind != nullptr && kind->Is("STRUCTURE") && kind->Is("MP_COUNT_FOR_VICTORY"))
						role |= gameplay::victory_role::LeavesCountedHole;
		if (role != 0)
		{
			world.Add<gameplay::VictoryRole>(entity);
			world.Get<gameplay::VictoryRole>(entity)->flags = role;
		}
	}
	// KINDOF_POWERED: disabled while its player is short of power.
	if (object->Is("POWERED"))
		world.Add<gameplay::Powered>(entity);
	if (const auto production = content::ReadObjectProduction(*object, game.step))
	{
		world.Add<gameplay::ProductionQueue>(entity);
		world.Get<gameplay::ProductionQueue>(entity)->capacity = production->capacity;
		world.Get<gameplay::ProductionQueue>(entity)->runsWhileDisabled = production->runsWhileDisabled;
		world.Add<gameplay::ProductionDoors>(entity);
		gameplay::ProductionDoors &doors = *world.Get<gameplay::ProductionDoors>(entity);
		doors.count = production->doors;
		doors.openTicks = production->doorOpenTicks;
		doors.waitTicks = production->doorWaitTicks;
		doors.closeTicks = production->doorCloseTicks;
		doors.completeTicks = production->completeTicks;
		if (production->queueExit)
		{
			world.Add<gameplay::ProductionExitGate>(entity);
			gameplay::ProductionExitGate &gate = *world.Get<gameplay::ProductionExitGate>(entity);
			gate.delayTicks = production->exitDelayTicks;
			gate.burst = production->initialBurst;
		}
	}
	// QueueProductionExitUpdate without a production queue (a spawner's exit): its gate all the same.
	if (!world.Has<gameplay::ProductionExitGate>(entity))
		if (const auto exit = content::ReadQueueExit(*object, game.step))
		{
			world.Add<gameplay::ProductionExitGate>(entity);
			gameplay::ProductionExitGate &gate = *world.Get<gameplay::ProductionExitGate>(entity);
			gate.delayTicks = exit->exitDelayTicks;
			gate.burst = exit->initialBurst;
		}
	if (const auto topple = content::ReadObjectTopple(*object))
	{
		world.Add<gameplay::Topple>(entity);
		*world.Get<gameplay::Topple>(entity) = topple->topple;
		if (world.Get<gameplay::Attitude>(entity) == nullptr)
			world.Add<gameplay::Attitude>(entity);
	}
	if (const auto stealth = content::ReadObjectStealth(*object, game.step))
	{
		world.Add<gameplay::Stealth>(entity);
		*world.Get<gameplay::Stealth>(entity) = stealth->stealth;
		// StealthUpdate's constructor: stealth allowed from its delay on (m_stealthAllowedFrame = now + StealthDelay).
		world.Get<gameplay::Stealth>(entity)->allowedAt = game.tick + stealth->stealth.delay;
		// UseRiderStealth: its rider's rules, found each tick (calcStealthOwner).
		if (stealth->stealth.Option(gameplay::stealth_option::UseRiderStealth))
			world.Add<gameplay::StealthRider>(entity);
	}
	// Stealth detectors scan at random phases, so they do not all scan on one tick (as the original). StealthDetectorUpdate's
	// constructor draws GameLogicRandomValue(1, DetectionRate) only for an enabled one: an InitiallyDisabled one sleeps
	// (UPDATE_SLEEP_FOREVER) and draws nothing, leaving the logic random stream as it was.
	if (auto detector = content::ReadObjectStealthDetector(*object, game.step))
	{
		if (detector->Has(gameplay::stealth_detector_flag::Enabled))
			detector->nextScan = game.tick + static_cast<std::uint64_t>(Engine::Math::UniformInt(game.random, 1, static_cast<std::int64_t>(detector->rate)));
		world.Add<gameplay::StealthDetector>(entity);
		*world.Get<gameplay::StealthDetector>(entity) = *detector;
	}
	// Fire fields and the like fire their weapon at themselves.
	if (const auto fire = content::ReadObjectAutoFire(*object, game.step))
	{
		const std::uint32_t weapon = game.templates.Weapon(fire->weapon);
		if (weapon != gameplay::WeaponCatalog::None)
		{
			world.Add<gameplay::AutoFire>(entity);
			if (!world.Has<gameplay::WeaponBonusConditions>(entity))
				world.Add<gameplay::WeaponBonusConditions>(entity);
			*world.Get<gameplay::AutoFire>(entity) = {weapon, 0, game.tick + fire->initialDelay, fire->exclusiveDelay,
				game.tick + game.templates.weapons.At(weapon).suspendFxTicks};
		}
	}
	if (const auto life = content::ReadObjectLifetime(*object, game.step))
	{
		world.Add<gameplay::Lifetime>(entity);
		// LifetimeUpdate::LifetimeUpdate: a hulk lives as long as a script wants hulks to, when it says (calcSleepDelay).
		std::int64_t least = static_cast<std::int64_t>(life->minimum), most = static_cast<std::int64_t>(life->maximum);
		if (const auto *hulks = world.FindResource<gameplay::HulkLifetime>(); hulks != nullptr && hulks->Overridden() && object->Is("HULK"))
			least = most = hulks->overrideTicks;
		const auto delay = Engine::Math::UniformInt(game.random, least, most);
		*world.Get<gameplay::Lifetime>(entity) = {game.tick + static_cast<std::uint64_t>(std::max<std::int64_t>(delay, 1)), life->deletes ? 1u : 0u};
	}
	// Stealth grantors (the GPS scrambler) are removed after their final scan.
	if (const auto grant = content::ReadObjectGrantStealth(*object, game.step))
	{
		world.Add<gameplay::GrantStealth>(entity);
		*world.Get<gameplay::GrantStealth>(entity) = *grant;
		if (world.Get<gameplay::Lifetime>(entity) == nullptr)
		{
			world.Add<gameplay::Lifetime>(entity);
			*world.Get<gameplay::Lifetime>(entity) = {~std::uint64_t{0}, 1u, 0u};
		}
	}
	if (const auto transport = content::ReadObjectTransport(*object, game.step))
	{
		world.Add<gameplay::Transport>(entity);
		world.Add<gameplay::ContainedDefinitions>(entity); // what it holds, for attack priority sets
		*world.Get<gameplay::Transport>(entity) = {.definition = *transport,
			.cruiseHeight = locomotor != nullptr ? locomotor->preferredHeight : Fixed{}};
		if (const auto exit = content.transportExitBones.find(object->name); exit != content.transportExitBones.end())
		{
			world.Get<gameplay::Transport>(entity)->definition.exitBone = exit->second.position;
			world.Get<gameplay::Transport>(entity)->definition.hasExitBone = 1;
		}
		if (const auto paths = content.transportExitPaths.find(object->name); paths != content.transportExitPaths.end())
		{
			auto &definition = world.Get<gameplay::Transport>(entity)->definition;
			definition.exitPaths = static_cast<std::uint32_t>(std::min<std::size_t>(paths->second.starts.size(), gameplay::TransportDefinition::MaxExitPaths));
			for (std::uint32_t path = 0; path < definition.exitPaths; ++path)
			{
				definition.exitStarts[path] = paths->second.starts[path];
				definition.exitEnds[path] = paths->second.ends[path];
			}
		}
		// TransportContain InitialPayload (a Troop Crawler's Red Guards, a Combat Bike's Rebel): made on its first update.
		// A RiderChangeContain's riders: what each one shows on it (its payload rider shown from the start).
		if (const auto riders = content::ReadRiderChange(*object, game.step))
		{
			RiderChange change;
			change.commandSets.fill(RiderChange::None);
			change.definitions.fill(RiderChange::None);
			change.conditions.fill(RiderChange::None);
			change.statuses.fill(RiderChange::None);
			for (const content::RiderChangeRider &rider : riders->riders)
			{
				const content::ObjectDefinition *kind = content.objects.Find(rider.name);
				if (kind == nullptr || change.count >= RiderChange::MaxRiders)
					continue;
				const std::uint32_t slot = change.count++;
				change.definitions[slot] = game.templates.Definition(*kind);
				const std::uint32_t bit = content::ModelConditionBit(rider.condition);
				change.conditions[slot] = bit == content::NoCondition ? RiderChange::None : bit;
				change.weaponFlags[slot] = content::SetFlag(content::WeaponSetFlagNames, rider.weaponFlag);
				change.statuses[slot] = content::ObjectStatusBit(rider.status);
				// Its command set (setCommandSetStringOverride) and locomotor set (chooseLocomotorSet) on the bike.
				change.commandSets[slot] = rider.commandSet.empty() ? RiderChange::None : game.templates.CommandSet(rider.commandSet);
				change.locomotorSets[slot] = content::LocomotorSetIndex(rider.locomotorSet).value_or(0);
			}
			change.scuttleTicks = riders->scuttleTicks;
			const std::uint32_t scuttle = content::ModelConditionBit(riders->scuttleCondition);
			change.scuttleCondition = scuttle == content::NoCondition ? RiderChange::None : scuttle;
			const content::ObjectCombat combat = content::ReadObjectCombat(*object, game.step);
			for (std::uint32_t slot = 0; slot < change.count; ++slot)
				if (!combat.riderCondition.empty() && change.conditions[slot] == content::ModelConditionBit(combat.riderCondition))
					change.current = slot;
			world.Add<RiderChange>(entity);
			*world.Get<RiderChange>(entity) = change;
		}
		for (const content::ModuleEntry &module : object->modules)
			if ((module.type == "TransportContain" || module.type == "RiderChangeContain") && module.block != nullptr)
				if (const auto *payload = module.block->Find("InitialPayload"); payload != nullptr && payload->values.size() >= 2)
					if (const content::ObjectDefinition *rider = content.objects.Find(payload->Value(0)))
					{
						const auto count = engine::config::values::ParseInt(payload->Value(1)).value_or(0);
						if (count > 0)
						{
							world.Add<InitialPayload>(entity);
							*world.Get<InitialPayload>(entity) = {game.templates.Definition(*rider), static_cast<std::uint32_t>(count)};
						}
					}
		// OverlordContain / HelixContain PayloadTemplateName (createPayload, the same in both; every shipped one is an
		// Overlord's naming a single object: the Avenger's laser turret).
		for (const content::ModuleEntry &module : object->modules)
			if ((module.type == "OverlordContain" || module.type == "HelixContain") && module.block != nullptr)
				if (const auto *payload = module.block->Find("PayloadTemplateName"); payload != nullptr && !payload->values.empty())
					if (const content::ObjectDefinition *rider = content.objects.Find(payload->Value(0)))
					{
						world.Add<InitialPayload>(entity);
						*world.Get<InitialPayload>(entity) = {game.templates.Definition(*rider), 1u, 1u};
					}
	}
	// A way into its player's tunnel network (TunnelContain: TunnelTracker::onTunnelCreated), holding MaxTunnelCapacity.
	else if (const auto heal = content::ReadObjectTunnel(*object, game.step))
	{
		world.Add<gameplay::Transport>(entity);
		world.Add<gameplay::ContainedDefinitions>(entity); // what it holds, for attack priority sets
		*world.Get<gameplay::Transport>(entity) = {.definition = {.slots = content.gameData.maxTunnelCapacity}};
		world.Add<gameplay::Tunnel>(entity);
		world.Get<gameplay::Tunnel>(entity)->fullHealTicks = *heal;
		game.manifest.Link(entity, world.Get<gameplay::Owner>(entity)->player);
	}
	// A heal pad (HealContain): those inside heal and leave once whole.
	if (const auto heal = content::ReadObjectHealPad(*object, game.step); heal && world.Has<gameplay::Transport>(entity))
	{
		world.Add<gameplay::HealPad>(entity);
		world.Get<gameplay::HealPad>(entity)->fullHealTicks = *heal;
	}
	// Drones and their kind serve whoever made them (enslaved by the creation list that makes them).
	if (const auto slaved = content::ReadObjectSlaved(*object))
	{
		world.Add<gameplay::Slaved>(entity);
		world.Get<gameplay::Slaved>(entity)->definition = *slaved;
		world.Get<gameplay::Slaved>(entity)->radius = content::BoundingCircleRadius(object->geometry);
	}
	// Ground movers are routed over the navigation grid: the surfaces they move over, and how many cells their
	// footprint reaches (from its bounding circle: diameters up to 10 fit a cell, up to 30 reach one more, larger two).
	if (locomotor != nullptr && (locomotor->surfaces & gameplay::locomotor_surface::Air) == 0 && locomotor->surfaces != 0)
	{
		// The original's size bands (getRadiusAndCenter): floor(diameter / 10 + 0.3) cells across, halved, at most 2.
		Fixed diameter = content::BoundingCircleRadius(object->geometry) * Fixed::FromInt(2);
		if (diameter > Fixed::FromInt(10) && diameter < Fixed::FromInt(20))
			diameter = Fixed::FromInt(20);
		const std::int64_t across = std::max<std::int64_t>((diameter / Fixed::FromInt(10) + Fixed::FromRatio(3, 10)).Floor(), 1);
		const auto reach = static_cast<std::uint8_t>(std::min<std::int64_t>(across / 2, 2));
		// Centred in its cell when an odd number across, or capped.
		const bool centered = (across & 1) != 0 || across / 2 > 2;
		world.Add<gameplay::NavigationAgent>(entity);
		// Routed over every surface its set moves on (LocomotorSet::getValidSurfaces).
		const auto *choice = world.Get<gameplay::LocomotorChoice>(entity);
		const std::uint8_t surfaces = choice != nullptr ? gameplay::SetSurfaces(*choice) : locomotor->surfaces;
		*world.Get<gameplay::NavigationAgent>(entity) = {surfaces, reach, static_cast<std::uint8_t>(centered ? 1 : 0)};
		// Its goal claims (AIUpdateInterface's m_pathfindGoalCell) and where its moves end.
		world.Add<gameplay::PathfindGoal>(entity);
		world.Add<gameplay::MoveGoal>(entity);
	}
	// Structures that stay put are obstacles on the grid (not small ones, mines, projectiles or bridge towers).
	if (locomotor == nullptr && object->Is("STRUCTURE") && !object->geometry.small && !object->Is("MINE") && !object->Is("PROJECTILE") &&
		!object->Is("BRIDGE_TOWER") && object->geometry.majorRadius > Fixed{})
	{
		gameplay::NavigationObstacle obstacle;
		obstacle.footprint.shape = object->geometry.shape == content::GeometryShape::Box ? gameplay::ObstacleShape::Box : gameplay::ObstacleShape::Cylinder;
		obstacle.footprint.majorRadius = object->geometry.majorRadius;
		obstacle.footprint.minorRadius = object->geometry.minorRadius;
		obstacle.footprint.seeThrough = object->Is("CAN_SEE_THROUGH_STRUCTURE");
		world.Add<gameplay::NavigationObstacle>(entity);
		*world.Get<gameplay::NavigationObstacle>(entity) = obstacle;
	}
	// Hordes (HordeUpdate): infantry first looks after a random 1..UpdateRate ticks (its first wake), others once
	// more than UpdateRate ticks have passed since it was made.
	if (const gameplay::HordeDefinition *horde = game.templates.hordes.Of(world.Get<gameplay::DefinitionRef>(entity)->index))
	{
		const auto wake = static_cast<std::uint64_t>(Engine::Math::UniformInt(game.random, 1, static_cast<std::int64_t>(std::max<std::uint64_t>(horde->updateTicks, 1))));
		world.Add<gameplay::Horde>(entity);
		world.Get<gameplay::Horde>(entity)->nextTick = game.tick + (horde->infantry ? wake : horde->updateTicks + 1);
		if (!world.Has<gameplay::WeaponBonusConditions>(entity))
			world.Add<gameplay::WeaponBonusConditions>(entity);
	}
	// Objects with upgrade triggers look at them when made (Object::initObject -> updateUpgradeModules).
	if (game.templates.upgradeTriggers.Of(world.Get<gameplay::DefinitionRef>(entity)->index) != nullptr)
		world.Add<gameplay::Upgradable>(entity);
	// CostModifierUpgrade modules: which triggers they are (their player's costs follow them: CostToBuild).
	if (const auto *effects = game.templates.upgradeEffects.Of(world.Get<gameplay::DefinitionRef>(entity)->index, 0) != nullptr
			? &game.templates.upgradeEffects.byDefinition[world.Get<gameplay::DefinitionRef>(entity)->index]
			: nullptr)
	{
		std::uint32_t triggers = 0;
		for (std::size_t trigger = 0; trigger < effects->size() && trigger < 32; ++trigger)
			if ((*effects)[trigger].kind == content::UpgradeEffectKind::CostModifier)
				triggers |= 1u << trigger;
		if (triggers != 0)
		{
			world.Add<CostModifying>(entity);
			world.Get<CostModifying>(entity)->triggers = triggers;
		}
	}
	if (object->transportSlots > 0)
	{
		world.Add<gameplay::CargoSize>(entity);
		*world.Get<gameplay::CargoSize>(entity) = {static_cast<std::uint32_t>(object->transportSlots)};
	}
	// Its radar dish and control rods, retracted until upgraded.
	const content::BuildingExtensions extensions = content::ReadBuildingExtensions(*object, game.step);
	if (extensions.radarTicks)
	{
		world.Add<RadarDish>(entity);
		world.Get<RadarDish>(entity)->ticks = *extensions.radarTicks;
	}
	if (extensions.rodsTicks)
	{
		world.Add<ControlRods>(entity);
		world.Get<ControlRods>(entity)->ticks = *extensions.rodsTicks;
	}
	// OverchargeBehavior: off until its player switches it on.
	if (extensions.overcharge)
	{
		world.Add<gameplay::Overcharge>(entity);
		world.Get<gameplay::Overcharge>(entity)->drain = extensions.overcharge->drainPercent;
		world.Get<gameplay::Overcharge>(entity)->floor = extensions.overcharge->floor;
	}
	// Where its riders stand (its FIREPOINT bones: OpenContain::putObjAtNextFirePoint).
	if (const auto fire = content.transportFirePoints.find(object->name); fire != content.transportFirePoints.end() && world.Has<gameplay::Transport>(entity))
	{
		gameplay::TransportFirePoints points;
		points.count = static_cast<std::uint32_t>(std::min(fire->second.points.size(), gameplay::TransportFirePoints::Max));
		for (std::uint32_t index = 0; index < points.count; ++index)
			points.points[index] = fire->second.points[index];
		points.inTurret = fire->second.inTurret ? 1u : 0u;
		points.turretPivot = fire->second.turretPivot;
		world.Add<gameplay::TransportFirePoints>(entity);
		*world.Get<gameplay::TransportFirePoints>(entity) = points;
	}
	// Units may garrison it (GarrisonContain).
	if (const auto garrison = content::ReadObjectGarrison(*object, game.step))
	{
		world.Add<gameplay::Garrison>(entity);
		world.Get<gameplay::Garrison>(entity)->fullHealTicks = garrison->fullHealTicks;
		world.Get<gameplay::Garrison>(entity)->untilDestroyed = garrison->untilDestroyed;
		world.Get<gameplay::Garrison>(entity)->immuneToClear = garrison->immuneToClear ? 1u : 0u;
		if (const auto points = content.garrisonPoints.find(object->name); points != content.garrisonPoints.end())
		{
			world.Add<gameplay::GarrisonPoints>(entity);
			auto &fire = *world.Get<gameplay::GarrisonPoints>(entity);
			for (std::size_t set = 0; set < fire.points.size(); ++set)
			{
				fire.counts[set] = static_cast<std::uint32_t>(std::min(points->second[set].size(), fire.points[set].size()));
				std::copy_n(points->second[set].begin(), fire.counts[set], fire.points[set].begin());
			}
		}
		// IsEnclosingContainer No (a fire base): its occupants stand at its STATION bones (loadStationGarrisonPoints).
		if (const auto stations = content.garrisonStations.find(object->name); stations != content.garrisonStations.end())
		{
			world.Add<gameplay::GarrisonStations>(entity);
			auto &held = *world.Get<gameplay::GarrisonStations>(entity);
			held.count = static_cast<std::uint32_t>(std::min(stations->second.size(), held.points.size()));
			std::copy_n(stations->second.begin(), held.count, held.points.begin());
		}
		if (garrison->rosterCount > 0)
			if (const content::ObjectDefinition *roster = content.objects.Find(garrison->rosterObject))
			{
				world.Get<gameplay::Garrison>(entity)->rosterDefinition = game.templates.Definition(*roster);
				world.Get<gameplay::Garrison>(entity)->rosterLeft = garrison->rosterCount;
			}
	}
	// It keeps spawns about (SpawnBehavior): slot n due after frame n (all at once for an initial burst).
	if (const auto spawns = content::ReadObjectSpawner(*object, game.step); spawns && spawns->number > 0 && !spawns->templates.empty())
	{
		gameplay::Spawner spawner;
		for (const std::string &name : spawns->templates)
			if (const content::ObjectDefinition *made = content.objects.Find(name); made != nullptr && spawner.templateCount < gameplay::Spawner::MaxTemplates)
				spawner.templates[spawner.templateCount++] = game.templates.Definition(*made);
		const auto count = static_cast<std::uint8_t>(std::min<std::int64_t>(spawns->number, gameplay::Spawner::MaxSpawns));
		for (std::uint8_t slot = 0; slot < count; ++slot)
			spawner.due[spawner.dueCount++] = spawns->initialBurst > 0 ? 0u : slot;
		spawner.replaceDelay = spawns->replaceTicks;
		spawner.oneShotLeft = spawns->oneShot ? count : -1;
		spawner.requireSpawner = spawns->requireSpawner;
		spawner.budding = spawns->budding;
		spawner.aggregateHealth = spawns->aggregateHealth;
		spawner.number = count;
		spawner.spawnsAreWeapons = object->Is("SPAWNS_ARE_THE_WEAPONS");
		spawner.freeWill = spawns->freeWill;
		spawner.initialBurstLeft = static_cast<std::uint8_t>(std::clamp<std::int64_t>(spawns->initialBurst, 0, 255)); // m_initialBurstCountdown
		if (spawner.templateCount > 0)
		{
			world.Add<gameplay::Spawner>(entity);
			*world.Get<gameplay::Spawner>(entity) = spawner;
			// computeAggregateStates: OBJECT_STATUS_MASKED, no target for any weapon (WeaponSet::isValidTarget).
			if (spawner.aggregateHealth)
				if (auto *body = world.Get<gameplay::Targetable>(entity))
					body->classes |= gameplay::target_class::Unattackable;
		}
	}
	// It shows its turns (AnimationSteeringUpdate).
	if (const auto steering = content::ReadAnimationSteering(*object, game.step))
	{
		world.Add<SteeringLook>(entity);
		world.Get<SteeringLook>(entity)->transitionTicks = *steering;
	}
	// Its create modules as it is made (onCreate): a starting level when its player knows the science
	// (VeterancyGainCreate: never lower than it is); GrantUpgradeCreate with ExemptStatus UNDER_CONSTRUCTION, when it is
	// not being built. The rest waits for it to be built (CreateModulesBuildComplete).
	const content::CreateModules creates = content::ReadCreateModules(*object);
	for (const content::VeterancyGain &gain : creates.veterancyGains)
	{
		const std::uint32_t player = world.Get<gameplay::Owner>(entity)->player;
		const auto science = gain.science.empty() ? std::nullopt : content.Science(gain.science);
		if (!gain.science.empty() && (!science || !world.Resource<gameplay::PlayerSciences>().Has(player, *science)))
			continue;
		if (world.Get<gameplay::Experience>(entity)->level < gain.level)
			PlaceAtVeterancy(game, entity, gain.level);
	}
	if (!underConstruction)
		for (const content::GrantedUpgrade &granted : creates.grantedUpgrades)
			if (granted.onCreate)
				object_factory_detail::GrantUpgrade(game, entity, granted.upgrade);
	// HeightDieUpdate.
	for (const content::ModuleEntry &module : object->modules)
		if (module.type == "HeightDieUpdate" && module.block != nullptr)
		{
			gameplay::HeightDie die;
			const auto fixed = [&](std::string_view key, Fixed fallback) {
				const auto *node = module.block->Find(key);
				return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(fallback) : fallback;
			};
			const auto yes = [&](std::string_view key) {
				const auto *node = module.block->Find(key);
				return node != nullptr && engine::config::values::ParseBool(node->Value()).value_or(false);
			};
			die.targetHeight = fixed("TargetHeight", Fixed{});
			die.onlyWhenMovingDown = yes("OnlyWhenMovingDown") ? 1 : 0;
			die.snapToGround = yes("SnapToGroundOnDeath") ? 1 : 0;
			die.includeStructures = yes("TargetHeightIncludesStructures") ? 1 : 0;
			die.particlesAt = fixed("DestroyAttachedParticlesAtHeight", Fixed::FromInt(-1));
			die.circleRadius = content::BoundingCircleRadius(object->geometry);
			die.sphereRadius = content::BoundingSphereRadius(object->geometry);
			die.centerZ = object->geometry.shape == content::GeometryShape::Sphere ? Fixed{} : object->geometry.height / Fixed::FromInt(2);
			// INI::parseDurationUnsignedInt: milliseconds rounded up to ticks.
			const Fixed delay = fixed("InitialDelay", Fixed{});
			die.initialDelay = delay > Fixed{} ? static_cast<std::uint64_t>((delay * Fixed::FromInt(static_cast<std::int64_t>(game.step.TicksPerSecond())) / Fixed::FromInt(1000)).Ceil()) : 0;
			world.Add<gameplay::HeightDie>(entity);
			*world.Get<gameplay::HeightDie>(entity) = die;
			break;
		}
	// Its special power modules (SpecialPowerModule's constructor).
	AttachSpecialPowers(game, entity, *object);
	game.roster.Join(team, entity, completesTeam);
	game.names.Assign(name, entity);
	// Object::initObject: the difficulty bonus, while objects get it.
	ObjectCreated(game, entity);
	// WeaponBonusUpdate: its first pulse on its first update.
	if (game.templates.WeaponBonusPulseOf(game.templates.Definition(*object)) != nullptr && !world.Has<WeaponBonusPulse>(entity))
		world.Add<WeaponBonusPulse>(entity);
	if (game.templates.OclTimerOf(game.templates.Definition(*object)) != nullptr && !world.Has<OclTimer>(entity))
		world.Add<OclTimer>(entity);
	// BoneFXUpdate: its bone effects' timers.
	if (game.templates.BoneFxOf(game.templates.Definition(*object)) != nullptr && !world.Has<BoneFx>(entity))
		world.Add<BoneFx>(entity);
	// BattleBusSlowDeathBehavior: driving, its first death to come.
	if (game.templates.BattleBusOf(game.templates.Definition(*object)) != nullptr && !world.Has<BattleBus>(entity))
		world.Add<BattleBus>(entity);
	// SupplyWarehouseCripplingBehavior: asleep, pristine.
	if (game.templates.WarehouseCripplingOf(game.templates.Definition(*object)) != nullptr && !world.Has<WarehouseCrippling>(entity))
		world.Add<WarehouseCrippling>(entity);
	// BattlePlanUpdate::onObjectCreated; Object::initObject: its player's battle plan bonuses.
	if (game.templates.BattlePlanOf(game.templates.Definition(*object)) != nullptr)
		InitBattlePlan(game, entity);
	BattlePlanObjectCreated(game, entity);
	// GarrisonContain::onObjectCreated: its InitialRoster made now (TheThingFactory->newObject) on its controlling player's
	// default team, each put inside when it may be (isValidContainerFor with its room: addToContain); one that may not
	// stays where it was made.
	if (auto *garrison = world.Get<gameplay::Garrison>(entity); garrison != nullptr && garrison->rosterLeft > 0)
	{
		const std::uint32_t count = garrison->rosterLeft;
		const std::string rosterName = game.templates.DefinitionAt(garrison->rosterDefinition).name;
		garrison->rosterLeft = 0;
		const auto *owner = world.Get<gameplay::Owner>(entity);
		if (const auto rosterTeam = owner != nullptr ? game.roster.DefaultTeam(owner->player) : std::nullopt)
			for (std::uint32_t index = 0; index < count; ++index)
			{
				const gameplay::Transform frame = *world.Get<gameplay::Transform>(entity);
				const ecs::Entity occupant = SpawnObject(game, rosterName, frame.position.XY(), frame.facing, *rosterTeam, "");
				const gameplay::Transport *room = world.Get<gameplay::Transport>(entity);
				if (world.IsAlive(occupant) && room != nullptr && !room->closed && room->occupied < room->definition.slots)
					PutInside(game, entity, occupant);
			}
	}
	return entity;
}
}

export namespace generalszh::gameplay
{
// CreateModule::onBuildComplete as the original calls it: for objects placed with the map or as a player's start, units
// a factory made, structures a dozer finished and ReplaceObjectUpgrade's replacements (never for what a creation list or
// a script makes): LockWeaponCreate locks its slot in hand for good; GrantUpgradeCreate gives its upgrade.
inline void CreateModulesBuildComplete(GameWorld &game, ecs::Entity entity)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *ref = world.IsAlive(entity) ? world.Get<gp::DefinitionRef>(entity) : nullptr;
	if (ref == nullptr)
		return;
	const content::CreateModules creates = content::ReadCreateModules(game.templates.DefinitionAt(ref->index));
	if (creates.lockedSlot)
		if (auto *armament = world.Get<gp::Armament>(entity))
		{
			if (!world.Has<gp::WeaponSlots>(entity))
			{
				world.Add<gp::WeaponSlots>(entity);
				armament = world.Get<gp::Armament>(entity);
				gp::WeaponSlot &primary = world.Get<gp::WeaponSlots>(entity)->slots[0];
				primary.weapon = armament->weapon;
				primary.clip = armament->clip;
				primary.barrels = armament->barrels;
				primary.aim = armament->turret ? gp::SlotAim::Turret : gp::SlotAim::Body;
			}
			gp::WeaponSlots &set = *world.Get<gp::WeaponSlots>(entity);
			if (set.slots[*creates.lockedSlot].weapon != gp::WeaponCatalog::None)
			{
				gp::StoreSlot(set.slots[set.current], *armament);
				set.locked = set.current = *creates.lockedSlot;
				gp::LoadSlot(*armament, set.slots[set.current], set.slots[set.current].aim == gp::SlotAim::Turret);
			}
		}
	for (const content::GrantedUpgrade &granted : creates.grantedUpgrades)
		object_factory_detail::GrantUpgrade(game, entity, granted.upgrade);
}
}
