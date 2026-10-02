export module games.generalszh.gameplay.upgrades.systems.upgrade_effect_system;
export import engine.gameplay.rts.loadout.components.loadout;
import games.generalszh.content.combat.loadout_content;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.upgrades.resources.upgrade_effects;
export import engine.gameplay.rts.upgrades.systems.upgrade_system;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.rts.stealth.components.stealth;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
export import engine.gameplay.rts.veterancy.components.experience;
export import engine.gameplay.rts.economy.components.energy_source;
export import games.generalszh.gameplay.powers.components.spy_vision;
export import engine.gameplay.rts.combat.components.countermeasures;
export import engine.gameplay.common.healing.components.healing;
export import games.generalszh.gameplay.appearance.components.building_extensions;
import engine.gameplay.common.appearance.components.appearance;
import engine.gameplay.rts.movement.components.locomotion;
export import games.generalszh.content.combat.weapon_bonus_content;
export import games.generalszh.gameplay.upgrades.components.command_set_override;
export import engine.gameplay.rts.upgrades.resources.player_upgrades;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.rts.powers.algorithms.special_power_timing;
export import engine.gameplay.rts.powers.systems.special_power_pause_system;
export import engine.gameplay.rts.radar.components.radar_provider;
export import engine.gameplay.common.appearance.components.part_overrides;
export import engine.gameplay.rts.slaves.components.spawner;

// Carries out the tick's upgrade triggers (each module's upgradeImplementation),
// through the command buffer, in the order they went:
//   MaxHealthUpgrade      ActiveBody::setMaxHealth(max + AddMaxHealth, ChangeType)
//   PassengersFireUpgrade its contain lets passengers fire (those aboard too)
//   StealthUpgrade        OBJECT_STATUS_CAN_STEALTH
//   ArmorUpgrade          ARMORSET_PLAYER_UPGRADE: its PLAYER_UPGRADE armor set
//   WeaponSetUpgrade      WEAPONSET_PLAYER_UPGRADE: its PLAYER_UPGRADE weapon set, fresh weapons
//   ObjectCreationUpgrade its UpgradeObject creation list, run by the session after the tick
//   WeaponBonusUpgrade    Object::setWeaponBonusCondition(PLAYER_UPGRADE) (a change re-times its weapons)
//   ExperienceScalarUpgrade its experience scalar plus AddXPScalar
//   PowerPlantUpgrade     Player::addPowerBonus: its EnergyBonus counts (while it produces)
//   RadarUpgrade          Player::addRadar: it gives its player radar (DisableProof: even short of power); its dish extends
//   CommandSetUpgrade     its command set is CommandSet, or CommandSetAlt once its player or it has TriggerAlt
//   UnpauseSpecialPowerUpgrade its module for the power unpauses once (retail: never starts ready)
//   SubObjectsUpgrade     unless it or its player has an upgrade it conflicts with: its parts shown and hidden (PartOverrides)
//   AutoHealBehavior      its dormant self or area heal wakes (GLA Junk Repair)
// Other modules' triggers still go once, and do nothing yet.
export namespace generalszh::gameplay
{
struct UpgradeEffectSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Upgradable>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::Transport>,
		ecs::Read<engine::gameplay::OffMap>, ecs::Read<engine::gameplay::Stealth>, ecs::Read<engine::gameplay::Armament>,
		ecs::Read<engine::gameplay::WeaponSlots>, ecs::Read<engine::gameplay::Targetable>, ecs::Read<engine::gameplay::WeaponBonusConditions>,
		ecs::Read<engine::gameplay::Experience>, ecs::Read<engine::gameplay::EnergySource>, ecs::Read<engine::gameplay::Loadout>,
		ecs::Read<RadarDish>, ecs::Read<SpyVision>, ecs::Read<engine::gameplay::Countermeasures>, ecs::Read<ControlRods>, ecs::Read<CommandSetOverride>, ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Upgradable>,
		ecs::Read<engine::gameplay::SpecialPowerTimers>, ecs::Read<engine::gameplay::RadarProvider>, ecs::Read<engine::gameplay::Appearance>, ecs::Read<engine::gameplay::Locomotion>,
		ecs::Read<engine::gameplay::PartOverrides>, ecs::Read<engine::gameplay::SelfHealing>, ecs::Read<engine::gameplay::AreaHealing>,
		ecs::Read<engine::gameplay::Spawner>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::UpgradeReactions>, ecs::Read<UpgradeEffects>,
		ecs::Read<engine::gameplay::WeaponCatalog>, ecs::Read<engine::gameplay::CargoManifest>, ecs::Write<UpgradeCreations>,
		ecs::Read<engine::gameplay::PlayerUpgrades>>;

	void Execute(ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const UpgradeEffects &effects = context.Read<UpgradeEffects>();
		const gp::WeaponCatalog &weapons = context.Read<gp::WeaponCatalog>();
		const gp::CargoManifest &manifest = context.Read<gp::CargoManifest>();
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		auto &creations = context.Write<UpgradeCreations>().list;
		creations.clear();
		// Components several triggers may change in one tick, as they leave them (committed after the pass).
		std::vector<std::pair<ecs::Entity, gp::WeaponBonusConditions>> bonuses;
		std::vector<std::pair<ecs::Entity, gp::Experience>> experiences;
		std::vector<std::pair<ecs::Entity, gp::EnergySource>> energies;
		std::vector<std::pair<ecs::Entity, gp::Loadout>> loadouts;
		std::vector<std::pair<ecs::Entity, gp::SpecialPowerTimers>> powers;
		std::vector<std::pair<ecs::Entity, gp::Appearance>> looks;
		std::vector<std::pair<ecs::Entity, gp::PartOverrides>> parts;
		std::vector<std::pair<ecs::Entity, SpyVision>> spyVisions;
		std::vector<std::pair<ecs::Entity, gp::AreaHealing>> areaHeals;
		std::vector<std::pair<ecs::Entity, gp::SelfHealing>> selfHeals;
		const auto pending = [&](auto &list, ecs::Entity entity, const auto *now) -> decltype(&list.front().second) {
			for (auto &entry : list)
				if (entry.first == entity)
					return &entry.second;
			if (now == nullptr)
				return nullptr;
			list.emplace_back(entity, *now);
			return &list.back().second;
		};
		context.Read<gp::UpgradeReactions>().ForEach([&](const gp::UpgradeReaction &reaction) {
			const gp::DefinitionRef *definition = lookup.Get<gp::DefinitionRef>(reaction.entity);
			const UpgradeEffect *effect = definition != nullptr ? effects.Of(definition->index, reaction.trigger) : nullptr;
			if (effect == nullptr)
				return;
			const ecs::Entity entity = reaction.entity;
			switch (effect->kind)
			{
			case content::UpgradeEffectKind::MaxHealth:
				if (const gp::Health *health = lookup.Get<gp::Health>(entity))
				{
					gp::Health changed = *health;
					gp::SetMaxHealth(changed, changed.maximum + effect->amount, effect->change);
					commands.Set<gp::Health>(entity, changed);
				}
				break;
			case content::UpgradeEffectKind::Armor:
				// ARMORSET_PLAYER_UPGRADE: its sets pick (the loadout system).
				if (gp::Loadout *loadout = pending(loadouts, entity, lookup.Get<gp::Loadout>(entity)))
				{
					loadout->armorFlags |= content::SetFlag(content::ArmorSetFlagNames, "PLAYER_UPGRADE");
					break;
				}
				if (const gp::Health *health = lookup.Get<gp::Health>(entity); health != nullptr && effect->armor != 0xFFFFFFFFu)
				{
					gp::Health changed = *health;
					changed.armor = effect->armor;
					commands.Set<gp::Health>(entity, changed);
				}
				break;
			case content::UpgradeEffectKind::Stealth:
				if (const gp::Stealth *stealth = lookup.Get<gp::Stealth>(entity))
				{
					gp::Stealth changed = *stealth;
					changed.Set(gp::stealth_flag::CanStealth, true);
					commands.Set<gp::Stealth>(entity, changed);
				}
				// SpawnBehavior::giveSlavesStealthUpgrade(TRUE): a spawner whose spawns are its weapons
				// (KINDOF_SPAWNS_ARE_THE_WEAPONS) gives each spawn it has now OBJECT_STATUS_CAN_STEALTH too.
				if (const gp::Spawner *spawner = lookup.Get<gp::Spawner>(entity); spawner != nullptr && spawner->spawnsAreWeapons)
					for (std::size_t index = 0; index < spawner->spawnedCount; ++index)
						if (const gp::Stealth *stealth = lookup.Get<gp::Stealth>(spawner->spawned[index]))
						{
							gp::Stealth changed = *stealth;
							changed.Set(gp::stealth_flag::CanStealth, true);
							commands.Set<gp::Stealth>(spawner->spawned[index], changed);
						}
				break;
			case content::UpgradeEffectKind::PassengersFire:
				if (const gp::Transport *transport = lookup.Get<gp::Transport>(entity))
				{
					gp::Transport changed = *transport;
					changed.definition.passengersFire = true;
					commands.Set<gp::Transport>(entity, changed);
					// Those aboard may fire now (isPassengerAllowedToFire asks every time).
					for (const ecs::Entity passenger : manifest.Aboard(entity))
						if (const gp::OffMap *inside = lookup.Get<gp::OffMap>(passenger); inside != nullptr && !inside->armed)
						{
							const gp::Targetable *kind = lookup.Get<gp::Targetable>(passenger);
							if (changed.definition.infantryOnly && (kind == nullptr || (kind->classes & gp::target_class::Infantry) == 0))
								continue;
							gp::OffMap armed = *inside;
							armed.armed = true;
							commands.Set<gp::OffMap>(passenger, armed);
						}
				}
				break;
			case content::UpgradeEffectKind::WeaponSet:
				// WEAPONSET_PLAYER_UPGRADE: its sets pick, fresh (the loadout system).
				if (gp::Loadout *loadout = pending(loadouts, entity, lookup.Get<gp::Loadout>(entity)))
				{
					loadout->weaponFlags |= content::SetFlag(content::WeaponSetFlagNames, "PLAYER_UPGRADE");
					break;
				}
				if (const gp::Armament *armament = lookup.Get<gp::Armament>(entity); armament != nullptr && effect->hasWeapons)
				{
					// WeaponSet::updateWeaponSet: the new set's weapons, fresh.
					const auto fresh = [&](std::uint32_t weapon) {
						gp::WeaponSlot slot;
						slot.weapon = weapon;
						if (weapon != gp::WeaponCatalog::None)
							slot.clip = weapons.At(weapon).clipSize;
						return slot;
					};
					const gp::WeaponSlots *slots = lookup.Get<gp::WeaponSlots>(entity);
					const bool multi = slots != nullptr || effect->weapons[1] != gp::WeaponCatalog::None || effect->weapons[2] != gp::WeaponCatalog::None;
					gp::Armament changed = *armament;
					if (multi)
					{
						gp::WeaponSlots set = slots != nullptr ? *slots : gp::WeaponSlots{};
						for (std::size_t index = 0; index < gp::WeaponSlotCount; ++index)
						{
							gp::WeaponSlot slot = fresh(effect->weapons[index]);
							slot.aim = slots != nullptr ? slots->slots[index].aim : index == 0 && armament->turret ? gp::SlotAim::Turret : gp::SlotAim::Body;
							slot.barrels = slots != nullptr ? slots->slots[index].barrels : index == 0 ? armament->barrels : std::uint8_t{1};
							set.slots[index] = slot;
						}
						set.current = 0;
						gp::LoadSlot(changed, set.slots[0], armament->turret);
						if (slots != nullptr)
							commands.Set<gp::WeaponSlots>(entity, set);
						else
							commands.Add<gp::WeaponSlots>(entity, set);
					}
					else
					{
						const gp::WeaponSlot slot = fresh(effect->weapons[0]);
						changed.weapon = slot.weapon;
						changed.clip = slot.clip;
						changed.readyTick = 0;
						changed.reloading = false;
						changed.barrel = 0;
					}
					commands.Set<gp::Armament>(entity, changed);
				}
				break;
			case content::UpgradeEffectKind::ObjectCreation:
				if (effect->creation != 0xFFFFFFFFu)
					creations.push_back({entity, effect->creation});
				break;
			case content::UpgradeEffectKind::WeaponBonus:
			{
				const gp::WeaponBonusConditions none{};
				const gp::WeaponBonusConditions *now = lookup.Get<gp::WeaponBonusConditions>(entity);
				if (gp::WeaponBonusConditions *conditions = pending(bonuses, entity, now != nullptr ? now : &none))
					gp::SetWeaponBonus(*conditions, content::weapon_bonus::PlayerUpgrade, true, context.Tick());
				break;
			}
			case content::UpgradeEffectKind::ExperienceScalar:
				if (gp::Experience *experience = pending(experiences, entity, lookup.Get<gp::Experience>(entity)))
					experience->scalar += effect->amount;
				break;
			case content::UpgradeEffectKind::PowerPlant:
				if (gp::EnergySource *energy = pending(energies, entity, lookup.Get<gp::EnergySource>(entity)))
					++energy->bonusSources;
				// PowerPlantUpdate::extendRods: out over RodsExtendTime (already out: nothing).
				if (const ControlRods *rods = lookup.Get<ControlRods>(entity); rods != nullptr && rods->state == ExtensionState::Retracted)
					commands.Set<ControlRods>(entity, ControlRods{rods->ticks, context.Tick() + rods->ticks, ExtensionState::Extending});
				break;
			case content::UpgradeEffectKind::Radar:
				// RadarUpgrade::upgradeImplementation: Player::addRadar.
				if (lookup.Get<gp::RadarProvider>(entity) == nullptr)
					commands.Add<gp::RadarProvider>(entity, gp::RadarProvider{static_cast<std::uint8_t>(effect->disableProof ? 1 : 0)});
				// RadarUpdate::extendRadar: out over RadarExtendTime.
				if (const RadarDish *dish = lookup.Get<RadarDish>(entity))
					commands.Set<RadarDish>(entity, RadarDish{dish->ticks, context.Tick() + dish->ticks, ExtensionState::Extending});
				break;
			case content::UpgradeEffectKind::CommandSet:
			{
				// CommandSetUpgrade::upgradeImplementation: the alternative once the player, or it, has TriggerAlt.
				std::uint32_t set = effect->commandSet;
				if (effect->triggerAlt != 0xFFFFFFFFu)
				{
					const gp::Owner *owner = lookup.Get<gp::Owner>(entity);
					const gp::Upgradable *own = lookup.Get<gp::Upgradable>(entity);
					if ((owner != nullptr && context.Read<gp::PlayerUpgrades>().Completed(owner->player).Has(effect->triggerAlt)) ||
						(own != nullptr && own->completed.Has(effect->triggerAlt)))
						set = effect->commandSetAlt;
				}
				if (set == 0xFFFFFFFFu)
					break;
				if (lookup.Get<CommandSetOverride>(entity) != nullptr)
					commands.Set<CommandSetOverride>(entity, CommandSetOverride{set});
				else
					commands.Add<CommandSetOverride>(entity, CommandSetOverride{set});
				break;
			}
			case content::UpgradeEffectKind::UnpausePower:
				if (gp::SpecialPowerTimers *timers = pending(powers, entity, lookup.Get<gp::SpecialPowerTimers>(entity)))
					for (std::uint32_t index = 0; index < timers->count; ++index)
						if (timers->timers[index].power == effect->power)
							gp::ResumeCountdown(timers->timers[index], context.Tick());
				break;
			case content::UpgradeEffectKind::ModelCondition:
				// ModelConditionUpgrade::upgradeImplementation: setModelConditionState(ConditionFlag).
				if (effect->condition != 0xFFFFFFFFu)
					if (gp::Appearance *look = pending(looks, entity, lookup.Get<gp::Appearance>(entity)))
						look->Set(effect->condition);
				break;
			case content::UpgradeEffectKind::GrantScience:
				// GrantScienceUpgrade::upgradeImplementation: its controlling player is granted the science.
				if (effect->science != 0xFFFFFFFFu)
					creations.push_back({entity, 0xFFFFFFFFu, effect->science});
				break;
			case content::UpgradeEffectKind::Minefield:
				// GenerateMinefieldBehavior::upgradeImplementation: placeMines (the game lays them after the step).
				creations.push_back({entity, 0xFFFFFFFFu, 0xFFFFFFFFu, true});
				break;
			case content::UpgradeEffectKind::ReplaceObject:
				// ReplaceObjectUpgrade::upgradeImplementation (the game swaps them after the step).
				if (effect->replacement != 0xFFFFFFFFu)
					creations.push_back({entity, 0xFFFFFFFFu, 0xFFFFFFFFu, false, effect->replacement});
				break;
			case content::UpgradeEffectKind::Countermeasures:
				// CountermeasuresBehavior: isUpgradeActive from now.
				if (const gp::Countermeasures *decoys = lookup.Get<gp::Countermeasures>(entity); decoys != nullptr && decoys->upgraded == 0)
				{
					gp::Countermeasures changed = *decoys;
					changed.upgraded = 1;
					commands.Set<gp::Countermeasures>(entity, changed);
				}
				break;
			case content::UpgradeEffectKind::AutoHeal:
				// AutoHealBehavior::upgradeImplementation: awake now, it heals (while hurt) from its next update.
				if ((effect->parts & content::UpgradeEffectContent::SelfHeal) != 0)
				{
					const std::uint32_t index = effect->parts & ~content::UpgradeEffectContent::SelfHeal;
					if (gp::SelfHealing *self = pending(selfHeals, entity, lookup.Get<gp::SelfHealing>(entity)); self != nullptr && index < self->count)
					{
						gp::SelfHealProgram &program = self->programs[index];
						if (program.dormant != 0)
						{
							program.dormant = 0;
							program.nextTick = context.Tick();
						}
					}
				}
				else if (gp::AreaHealing *area = pending(areaHeals, entity, lookup.Get<gp::AreaHealing>(entity)); area != nullptr && effect->parts < area->count)
				{
					gp::AreaHealProgram &program = area->programs[effect->parts];
					if ((program.flags & gp::area_healing::Dormant) != 0)
					{
						program.flags &= ~gp::area_healing::Dormant;
						program.nextTick = context.Tick();
					}
				}
				break;
			case content::UpgradeEffectKind::SpyVision:
				// SpyVisionUpdate::upgradeImplementation (NeedsUpgrade): activateSpyVision(SelfPoweredDuration), once.
				if (SpyVision *spy = pending(spyVisions, entity, lookup.Get<SpyVision>(entity)); spy != nullptr && effect->parts < spy->count)
				{
					SpyVisionState &module = spy->modules[effect->parts];
					if (module.upgraded == 0)
					{
						module.upgraded = 1;
						module.activateAsked = 1;
						module.activateTicks = SpyVisionState::Never;
					}
				}
				break;
			case content::UpgradeEffectKind::LocomotorSet:
				// AIUpdateInterface::setLocomotorUpgrade: its normal set is the upgraded one from now on; on its normal set,
				// it moves on it at once.
				if (const gp::Locomotion *motion = lookup.Get<gp::Locomotion>(entity); motion != nullptr && effect->locomotor)
				{
					gp::Locomotion upgraded = *motion;
					upgraded.upgraded = 1;
					if (motion->set == 0)
						upgraded.locomotor = gp::MakeLocomotion(*effect->locomotor).locomotor;
					commands.Set<gp::Locomotion>(entity, upgraded);
				}
				break;
			case content::UpgradeEffectKind::SubObjects:
			{
				// SubObjectsUpgrade::upgradeImplementation: nothing if it, or its player, has an upgrade it conflicts with.
				const gp::Upgradable *own = lookup.Get<gp::Upgradable>(entity);
				const gp::Owner *owner = lookup.Get<gp::Owner>(entity);
				if ((own != nullptr && own->completed.AnyOf(effect->conflicting)) ||
					(owner != nullptr && context.Read<gp::PlayerUpgrades>().Completed(owner->player).AnyOf(effect->conflicting)))
					break;
				const gp::PartOverrides none{};
				const gp::PartOverrides *now = lookup.Get<gp::PartOverrides>(entity);
				pending(parts, entity, now != nullptr ? now : &none)->Apply(static_cast<std::uint8_t>(effect->parts));
				break;
			}
			case content::UpgradeEffectKind::Other:
				break;
			}
		});
		for (const auto &[entity, energy] : energies)
			commands.Set<gp::EnergySource>(entity, energy);
		for (const auto &[entity, timers] : powers)
			commands.Set<gp::SpecialPowerTimers>(entity, timers);
		for (const auto &[entity, look] : looks)
			commands.Set<gp::Appearance>(entity, look);
		for (const auto &[entity, conditions] : bonuses)
		{
			if (lookup.Get<gp::WeaponBonusConditions>(entity) != nullptr)
				commands.Set<gp::WeaponBonusConditions>(entity, conditions);
			else
				commands.Add<gp::WeaponBonusConditions>(entity, conditions);
		}
		for (const auto &[entity, overrides] : parts)
		{
			if (lookup.Get<gp::PartOverrides>(entity) != nullptr)
				commands.Set<gp::PartOverrides>(entity, overrides);
			else
				commands.Add<gp::PartOverrides>(entity, overrides);
		}
		for (const auto &[entity, experience] : experiences)
			commands.Set<gp::Experience>(entity, experience);
		for (const auto &[entity, spy] : spyVisions)
			commands.Set<SpyVision>(entity, spy);
		for (const auto &[entity, area] : areaHeals)
			commands.Set<gp::AreaHealing>(entity, area);
		for (const auto &[entity, self] : selfHeals)
			commands.Set<gp::SelfHealing>(entity, self);
		for (const auto &[entity, loadout] : loadouts)
			commands.Set<gp::Loadout>(entity, loadout);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::UpgradeEffectSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.upgrade_effects";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<engine::gameplay::SpecialPowerPauseSystem>; // an unpause lands before this tick's disable edges
	using After = SystemTypeList<engine::gameplay::UpgradeSystem>;
};
}
