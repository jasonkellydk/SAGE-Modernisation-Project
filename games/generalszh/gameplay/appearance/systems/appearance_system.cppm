export module games.generalszh.gameplay.appearance.systems.appearance_system;
import games.generalszh.gameplay.containment.components.railed_transport;
import games.generalszh.gameplay.combat.components.checkpoint;
import games.generalszh.gameplay.combat_drop.components.combat_drop;
import std;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.rts.teams.resources.team_roster;
export import games.generalszh.gameplay.teams.components.tech_building;
export import engine.gameplay.rts.parachute.components.parachute;
export import engine.gameplay.common.status.components.disabled;

export import engine.ecs.system.system;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.spatial.systems.snapshot_system;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.combat.components.firing_tracker;
export import games.generalszh.gameplay.containment.components.rider_change;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.movement.components.descent;
export import engine.gameplay.rts.movement.systems.movement_system;
export import engine.gameplay.rts.combat.systems.weapon_system;
export import engine.gameplay.rts.combat.systems.turret_system;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.common.fire.components.flammable;
export import engine.gameplay.rts.stealth.components.stealth;
export import engine.gameplay.rts.production.components.production_doors;
export import engine.gameplay.rts.death.components.crash;
export import engine.gameplay.rts.death.components.collapse;
export import engine.gameplay.rts.death.components.structure_topple;
export import games.generalszh.gameplay.combat.components.enemy_near;
export import engine.gameplay.rts.death.components.blast_wave;
export import engine.gameplay.rts.topple.components.topple;
export import engine.gameplay.rts.containment.components.mount;
export import engine.gameplay.rts.mines.components.minefield;
export import engine.gameplay.rts.construction.components.sale;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.construction.components.builder;
export import engine.gameplay.rts.docking.components.docking;
export import engine.gameplay.rts.docking.components.dock_look;
import engine.gameplay.rts.combat.components.deploy;
import games.generalszh.gameplay.powers.components.launcher_door;
import games.generalszh.gameplay.powers.components.particle_cannon;
import games.generalszh.gameplay.powers.components.spectre_gunship;
export import engine.gameplay.rts.harvesting.components.harvester;
export import engine.gameplay.rts.harvesting.components.resource_store;
export import engine.gameplay.rts.loadout.components.loadout;
export import engine.gameplay.rts.death.systems.slow_death_system;
export import engine.gameplay.rts.slaves.components.slaved;
export import games.generalszh.gameplay.objects.resources.object_templates;
import games.generalszh.content.objects.model_conditions;
export import engine.gameplay.rts.blocking.components.blocked_state;
import games.generalszh.content.combat.loadout_content;

// Zero Hour's model conditions from the simulation state, each tick, in
// parallel: moving, attacking and the weapon's firing / between-shots /
// reloading phase, the body's damage state by health (damaged, really
// damaged, rubble at none; the original's GameData thresholds), parachuting,
// loaded transports, and dying (the dead with an AI: never moving or firing
// again, as the original's dead state). Draw modules pick condition states
// by these bits.
export namespace generalszh::gameplay
{
namespace gameplay = engine::gameplay;

struct AppearanceSettings
{
	// Health ratios at or below which a unit looks damaged / really damaged.
	Engine::Math::Fixed damaged{Engine::Math::Fixed::FromRatio(7, 10)};
	Engine::Math::Fixed reallyDamaged{Engine::Math::Fixed::FromRatio(35, 100)};
	// Ticks a shot shows as FIRING_A.
	std::uint64_t firingTicks{3};
};

}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::AppearanceSettings>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.appearance_settings";
};
}

// The model condition bits the system sets by name, worked out at compile time (ModelConditionBit is a search of the
// names: never in the row loop). Door n's bits are door 1's, 4 apart (model_condition::DoorOpening).
namespace generalszh::gameplay::appearance_bits
{
inline constexpr std::uint32_t ContinuousFireMean = content::ModelConditionBit("CONTINUOUS_FIRE_MEAN");
inline constexpr std::uint32_t ContinuousFireFast = content::ModelConditionBit("CONTINUOUS_FIRE_FAST");
inline constexpr std::uint32_t ContinuousFireSlow = content::ModelConditionBit("CONTINUOUS_FIRE_SLOW");
inline constexpr std::uint32_t Door1Opening = content::ModelConditionBit("DOOR_1_OPENING");
inline constexpr std::uint32_t Door1Closing = content::ModelConditionBit("DOOR_1_CLOSING");
inline constexpr std::uint32_t Door1WaitingOpen = content::ModelConditionBit("DOOR_1_WAITING_OPEN");
inline constexpr std::uint32_t Door1WaitingToClose = content::ModelConditionBit("DOOR_1_WAITING_TO_CLOSE");
inline constexpr std::uint32_t EnemyNearLook = content::ModelConditionBit("ENEMYNEAR");
inline constexpr std::uint32_t JetAfterburner = content::ModelConditionBit("JETAFTERBURNER");
inline constexpr std::uint32_t ParachutingLook = content::ModelConditionBit("PARACHUTING");
}

export namespace generalszh::gameplay
{
struct AppearanceSystem
{
	using Query = ecs::Query<ecs::Write<gameplay::Appearance>, ecs::Optional<gameplay::Locomotion>, ecs::Optional<gameplay::BlockedState>, ecs::Optional<gameplay::AttackTarget>,
		ecs::Optional<gameplay::Armament>, ecs::Optional<gameplay::Health>, ecs::Optional<gameplay::Descent>,
		ecs::Optional<gameplay::Transport>, ecs::Optional<gameplay::Dying>, ecs::Optional<gameplay::DefinitionRef>,
		ecs::Optional<gameplay::Flammable>, ecs::Optional<gameplay::Stealth>, ecs::Optional<gameplay::ProductionDoors>, ecs::Optional<gameplay::Crash>,
		ecs::Optional<gameplay::Docking>, ecs::Optional<gameplay::Harvester>, ecs::Optional<gameplay::ResourceStore>, ecs::Optional<gameplay::Loadout>,
		ecs::Optional<gameplay::Collapse>, ecs::Optional<gameplay::StructureTopple>, ecs::Optional<EnemyNear>, ecs::Optional<gameplay::Sale>, ecs::Optional<gameplay::UnderConstruction>, ecs::Optional<gameplay::Builder>,
		ecs::Optional<gameplay::Parachute>, ecs::Optional<gameplay::ParachuteRider>, ecs::Optional<gameplay::Disabled>, ecs::Optional<TechBuilding>,
		ecs::Optional<gameplay::Owner>, ecs::Optional<gameplay::Scorched>, ecs::Optional<gameplay::Topple>, ecs::Optional<gameplay::Mounted>, ecs::Optional<gameplay::Minefield>,
		ecs::Optional<gameplay::Deploy>, ecs::Optional<LauncherDoor>, ecs::Optional<ParticleCannon>, ecs::Optional<SpectreGunship>,
		ecs::Optional<gameplay::FiringTracker>, ecs::Optional<RiderChange>, ecs::Optional<Rappel>, ecs::Optional<Checkpoint>, ecs::Optional<RailedHaul>, ecs::Optional<gameplay::Slaved>, ecs::Optional<gameplay::DockLook>>;
	using Lookup = ecs::Lookup<ecs::Read<gameplay::Parachute>, ecs::Read<gameplay::Health>, ecs::Read<gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<AppearanceSettings>, ecs::Read<gameplay::TeamRoster>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const AppearanceSettings &settings = context.Read<AppearanceSettings>();
		const gameplay::TeamRoster &roster = context.Read<gameplay::TeamRoster>();
		namespace mc = content::model_condition;
		const std::uint64_t tick = context.Tick();
		auto appearances = chunk.Get<gameplay::Appearance>();
		const auto disabledRows = chunk.Get<gameplay::Disabled>();
		const auto techBuildings = chunk.Get<TechBuilding>();
		const auto owners = chunk.Get<gameplay::Owner>();
		const auto scorched = chunk.Get<gameplay::Scorched>();
		const auto topples = chunk.Get<gameplay::Topple>();
		const auto mounts = chunk.Get<gameplay::Mounted>();
		const auto minefields = chunk.Get<gameplay::Minefield>();
		const auto motion = chunk.Get<gameplay::Locomotion>();
		const auto blockedRows = chunk.Get<gameplay::BlockedState>();
		const auto targets = chunk.Get<gameplay::AttackTarget>();
		const auto armaments = chunk.Get<gameplay::Armament>();
		const auto healths = chunk.Get<gameplay::Health>();
		const auto descents = chunk.Get<gameplay::Descent>();
		const auto transports = chunk.Get<gameplay::Transport>();
		const auto dyings = chunk.Get<gameplay::Dying>();
		const auto definitions = chunk.Get<gameplay::DefinitionRef>();
		const auto fires = chunk.Get<gameplay::Flammable>();
		const auto stealths = chunk.Get<gameplay::Stealth>();
		const auto doorSets = chunk.Get<gameplay::ProductionDoors>();
		const auto crashes = chunk.Get<gameplay::Crash>();
		const auto dockings = chunk.Get<gameplay::Docking>();
		const auto dockLooks = chunk.Get<gameplay::DockLook>();
		const auto deploys = chunk.Get<gameplay::Deploy>();
		const auto launchers = chunk.Get<LauncherDoor>();
		const auto checkpoints = chunk.Get<Checkpoint>();
		const auto hauls = chunk.Get<RailedHaul>();
		const auto cannons = chunk.Get<ParticleCannon>();
		const auto gunships = chunk.Get<SpectreGunship>();
		const auto trackers = chunk.Get<gameplay::FiringTracker>();
		const auto riderChanges = chunk.Get<RiderChange>();
		const auto harvesters = chunk.Get<gameplay::Harvester>();
		const auto stores = chunk.Get<gameplay::ResourceStore>();
		const auto loadouts = chunk.Get<gameplay::Loadout>();
		const auto collapses = chunk.Get<gameplay::Collapse>();
		const auto structureTopples = chunk.Get<gameplay::StructureTopple>();
		const auto enemyNears = chunk.Get<EnemyNear>();
		const auto sales = chunk.Get<gameplay::Sale>();
		const auto sites = chunk.Get<gameplay::UnderConstruction>();
		const auto builders = chunk.Get<gameplay::Builder>();
		const auto chutes = chunk.Get<gameplay::Parachute>();
		const auto riders = chunk.Get<gameplay::ParachuteRider>();
		const auto rappels = chunk.Get<Rappel>();
		const auto slavedRows = chunk.Get<gameplay::Slaved>();
		const auto lookup = context.Lookup<Lookup>();
		constexpr std::uint32_t crateArmorOne = content::SetFlag(content::ArmorSetFlagNames, "CRATE_UPGRADE_ONE");
		constexpr std::uint32_t crateArmorTwo = content::SetFlag(content::ArmorSetFlagNames, "CRATE_UPGRADE_TWO");
		// TheWeaponSetTypeToModelConditionTypeMap: each of these weapon set flags shows as its model condition
		// (the riders' are the rider's own, set as it boards).
		constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 6> weaponSetLooks{{
			{content::SetFlag(content::WeaponSetFlagNames, "VETERAN"), content::ModelConditionBit("WEAPONSET_VETERAN")},
			{content::SetFlag(content::WeaponSetFlagNames, "ELITE"), content::ModelConditionBit("WEAPONSET_ELITE")},
			{content::SetFlag(content::WeaponSetFlagNames, "HERO"), content::ModelConditionBit("WEAPONSET_HERO")},
			{content::SetFlag(content::WeaponSetFlagNames, "PLAYER_UPGRADE"), content::ModelConditionBit("WEAPONSET_PLAYER_UPGRADE")},
			{content::SetFlag(content::WeaponSetFlagNames, "CRATEUPGRADE_ONE"), content::ModelConditionBit("WEAPONSET_CRATEUPGRADE_ONE")},
			{content::SetFlag(content::WeaponSetFlagNames, "CRATEUPGRADE_TWO"), content::ModelConditionBit("WEAPONSET_CRATEUPGRADE_TWO")},
		}};
		for (std::size_t row = 0; row < appearances.size(); ++row)
		{
			gameplay::Appearance &look = appearances[row];
			// RailedTransportDockUpdate: MOVING while pulled into or pushed out of a railed transport.
			// AIInternalMoveToState::update: held up by a unit in its way over a quarter second (7 ticks), it is not MOVING.
			const bool heldUp = !blockedRows.empty() && blockedRows[row].frames > 7u;
			look.Set(mc::Moving, (!motion.empty() && motion[row].speed > Engine::Math::Fixed{} && !heldUp) || !hauls.empty());
			const bool attacking = !targets.empty() && targets[row].target.IsValid();
			look.Set(mc::Attacking, attacking);
			bool firing = false, reloading = false;
			if (!armaments.empty() && armaments[row].firedTick != 0)
			{
				firing = tick < armaments[row].firedTick + settings.firingTicks;
				reloading = armaments[row].reloading && tick < armaments[row].readyTick;
			}
			// Only something armed shows its weapon's fire (a hacker's FIRING_A is its hacking's).
			if (!armaments.empty())
				look.Set(mc::FiringA, firing);
			// FiringTracker::speedUp / coolDown: CONTINUOUS_FIRE_MEAN or _FAST while firing faster, _SLOW spinning down.
			if (!trackers.empty())
			{
				look.Set(appearance_bits::ContinuousFireMean, trackers[row].level == 1);
				look.Set(appearance_bits::ContinuousFireFast, trackers[row].level == 2);
				look.Set(appearance_bits::ContinuousFireSlow, trackers[row].slow != 0);
			}
			// CheckpointUpdate: its gate DOOR_1_OPENING or DOOR_1_CLOSING (clearAndSetModelConditionState), neither at first.
			if (!checkpoints.empty() && checkpoints[row].gate != CheckpointGate::None)
			{
				look.Set(appearance_bits::Door1Opening, checkpoints[row].gate == CheckpointGate::Opening);
				look.Set(appearance_bits::Door1Closing, checkpoints[row].gate == CheckpointGate::Closing);
			}
			// EnemyNearUpdate: ENEMYNEAR while an enemy was near at its last look.
			if (!enemyNears.empty())
				look.Set(appearance_bits::EnemyNearLook, enemyNears[row].near != 0);
			// Object::adjustModelConditionForWeaponStatus: PREATTACK while it winds up (Weapon::getStatus PRE_ATTACK).
			look.Set(mc::PreattackA, !armaments.empty() && armaments[row].preAttackUntil != 0 && tick < armaments[row].preAttackUntil);
			look.Set(mc::UsingWeaponA, attacking || firing);
			look.Set(mc::ReloadingA, attacking && reloading && !firing);
			look.Set(mc::BetweenFiringShotsA, attacking && !firing && !reloading);
			if (!healths.empty() && healths[row].maximum > Engine::Math::Fixed{})
			{
				// The health ratio against the thresholds, as products (no division per object).
				const Engine::Math::Fixed current = healths[row].current, maximum = healths[row].maximum;
				const bool alive = current > Engine::Math::Fixed{};
				const bool reallyDamaged = current <= maximum * settings.reallyDamaged;
				// Come down (StructureCollapseUpdate) or toppled flat (StructureToppleUpdate): its post-collapse look, not its rubble.
				const bool collapsed = (!collapses.empty() && collapses[row].state == gameplay::CollapseState::Done) ||
					(!structureTopples.empty() && structureTopples[row].state == gameplay::StructureToppleState::Done);
				look.Set(mc::Rubble, !alive && !collapsed);
				look.Set(mc::PostCollapse, collapsed);
				// While under construction its looks do not follow its damage (evaluateVisualCondition waits for it to stand).
				const bool building = !sites.empty();
				look.Set(mc::ReallyDamaged, reallyDamaged && alive && !building);
				look.Set(mc::Damaged, current <= maximum * settings.damaged && !reallyDamaged && !building);
			}
			// MinefieldBehavior: spent, a mine looks rubble (setModelConditionState(MODELCONDITION_RUBBLE)); live again, not.
			if (!minefields.empty())
				look.Set(mc::Rubble, minefields[row].remaining == 0);
			// OverlordContain / HelixContain::onBodyDamageStateChange: a mounted portable structure takes its carrier's damage
			// state (never its rubble: at none it stays really damaged).
			if (!mounts.empty())
				if (const gameplay::Health *carrier = lookup.Get<gameplay::Health>(mounts[row].carrier); carrier != nullptr && carrier->maximum > Engine::Math::Fixed{})
				{
					const bool reallyDamaged = carrier->current <= carrier->maximum * settings.reallyDamaged;
					look.Set(mc::ReallyDamaged, reallyDamaged);
					look.Set(mc::Damaged, carrier->current <= carrier->maximum * settings.damaged && !reallyDamaged);
				}
			// Being sold (BuildAssistant): its scaffold up and coming down, then SOLD once its construction runs out.
			// Built (DozerAIUpdate): awaiting construction until a builder gets to it, then partly built, and actively
			// being built while one works on it (this tick or the last); the builder at work ACTIVELY_CONSTRUCTING.
			const bool started = !sites.empty() && sites[row].started != 0;
			const bool worked = started && sites[row].workedTick + 1 >= tick;
			look.Set(mc::AwaitingConstruction, !sites.empty() && !started);
			look.Set(mc::PartiallyConstructed, started || (!sales.empty() && sales[row].sunk == 0));
			look.Set(mc::ActivelyBeingConstructed, worked || (!sales.empty() && sales[row].sunk == 0));
			look.Set(mc::ActivelyConstructing, !builders.empty() && builders[row].atWork != 0);
			look.Set(mc::Sold, !sales.empty() && sales[row].sunk != 0);
			// TechBuildingBehavior::update: CAPTURED while its controlling player is a playable side.
			if (!techBuildings.empty())
			{
				const gameplay::Owner *owner = owners.empty() ? nullptr : &owners[row];
				look.Set(mc::Captured, owner != nullptr && owner->player < roster.PlayerCount() && roster.PlayerAt(owner->player).playable);
			}
			// ParachuteContain: its rider falls (FREEFALL) until the chute opens, then hangs (PARACHUTING), as does the
			// chute itself once open; a steady descent shows PARACHUTING throughout.
			const gameplay::Parachute *riding = riders.empty() ? nullptr : lookup.Get<gameplay::Parachute>(riders[row].chute);
			const bool open = riding != nullptr && riding->Has(gameplay::parachute_flag::Opened);
			// PhysicsBehavior: a body in free fall (its chute lost aloft) shows FREEFALL until it lands.
			const gameplay::Disabled *held = disabledRows.empty() ? nullptr : &disabledRows[row];
			look.Set(mc::Freefall, (riding != nullptr && !open) || (held != nullptr && (held->mask & gameplay::disabled_type::Freefall) != 0));
			look.Set(mc::Parachuting, !descents.empty() || open || (!chutes.empty() && chutes[row].Has(gameplay::parachute_flag::Opened)));
			look.Set(mc::Loaded, !transports.empty() && transports[row].occupied > 0);
			// AIRappelState: RAPPELLING on its rope until it lands.
			look.Set(mc::Rappelling, !rappels.empty());
			// OpenContain's door: DOOR_1_OPENING from each exit until DoorOpenTime has passed, then DOOR_1_CLOSING.
			if (!transports.empty() && transports[row].doorOpenedTick != 0 && transports[row].definition.doorOpenTicks > 0)
			{
				const bool open = tick < transports[row].doorOpenedTick + transports[row].definition.doorOpenTicks;
				look.Set(appearance_bits::Door1Opening, open);
				look.Set(appearance_bits::Door1Closing, !open);
			}
			// A disguiser never looks stealthed (calcStealthedStatusForPlayer: STEALTHLOOK_DISGUISED_ENEMY or NONE); disguised it
			// shows MODELCONDITION_DISGUISED (changeVisualDisguise).
			const bool disguiser = !stealths.empty() && stealths[row].Option(gameplay::stealth_option::DisguisesAsTeam);
			look.Set(mc::StealthedLook, !stealths.empty() && !disguiser && stealths[row].Has(gameplay::stealth_flag::Stealthed));
			look.Set(mc::DetectedLook, !stealths.empty() && !disguiser && stealths[row].Has(gameplay::stealth_flag::Detected));
			static constexpr std::uint32_t disguisedLook = content::ModelConditionBit("DISGUISED");
			look.Set(disguisedLook, !stealths.empty() && stealths[row].Has(gameplay::stealth_flag::Disguised));
			look.Set(mc::Aflame, !fires.empty() && fires[row].state == gameplay::FlameState::Aflame);
			look.Set(mc::Smoldering, !fires.empty() && fires[row].burned != 0);
			// NeutronMissileSlowDeathBehavior::doScorchBlast: burned by a blast's scorch wave, for good.
			look.Set(mc::Burned, !scorched.empty());
			// ToppleUpdate::applyTopplingForce: TOPPLED from the moment it starts to fall, for good.
			look.Set(mc::Toppled, !topples.empty() && topples[row].state != gameplay::ToppleState::Upright);
			// RiderChangeContain::onRemoving: a scuttled bike shows its ScuttleStatus until it goes.
			if (!riderChanges.empty() && riderChanges[row].scuttledTick != 0 && riderChanges[row].scuttleCondition != RiderChange::None)
				look.Set(riderChanges[row].scuttleCondition);
			// Supplies aboard (W3DModelDraw::updateDrawModuleSupplyStatus): a truck's boxes, a warehouse's stock.
			look.Set(mc::Carrying, (!harvesters.empty() && harvesters[row].boxes > 0) || (!stores.empty() && stores[row].boxes > 0));
			// Salvaged armor (SalvageCrateCollide::doArmorSet): the first crate's look, then the second's instead.
			const std::uint32_t armorFlags = loadouts.empty() ? 0u : loadouts[row].armorFlags;
			look.Set(mc::ArmorsetCrateUpgradeOne, (armorFlags & crateArmorOne) != 0);
			look.Set(mc::ArmorsetCrateUpgradeTwo, (armorFlags & crateArmorTwo) != 0);
			if (!loadouts.empty())
				for (const auto &[flag, condition] : weaponSetLooks)
					look.Set(condition, (loadouts[row].weaponFlags & flag) != 0);
			// MissileLauncherBuildingUpdate::switchToState: DOOR_1_OPENING, DOOR_1_WAITING_OPEN (open), DOOR_1_WAITING_TO_CLOSE,
			// DOOR_1_CLOSING; none closed.
			if (!launchers.empty())
			{
				const LauncherDoorState state = launchers[row].state;
				look.Set(appearance_bits::Door1Opening, state == LauncherDoorState::Opening);
				look.Set(appearance_bits::Door1WaitingOpen, state == LauncherDoorState::Open);
				look.Set(appearance_bits::Door1WaitingToClose, state == LauncherDoorState::WaitingToClose);
				look.Set(appearance_bits::Door1Closing, state == LauncherDoorState::Closing);
			}
			// SpectreGunshipUpdate: coming in and leaving DOOR_1_CLOSING with its afterburners (JETAFTERBURNER); circling
			// DOOR_1_OPENING.
			if (!gunships.empty() && gunships[row].status != GunshipStatus::Idle)
			{
				const GunshipStatus status = gunships[row].status;
				look.Set(appearance_bits::Door1Opening, status == GunshipStatus::Orbiting);
				look.Set(appearance_bits::Door1Closing, status != GunshipStatus::Orbiting);
				look.Set(appearance_bits::JetAfterburner, status != GunshipStatus::Orbiting);
			}
			// ParticleUplinkCannonUpdate::setLogicalStatus: UNPACKING raising its antenna, DEPLOYED almost ready, ready and
			// firing (kept after the beam), PACKING packing up; none idle (charging keeps what it had).
			if (!cannons.empty())
			{
				const CannonStatus status = cannons[row].status;
				const bool deployed = status == CannonStatus::AlmostReady || status == CannonStatus::ReadyToFire || status == CannonStatus::Firing ||
					status == CannonStatus::PostFire;
				if (status != CannonStatus::Charging && status != CannonStatus::PreFire)
				{
					look.Set(mc::Unpacking, status == CannonStatus::Preparing);
					look.Set(mc::Deployed, deployed);
					look.Set(mc::Packing, status == CannonStatus::Packing);
				}
			}
			// DeployStyleAIUpdate::setMyState: UNPACKING while unpacking, PACKING while packing, DEPLOYED once deployed (kept
			// while its turrets align).
			if (!deploys.empty())
			{
				const gameplay::DeployState state = deploys[row].state;
				look.Set(mc::Unpacking, state == gameplay::DeployState::Deploy);
				look.Set(mc::Packing, state == gameplay::DeployState::Undeploy);
				look.Set(mc::Deployed, state == gameplay::DeployState::ReadyToAttack || state == gameplay::DeployState::AligningTurrets);
			}
			// SlavedUpdate::setRepairModelConditionStates: a drone that repairs shows its arm (PACKING from its creation and
			// whenever a repair ends, UNPACKING, FIRING_B extending and welding, FIRING_C retracting).
			if (!slavedRows.empty() && slavedRows[row].definition.repairPerTick > Engine::Math::Fixed{})
			{
				using gameplay::SlaveRepairLook;
				static constexpr std::uint32_t firingB = content::ModelConditionBit("FIRING_B");
				static constexpr std::uint32_t firingC = content::ModelConditionBit("FIRING_C");
				const SlaveRepairLook arm = slavedRows[row].repairLook;
				look.Set(mc::Packing, arm == SlaveRepairLook::Packing);
				look.Set(mc::Unpacking, arm == SlaveRepairLook::Unpacking);
				look.Set(firingB, arm == SlaveRepairLook::FiringB);
				look.Set(firingC, arm == SlaveRepairLook::FiringC);
			}
			// Docking (DockUpdate::onEnterReached / onDockReached / onExitReached): beginning on the way in, active at
			// the business and on the way out, ending once out.
			if (!dockings.empty())
			{
				const gameplay::Docking &docking = dockings[row];
				const bool in = gameplay::IsDocking(docking) && docking.phase >= gameplay::DockPhase::MoveToDock;
				const bool active = in && docking.phase != gameplay::DockPhase::MoveToDock;
				look.Set(mc::Docking, in);
				look.Set(mc::DockingBeginning, in && !active);
				look.Set(mc::DockingActive, active);
				look.Set(mc::DockingEnding, !gameplay::IsDocking(docking) && docking.left);
				// DockUpdate::update on a supply source (KINDOF_SUPPLY_SOURCE): its active docker, a worker (DOZER and
				// HARVESTER), shows no MOVING while DOCKING_BEGINNING (its pick-up, not its walk).
				if (look.Test(mc::DockingBeginning) && !definitions.empty())
				{
					const content::ObjectDefinition &self = templates.DefinitionAt(definitions[row].index);
					const auto *dockRef = lookup.template Get<gameplay::DefinitionRef>(docking.dock);
					if (self.Is("DOZER") && self.Is("HARVESTER") && dockRef != nullptr && templates.DefinitionAt(dockRef->index).Is("SUPPLY_SOURCE"))
						look.Set(mc::Moving, false);
				}
			}
			// A dock's own (DockUpdate's flags on `me`: the supply center's arm and box).
			if (!dockLooks.empty())
			{
				const std::uint8_t on = dockLooks[row].flags;
				namespace dl = gameplay::dock_look;
				look.Set(mc::Docking, (on & dl::Docking) != 0);
				look.Set(mc::DockingBeginning, (on & dl::Beginning) != 0);
				look.Set(mc::DockingActive, (on & dl::Active) != 0);
				look.Set(mc::DockingEnding, (on & dl::Ending) != 0);
			}
			// A crashed helicopter's wreck, down.
			look.Set(mc::SpecialDamaged, !crashes.empty() && crashes[row].groundTick != 0);
			if (!doorSets.empty())
			{
				const gameplay::ProductionDoors &doors = doorSets[row];
				look.Set(mc::ConstructionComplete, doors.completeTick != 0);
				// ProductionUpdate sets door looks only for its door animations (NumDoorAnimations): with none it leaves them
				// to others (a superweapon's launch door).
				for (std::uint32_t door = 0; door < doors.count && door < gameplay::ProductionDoors::MaxDoors; ++door)
				{
					const gameplay::ProductionDoor &state = doors.doors[door];
					look.Set(appearance_bits::Door1Opening + 4 * door, state.opening != 0);
					look.Set(appearance_bits::Door1WaitingOpen + 4 * door, state.open != 0);
					look.Set(appearance_bits::Door1Closing + 4 * door, state.closing != 0);
				}
			}
			const bool dying = !dyings.empty() && !definitions.empty() && templates.HasAI(definitions[row].index);
			look.Set(mc::Dying, dying);
			// Flung by its death: flailing in the air, then bouncing once down (SlowDeathBehavior).
			const bool flung = !dyings.empty() && dyings[row].flung != 0;
			// Caught in a tree: no longer flailing or bouncing, PARACHUTING (it looks snagged).
			const bool snagged = flung && dyings[row].snagged != 0;
			look.Set(mc::ExplodedFlailing, flung && !snagged && dyings[row].landed == 0);
			look.Set(mc::ExplodedBouncing, flung && !snagged && dyings[row].landed != 0);
			if (snagged)
				look.Set(appearance_bits::ParachutingLook, true);
			// Crushed to death (CrushDie): its front, its back, or both.
			look.Set(mc::FrontCrushed, !dyings.empty() && (dyings[row].crushed & 1u) != 0);
			look.Set(mc::BackCrushed, !dyings.empty() && (dyings[row].crushed & 2u) != 0);
			if (dying)
				for (const std::uint32_t bit : {mc::Moving, mc::Attacking, mc::FiringA, mc::UsingWeaponA, mc::ReloadingA, mc::BetweenFiringShotsA})
					look.Set(bit, false);
		}
	}
};
}

export namespace ecs
{

template<>
struct SystemTraits<generalszh::gameplay::AppearanceSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.appearance";
	// Its rows are independent: large chunks are shared out in pieces of 32 rows.
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<engine::gameplay::SnapshotSystem>;
	// Sees this tick's deaths and slow deaths.
	using After = SystemTypeList<engine::gameplay::SlowDeathSystem>;
};
}
