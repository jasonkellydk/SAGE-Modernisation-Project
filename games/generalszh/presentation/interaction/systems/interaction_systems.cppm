export module games.generalszh.presentation.interaction.systems.interaction_systems;
import games.generalszh.gameplay.containment.components.railed_transport;
import games.generalszh.gameplay.powers.components.particle_cannon;
import games.generalszh.gameplay.powers.components.spectre_gunship;
import engine.gameplay.rts.vision.resources.shroud_map;
import std;
import engine.gameplay.rts.containment.components.transport;

export import engine.ecs.system.system;
export import games.generalszh.presentation.interaction.components.selected;
export import games.generalszh.presentation.interaction.resources.interaction_resources;
export import games.generalszh.presentation.interaction.algorithms.selection_rules;
export import engine.gameplay.common.spatial.systems.snapshot_system;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.status.components.script_status;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.rts.combat.algorithms.target_pitch;
export import engine.gameplay.rts.combat.algorithms.weapon_fitness;
export import engine.gameplay.rts.slaves.components.slaved;
export import engine.gameplay.common.identity.components.definition_ref;
import Engine.Core.Math.FixedPresentation;
import games.generalszh.gameplay.world.resources.deselections;
export import engine.gameplay.common.status.components.status_flags;
export import engine.gameplay.rts.loadout.components.loadout;
import games.generalszh.content.objects.object_status;
import games.generalszh.content.combat.loadout_content;
export import games.generalszh.presentation.interaction.resources.build_placement;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
import games.generalszh.gameplay.powers.algorithms.power_targeting;
import games.generalszh.content.control_bar.command_catalog;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.common.health.components.inactive_body;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.construction.components.builder;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.stealth.components.stealth;
import engine.gameplay.rts.containment.components.garrison;
import engine.gameplay.rts.containment.components.mount;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import games.generalszh.gameplay.containment.components.rider_change;
import engine.gameplay.rts.harvesting.components.harvester;
import engine.gameplay.rts.harvesting.components.resource_store;
import engine.gameplay.rts.docking.components.dock;
import engine.gameplay.common.healing.components.healing;
import engine.gameplay.rts.powers.components.special_power_timers;
import engine.gameplay.rts.powers.algorithms.special_power_timing;
import engine.gameplay.rts.powers.definitions.special_power_rules;
import engine.gameplay.rts.powers.resources.shared_power_timers;
import engine.gameplay.rts.aircraft.components.jet;
import engine.gameplay.rts.aircraft.components.airfield;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.rts.veterancy.definitions.veterancy;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.common.physics.resources.physics_settings;
import games.generalszh.gameplay.crates.components.pilot_seeker;
import games.generalszh.gameplay.upgrades.components.command_set_override;

// The player's pointer in the world, on real frame time (the original's
// SelectionTranslator and CommandTranslator for the default mouse setup):
//   left down anchors; moving past DragTolerance (either axis) drags a box;
//   left up is a click, a point when the box is under DragTolerance on both
//   axes: the selection rules pick (a point: the object under the pointer;
//   a box: selectable objects whose position projects into it) and select,
//   or leave the click to the command translator, which on a point with a
//   selection of the player's issues the context command, the first that
//   applies in the original's order: drive a special power's destination,
//   resume construction, dock, repair, get repaired, get healed, hijack,
//   make a car bomb, sabotage, salvage, enter, attack, capture, hack (a
//   vehicle, cash, a building), set a rally point; else move to the ground
//   clicked (never onto an object);
//   a right click (within DragTolerance, DragToleranceMS and the camera's
//   DragTolerance3D) deselects all.
// Selection is the Selected side table; orders go to PlayerOrders. Every frame the same evaluation, as a hint
// (CommandTranslator's DO_HINT through InGameUI::createMouseoverHint / createCommandHint), picks the mouse cursor
// (CursorState).

export namespace generalszh::presentation
{
namespace interaction_detail
{
// A pointer's world position into the simulation's numbers (once, on the issuing client: the command carries it).
inline Engine::Math::Fixed ToFixed(float value) noexcept
{
	return Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>(std::llround(static_cast<double>(value) * static_cast<double>(Engine::Math::Fixed::One().Raw()))));
}

struct Candidate
{
	ecs::Entity entity;
	float depth{0};
	PickedObject picked;
	float x{0}, y{0}; // its position
	std::uint32_t definition{0};
};

// CanAttackResult, in its order (the best of a selection is the highest).
enum class AttackResult : std::uint8_t
{
	NotPossible, // ATTACKRESULT_NOT_POSSIBLE
	InvalidShot, // ATTACKRESULT_INVALID_SHOT: armed, but no clear shot
	AfterMoving, // ATTACKRESULT_POSSIBLE_AFTER_MOVING
	Possible,    // ATTACKRESULT_POSSIBLE
};

// The kinds the context commands test.
namespace context_kind
{
inline constexpr std::size_t Dozer = content::KindOfBit("DOZER");
inline constexpr std::size_t Structure = content::KindOfBit("STRUCTURE");
inline constexpr std::size_t Infantry = content::KindOfBit("INFANTRY");
inline constexpr std::size_t Vehicle = content::KindOfBit("VEHICLE");
inline constexpr std::size_t Aircraft = content::KindOfBit("AIRCRAFT");
inline constexpr std::size_t Immobile = content::KindOfBit("IMMOBILE");
inline constexpr std::size_t RepairPad = content::KindOfBit("REPAIR_PAD");
inline constexpr std::size_t HealPad = content::KindOfBit("HEAL_PAD");
inline constexpr std::size_t FsAirfield = content::KindOfBit("FS_AIRFIELD");
inline constexpr std::size_t Bridge = content::KindOfBit("BRIDGE");
inline constexpr std::size_t BridgeTower = content::KindOfBit("BRIDGE_TOWER");
inline constexpr std::size_t RebuildHole = content::KindOfBit("REBUILD_HOLE");
inline constexpr std::size_t IgnoredInGui = content::KindOfBit("IGNORED_IN_GUI");
inline constexpr std::size_t MobNexus = content::KindOfBit("MOB_NEXUS");
inline constexpr std::size_t RejectUnmanned = content::KindOfBit("REJECT_UNMANNED");
inline constexpr std::size_t ProducedAtHelipad = content::KindOfBit("PRODUCED_AT_HELIPAD");
inline constexpr std::size_t Salvager = content::KindOfBit("SALVAGER");
inline constexpr std::size_t ImmuneToCapture = content::KindOfBit("IMMUNE_TO_CAPTURE");
inline constexpr std::size_t Capturable = content::KindOfBit("CAPTURABLE");
inline constexpr std::size_t CashGenerator = content::KindOfBit("CASH_GENERATOR");
inline constexpr std::size_t FsTechnology = content::KindOfBit("FS_TECHNOLOGY");
inline constexpr std::size_t StealthGarrison = content::KindOfBit("STEALTH_GARRISON");
inline constexpr std::size_t NoGarrison = content::KindOfBit("NO_GARRISON");
inline constexpr std::size_t PortableStructure = content::KindOfBit("PORTABLE_STRUCTURE");
inline constexpr std::size_t Parachute = content::KindOfBit("PARACHUTE");
inline constexpr std::size_t AircraftCarrier = content::KindOfBit("AIRCRAFT_CARRIER");
// Object::isFactionStructure (KINDOFMASK_FS).
inline constexpr std::array<std::size_t, 14> Faction{content::KindOfBit("FS_FACTORY"), content::KindOfBit("FS_BASE_DEFENSE"),
	content::KindOfBit("FS_TECHNOLOGY"), content::KindOfBit("FS_SUPPLY_DROPZONE"), content::KindOfBit("FS_SUPERWEAPON"),
	content::KindOfBit("FS_BLACK_MARKET"), content::KindOfBit("FS_SUPPLY_CENTER"), content::KindOfBit("FS_STRATEGY_CENTER"),
	content::KindOfBit("FS_FAKE"), content::KindOfBit("FS_INTERNET_CENTER"), content::KindOfBit("FS_ADVANCED_TECH"),
	content::KindOfBit("FS_BARRACKS"), content::KindOfBit("FS_WARFACTORY"), content::KindOfBit("FS_AIRFIELD")};
}

// ActionManager's tests for the local player's objects (CMD_FROM_PLAYER: a human's, so the fog counts), read through
// the interaction's lookup and the jets' query (which parking spaces are held); made for one evaluation.
template<typename Lookup, typename Jets>
struct ContextScene
{
	const Lookup &lookup;
	const SelectionCatalog &catalog;
	const engine::gameplay::Relationships &relationships;
	const engine::gameplay::ShroudMap &shroud;
	const engine::gameplay::GroundHeight &ground;
	const engine::gameplay::CargoManifest &manifest;
	const engine::gameplay::SpecialPowerRules &rules;
	const engine::gameplay::SharedPowerTimers &shared;
	const engine::gameplay::PhysicsSettings &physics;
	Jets &jets;
	std::uint32_t local;

	const content::KindOfMask *KindsOf(ecs::Entity entity) const
	{
		const auto *ref = lookup.template Get<engine::gameplay::DefinitionRef>(entity);
		return ref != nullptr && ref->index < catalog.kinds.size() ? &catalog.kinds[ref->index] : nullptr;
	}
	const SelectionLook *LookOf(ecs::Entity entity) const
	{
		const auto *ref = lookup.template Get<engine::gameplay::DefinitionRef>(entity);
		return ref != nullptr ? catalog.Of(ref->index) : nullptr;
	}
	bool Is(ecs::Entity entity, std::size_t bit) const
	{
		const content::KindOfMask *kinds = KindsOf(entity);
		return kinds != nullptr && content::HasKindOf(*kinds, bit);
	}
	bool FactionStructure(ecs::Entity entity) const
	{
		return std::ranges::any_of(context_kind::Faction, [&](std::size_t bit) { return Is(entity, bit); });
	}
	std::uint32_t PlayerOf(ecs::Entity entity) const
	{
		const auto *owner = lookup.template Get<engine::gameplay::Owner>(entity);
		return owner != nullptr ? owner->player : 0u;
	}
	std::uint32_t TeamOf(ecs::Entity entity) const
	{
		const auto *member = lookup.template Get<engine::gameplay::TeamMember>(entity);
		return member != nullptr ? member->team : engine::gameplay::Relationships::NoTeam;
	}
	// Object::getRelationship: `from`'s team's view of `to`'s.
	engine::gameplay::Relationship Relation(ecs::Entity from, ecs::Entity to) const
	{
		return relationships.Between(TeamOf(from), PlayerOf(from), TeamOf(to), PlayerOf(to));
	}
	// isEffectivelyDead.
	bool Dead(ecs::Entity entity) const
	{
		if (lookup.template Get<engine::gameplay::Dying>(entity) != nullptr || lookup.template Get<engine::gameplay::InactiveBody>(entity) != nullptr)
			return true;
		const auto *health = lookup.template Get<engine::gameplay::Health>(entity);
		return health != nullptr && engine::gameplay::IsDead(*health);
	}
	bool Building(ecs::Entity entity) const { return lookup.template Get<engine::gameplay::UnderConstruction>(entity) != nullptr; }
	bool Sold(ecs::Entity entity) const { return lookup.template Get<engine::gameplay::Sale>(entity) != nullptr; }
	bool DisabledBy(ecs::Entity entity, std::uint32_t types) const
	{
		const auto *off = lookup.template Get<engine::gameplay::Disabled>(entity);
		return off != nullptr && (off->mask & types) != 0;
	}
	// Object::isMobile: not IMMOBILE, not disabled.
	bool Mobile(ecs::Entity entity) const { return !Is(entity, context_kind::Immobile) && !DisabledBy(entity, engine::gameplay::disabled_type::All); }
	// getHealth() == getMaxHealth().
	bool Whole(ecs::Entity entity) const
	{
		const auto *health = lookup.template Get<engine::gameplay::Health>(entity);
		return health != nullptr && health->current == health->maximum;
	}
	bool Contained(ecs::Entity entity) const
	{
		return lookup.template Get<engine::gameplay::Passenger>(entity) != nullptr || lookup.template Get<engine::gameplay::OffMap>(entity) != nullptr;
	}
	// isObjectShroudedForAction: fogged or shrouded to the local player where it stands.
	bool Shrouded(ecs::Entity target) const
	{
		const auto *at = lookup.template Get<engine::gameplay::Transform>(target);
		return at != nullptr && shroud.StatusAt(local, at->position.x, at->position.y) != engine::gameplay::CellShroud::Clear;
	}
	// Object::isAboveTerrain.
	bool AboveTerrain(ecs::Entity entity) const
	{
		const auto *at = lookup.template Get<engine::gameplay::Transform>(entity);
		return at != nullptr && at->position.z - ground.At(at->position.XY()) > Engine::Math::Fixed{};
	}
	// OBJECT_STATUS_STEALTHED, not DETECTED, not DISGUISED.
	bool Hidden(ecs::Entity entity) const
	{
		const auto *stealth = lookup.template Get<engine::gameplay::Stealth>(entity);
		return stealth != nullptr && stealth->HiddenUndisguised();
	}
	// ActionManager's appearsToContainFriendlies: a container whose apparent controlling player to the unit's player (a
	// building held only by undetected stealthy garrisoners looks as it did, its original player's, to those its side is
	// not allied with) is not an enemy of the unit's team.
	bool AppearsToContainFriendlies(ecs::Entity unit, ecs::Entity target) const
	{
		if (lookup.template Get<engine::gameplay::Transport>(target) == nullptr)
			return false;
		const std::uint32_t viewer = PlayerOf(unit);
		std::uint32_t apparent = PlayerOf(target);
		if (const auto *garrison = lookup.template Get<engine::gameplay::Garrison>(target);
			garrison != nullptr && garrison->originalPlayer != engine::gameplay::Garrison::NoTeam && viewer < 32 && ((garrison->hiddenFrom >> viewer) & 1u) != 0)
			apparent = garrison->originalPlayer;
		return relationships.Between(TeamOf(unit), viewer, engine::gameplay::Relationships::NoTeam, apparent) != engine::gameplay::Relationship::Enemies;
	}
	// Object::hasSpecialPower / findSpecialPowerModuleInterface for a context power: its module (the first of that type).
	const engine::gameplay::SpecialPowerTimer *PowerOf(ecs::Entity entity, ContextPower power) const
	{
		const auto *timers = lookup.template Get<engine::gameplay::SpecialPowerTimers>(entity);
		if (timers == nullptr)
			return nullptr;
		for (std::uint32_t index = 0; index < timers->count; ++index)
			if (timers->timers[index].power < catalog.powers.size() && catalog.powers[timers->timers[index].power] == power)
				return &timers->timers[index];
		return nullptr;
	}
	// getPercentReady() >= 1.
	bool Ready(ecs::Entity entity, const engine::gameplay::SpecialPowerTimer *timer) const
	{
		return timer != nullptr && engine::gameplay::PeekPercentReady(*timer, rules, shared, PlayerOf(entity), catalog.tick) >= Engine::Math::Fixed::One();
	}
	// Its command set's context buttons (an upgrade's set in place of its own: getCommandSetString).
	std::span<const ContextButton> ButtonsOf(ecs::Entity entity) const
	{
		std::uint32_t set = SelectionLook::NoCarBomber;
		if (const auto *swapped = lookup.template Get<generalszh::gameplay::CommandSetOverride>(entity))
			set = swapped->id < catalog.overrideSets.size() ? catalog.overrideSets[swapped->id] : SelectionLook::NoCarBomber;
		else if (const SelectionLook *look = LookOf(entity))
			set = look->commandSet;
		return set < catalog.contextSets.size() ? std::span<const ContextButton>(catalog.contextSets[set]) : std::span<const ContextButton>{};
	}

	// ActionManager::canResumeConstructionOf: a DOZER of the structure's own player (the fork's fix), alive; the structure
	// under construction, its builder (getBuilderID) not alive at work building it, not fogged.
	bool CanResumeConstruction(ecs::Entity unit, ecs::Entity target) const
	{
		if (!Is(unit, context_kind::Dozer) || PlayerOf(unit) != PlayerOf(target))
			return false;
		const auto *building = lookup.template Get<engine::gameplay::UnderConstruction>(target);
		if (building == nullptr || Dead(unit))
			return false;
		if (const ecs::Entity builder = building->builder; lookup.IsAlive(builder) && !Dead(builder))
			if (const auto *task = lookup.template Get<engine::gameplay::Builder>(builder); task != nullptr && task->repair == 0 && task->target == target)
				return false;
		return !Shrouded(target);
	}

	// ActionManager::canTransferSuppliesAt: a supply truck (SupplyTruckAIUpdate) at a living dock, neither being built, the
	// dock not being sold; a warehouse with boxes left not an enemy's (its view), a supply centre of its own player with
	// boxes aboard; the truck available (a Chinook: empty, nobody getting out); the dock not shrouded (fog is fine).
	bool CanTransferSuppliesAt(ecs::Entity unit, ecs::Entity target) const
	{
		if (Dead(target) || Building(unit) || Building(target) || Sold(target))
			return false;
		const auto *truck = lookup.template Get<engine::gameplay::Harvester>(unit);
		if (truck == nullptr)
			return false;
		const auto *warehouse = lookup.template Get<engine::gameplay::ResourceStore>(target);
		const auto *center = lookup.template Get<engine::gameplay::ResourceDepot>(target);
		if (warehouse != nullptr && (warehouse->boxes == 0 || Relation(target, unit) == engine::gameplay::Relationship::Enemies))
			return false;
		if (center != nullptr && (truck->boxes == 0 || PlayerOf(target) != PlayerOf(unit)))
			return false;
		if (warehouse == nullptr && center == nullptr)
			return false;
		// ChinookAIUpdate::isAvailableForSupplying: a contain, nobody inside, none getting out.
		if (const SelectionLook *look = LookOf(unit); look != nullptr && look->chinook)
		{
			const auto *hold = lookup.template Get<engine::gameplay::Transport>(unit);
			if (hold == nullptr || hold->occupied != 0 || hold->state == engine::gameplay::TransportState::Unloading)
				return false;
		}
		const auto *at = lookup.template Get<engine::gameplay::Transform>(target);
		return at == nullptr || shroud.StatusAt(local, at->position.x, at->position.y) != engine::gameplay::CellShroud::Shrouded;
	}

	// ActionManager::canDockAt: the target has a dock (a DockUpdateInterface); supplies may be transferred there, or it is
	// a railed transport any VEHICLE or INFANTRY may dock with.
	bool CanDockAt(ecs::Entity unit, ecs::Entity target) const
	{
		if (lookup.template Get<engine::gameplay::Dock>(target) == nullptr)
			return false;
		if (CanTransferSuppliesAt(unit, target))
			return true;
		// RailedTransportDockUpdate (the ferry): VEHICLE or INFANTRY may dock with it.
		if (lookup.template Get<generalszh::gameplay::RailedTransport>(target) != nullptr)
			if (const SelectionLook *look = LookOf(unit))
				return (look->kinds & (select_kind::Vehicle | select_kind::Infantry)) != 0;
		return false;
	}

	// ActionManager::canRepairObject, and nobody else its sole healer now (getSoleHealingBenefactor): a DOZER, not inside
	// anything, at a STRUCTURE not an enemy's, alive, no bridge, bridge tower or rebuild hole, neither being built, hurt,
	// not fogged.
	bool CanRepairObject(ecs::Entity unit, ecs::Entity target) const
	{
		if (Relation(unit, target) == engine::gameplay::Relationship::Enemies || Dead(target))
			return false;
		if (Is(target, context_kind::Bridge) || Is(target, context_kind::BridgeTower) || Building(unit) || Building(target) || Is(target, context_kind::RebuildHole))
			return false;
		if (!Is(unit, context_kind::Dozer) || !Is(target, context_kind::Structure) || Whole(target) || Shrouded(target) || Contained(unit))
			return false;
		const auto *lock = lookup.template Get<engine::gameplay::HealLock>(target);
		return lock == nullptr || catalog.tick > lock->until || lock->healer == unit;
	}

	// ActionManager::canGetRepairedAt: an ally's REPAIR_PAD (an airborne aircraft: an FS_AIRFIELD) taking a hurt, living,
	// mobile VEHICLE; neither being built, the pad not sold, not fogged.
	bool CanGetRepairedAt(ecs::Entity unit, ecs::Entity target) const
	{
		if (Relation(unit, target) != engine::gameplay::Relationship::Allies || Dead(unit) || !Mobile(unit))
			return false;
		if (Building(unit) || Building(target) || Sold(target) || !Is(unit, context_kind::Vehicle))
			return false;
		if (Is(unit, context_kind::Aircraft) ? !AboveTerrain(unit) || !Is(target, context_kind::FsAirfield) : !Is(target, context_kind::RepairPad))
			return false;
		return !Whole(unit) && !Shrouded(target);
	}

	// ActionManager::canGetHealedAt (an ally's living HEAL_PAD taking a hurt INFANTRY; neither being built, the pad not
	// sold, not fogged), and the pad no HealContain (one that heals only who enters it is entered instead).
	bool CanGetHealedAt(ecs::Entity unit, ecs::Entity target) const
	{
		if (Relation(unit, target) != engine::gameplay::Relationship::Allies || Dead(target))
			return false;
		if (Building(unit) || Building(target) || Sold(target) || !Is(unit, context_kind::Infantry) || !Is(target, context_kind::HealPad))
			return false;
		if (Shrouded(target) || Whole(unit))
			return false;
		const SelectionLook *look = LookOf(target);
		return look == nullptr || look->contain != ContainKind::Heal;
	}

	// canHijackVehicle for a hijacker (its ConvertToHijackedVehicleCrateCollide): an enemy VEHICLE, alive, not fogged,
	// not AIRCRAFT, BOAT, DRONE or IMMUNE_TO_CAPTURE, with an AI, not HIJACKED, not a transport with anyone in it, of the
	// kinds its collide takes.
	bool CanHijack(ecs::Entity unit, ecs::Entity target) const
	{
		const SelectionLook *jacker = LookOf(unit);
		const content::KindOfMask *kinds = KindsOf(target);
		if (jacker == nullptr || jacker->hijacker == SelectionLook::NoCarBomber || kinds == nullptr || Relation(unit, target) != engine::gameplay::Relationship::Enemies)
			return false;
		const auto is = [&](std::string_view name) { return content::HasKindOf(*kinds, content::KindOfBit(name)); };
		if (!is("VEHICLE") || is("AIRCRAFT") || is("BOAT") || is("DRONE") || is("IMMUNE_TO_CAPTURE"))
			return false;
		if (Dead(target) || Shrouded(target))
			return false;
		static constexpr std::uint64_t hijacked = std::uint64_t{1} << content::ObjectStatusBit("HIJACKED");
		if (const auto *flags = lookup.template Get<engine::gameplay::StatusFlags>(target); flags != nullptr && (flags->bits & hijacked) != 0)
			return false;
		if (const auto *carrier = lookup.template Get<engine::gameplay::Transport>(target); carrier != nullptr && is("TRANSPORT") && carrier->occupied > 0)
			return false;
		const CarBomberKinds &wants = catalog.hijackers[jacker->hijacker];
		for (std::size_t word = 0; word < kinds->size(); ++word)
			if (((*kinds)[word] & wants.required[word]) != wants.required[word] || ((*kinds)[word] & wants.forbidden[word]) != 0)
				return false;
		return true;
	}

	// canConvertObjectToCarBomb for a car bomber (its ConvertToCarBombCrateCollide): a living vehicle, not fogged, that may
	// be made a car bomb (with an AI, not AIRCRAFT or BOAT, a CARBOMB weapon set not in use, not IS_CARBOMB) of the kinds
	// its collide takes.
	bool CanConvertToCarBomb(ecs::Entity unit, ecs::Entity target) const
	{
		const SelectionLook *look = LookOf(target);
		const SelectionLook *bomber = LookOf(unit);
		const content::KindOfMask *kinds = KindsOf(target);
		if (look == nullptr || !look->carBombable || kinds == nullptr || bomber == nullptr || bomber->carBomber == SelectionLook::NoCarBomber || unit == target)
			return false;
		if (Dead(target) || Shrouded(target))
			return false;
		static constexpr std::uint64_t carBombStatus = std::uint64_t{1} << content::ObjectStatusBit("IS_CARBOMB");
		static const std::uint32_t carBombFlag = content::SetFlag(content::WeaponSetFlagNames, "CARBOMB");
		if (const auto *flags = lookup.template Get<engine::gameplay::StatusFlags>(target); flags != nullptr && (flags->bits & carBombStatus) != 0)
			return false;
		if (const auto *loadout = lookup.template Get<engine::gameplay::Loadout>(target); loadout != nullptr && (loadout->weaponFlags & carBombFlag) != 0)
			return false;
		const CarBomberKinds &wants = catalog.carBombers[bomber->carBomber];
		for (std::size_t word = 0; word < kinds->size(); ++word)
			if (((*kinds)[word] & wants.required[word]) != wants.required[word] || ((*kinds)[word] & wants.forbidden[word]) != 0)
				return false;
		return true;
	}

	// A saboteur's Sabotage*CrateCollide would like to collide with the building (wouldLikeToCollideWith: CrateCollide's
	// isValidToExecute, then its own): with an AI or a STRUCTURE it picks up as a building, of its kinds, alive, the
	// saboteur on the ground unless picking up a building, not its own player's where ForbidOwnerPlayer, no PARACHUTE; a
	// building of the collide's kind (Sabotage*CrateCollide::isValidToExecute), not being built or sold, an enemy's.
	bool SabotageCollides(ecs::Entity unit, ecs::Entity target) const
	{
		const SelectionLook *look = LookOf(unit);
		const SelectionLook *building = LookOf(target);
		const content::KindOfMask *kinds = KindsOf(target);
		if (look == nullptr || building == nullptr || kinds == nullptr || look->saboteurCount == 0)
			return false;
		const auto is = [&](std::string_view name) { return content::HasKindOf(*kinds, content::KindOfBit(name)); };
		for (std::uint32_t index = look->saboteur; index < look->saboteur + look->saboteurCount; ++index)
		{
			const SaboteurCollide &collide = catalog.saboteurs[index];
			const bool pickup = collide.buildingPickup && is("STRUCTURE");
			if (!building->hasAi && !pickup)
				continue;
			bool kindsMatch = true;
			for (std::size_t word = 0; word < kinds->size(); ++word)
				kindsMatch = kindsMatch && ((*kinds)[word] & collide.required[word]) == collide.required[word] && ((*kinds)[word] & collide.forbidden[word]) == 0;
			if (!kindsMatch || Dead(target) || (!pickup && AboveTerrain(unit)) || (collide.forbidOwner && PlayerOf(unit) == PlayerOf(target)) || is("PARACHUTE"))
				continue;
			bool ofKind = false;
			switch (collide.kind)
			{
			case content::SabotageKind::PowerPlant: ofKind = is("FS_POWER"); break;
			case content::SabotageKind::SupplyDropzone: ofKind = is("FS_SUPPLY_DROPZONE"); break;
			case content::SabotageKind::Superweapon: ofKind = is("FS_SUPERWEAPON") || is("FS_STRATEGY_CENTER"); break;
			case content::SabotageKind::CommandCenter: ofKind = is("COMMANDCENTER"); break;
			case content::SabotageKind::SupplyCenter: ofKind = is("FS_SUPPLY_CENTER"); break;
			case content::SabotageKind::MilitaryFactory: ofKind = !is("AIRCRAFT_CARRIER") && (is("FS_BARRACKS") || is("FS_WARFACTORY") || is("FS_AIRFIELD")); break;
			case content::SabotageKind::FakeBuilding: ofKind = is("FS_FAKE"); break;
			case content::SabotageKind::InternetCenter: ofKind = is("FS_INTERNET_CENTER"); break;
			}
			if (ofKind && !Building(target) && !Sold(target) && Relation(unit, target) == engine::gameplay::Relationship::Enemies)
				return true;
		}
		return false;
	}

	// ActionManager::canSabotageBuilding: alive, not fogged, an enemy's, and a sabotage collide of the unit's would take it.
	bool CanSabotage(ecs::Entity unit, ecs::Entity target) const
	{
		return !Dead(target) && !Shrouded(target) && Relation(unit, target) == engine::gameplay::Relationship::Enemies && SabotageCollides(unit, target);
	}

	// A pilot's VeterancyCrateCollide would like to collide with the vehicle (CrateCollide / VeterancyCrateCollide::
	// isValidToExecute, as PilotSeekSystem joins).
	bool PilotJoins(ecs::Entity unit, ecs::Entity target) const
	{
		const auto *seeker = lookup.template Get<generalszh::gameplay::PilotSeeker>(unit);
		if (seeker == nullptr || !lookup.IsAlive(target) || lookup.template Get<engine::gameplay::Dying>(target) != nullptr ||
			lookup.template Get<engine::gameplay::OffMap>(target) != nullptr)
			return false;
		const content::KindOfMask *kinds = KindsOf(target);
		const SelectionLook *look = LookOf(target);
		const auto *place = lookup.template Get<engine::gameplay::Transform>(target);
		if (kinds == nullptr || look == nullptr || place == nullptr || !look->hasAi || lookup.template Get<engine::gameplay::Owner>(target) == nullptr)
			return false;
		for (std::size_t word = 0; word < seeker->required.size(); ++word)
			if (((*kinds)[word] & seeker->required[word]) != seeker->required[word] || ((*kinds)[word] & seeker->forbidden[word]) != 0)
				return false;
		if (const auto *health = lookup.template Get<engine::gameplay::Health>(target); health != nullptr && engine::gameplay::IsDead(*health))
			return false;
		if (place->position.z - ground.At(place->position.XY()) > physics.SignificantHeight())
			return false;
		const auto *own = lookup.template Get<engine::gameplay::Experience>(unit);
		const std::uint32_t levels = seeker->addsOwnerVeterancy != 0 ? (own != nullptr ? own->level : 0u) : 1u;
		const auto *experience = lookup.template Get<engine::gameplay::Experience>(target);
		if (levels == 0 || experience == nullptr || !experience->trainable || experience->level + 1u >= engine::gameplay::VeterancyLevelCount)
			return false;
		if (seeker->isPilot != 0)
		{
			if (PlayerOf(target) != PlayerOf(unit))
				return false;
			if (const auto *motion = lookup.template Get<engine::gameplay::Locomotion>(target); motion != nullptr && engine::gameplay::IsAirborne(motion->locomotor))
				return false;
		}
		return true;
	}

	// ParkingPlaceBehavior::hasAvailableSpaceFor: a space of the airfield no jet holds.
	bool FreeSpaceAt(ecs::Entity airfield) const
	{
		const auto *field = lookup.template Get<engine::gameplay::Airfield>(airfield);
		if (field == nullptr)
			return false;
		std::array<bool, engine::gameplay::Airfield::MaxSpaces> taken{};
		jets.ForEachChunk([&](auto chunk) {
			for (const engine::gameplay::Jet &jet : chunk.template Get<engine::gameplay::Jet>())
				if (jet.airfield == airfield && jet.space < field->spaceCount)
					taken[jet.space] = true;
		});
		for (std::uint32_t space = 0; space < field->spaceCount; ++space)
			if (!taken[space])
				return true;
		return false;
	}

	// Who is inside (getContainCount; a tunnel: its whole network) and how many of them are stealthy garrisoners
	// (getStealthUnitsContained: none in a tunnel's own list).
	std::uint32_t ContainCount(ecs::Entity target, const SelectionLook &look) const
	{
		if (look.contain == ContainKind::Tunnel || look.contain == ContainKind::Cave)
		{
			const auto network = manifest.NetworkOf(target);
			return network ? static_cast<std::uint32_t>(manifest.NetworkCount(*network)) : 0u;
		}
		return static_cast<std::uint32_t>(manifest.Aboard(target).size());
	}
	std::uint32_t StealthContained(ecs::Entity target, const SelectionLook &look) const
	{
		if (look.contain == ContainKind::Tunnel || look.contain == ContainKind::Cave)
			return 0;
		std::uint32_t count = 0;
		for (const ecs::Entity rider : manifest.Aboard(target))
			count += Is(rider, context_kind::StealthGarrison) ? 1u : 0u;
		return count;
	}

	// OpenContain::isValidContainerFor: the rider of AllowInsideKindOf (any when none given) and none of
	// ForbidInsideKindOf, and allowed in as it stands to the container (its view).
	bool OpenAllows(ecs::Entity container, const SelectionLook &look, ecs::Entity rider) const
	{
		const content::KindOfMask *kinds = KindsOf(rider);
		if (kinds == nullptr)
			return false;
		bool any = look.anyInside, forbidden = false;
		for (std::size_t word = 0; word < kinds->size(); ++word)
		{
			any = any || ((*kinds)[word] & look.allowInside[word]) != 0;
			forbidden = forbidden || ((*kinds)[word] & look.forbidInside[word]) != 0;
		}
		if (!any || forbidden)
			return false;
		switch (Relation(rider, container))
		{
		case engine::gameplay::Relationship::Allies: return look.alliesInside;
		case engine::gameplay::Relationship::Enemies: return look.enemiesInside;
		default: return look.neutralInside;
		}
	}

	// TransportContain::isValidContainerFor: OpenContain's, the rider its own player's with a TransportSlotCount, and
	// (checking room) its slots free.
	bool TransportAllows(ecs::Entity container, const SelectionLook &look, ecs::Entity rider, bool checkCapacity) const
	{
		const SelectionLook *riderLook = LookOf(rider);
		if (!OpenAllows(container, look, rider) || PlayerOf(rider) != PlayerOf(container) || riderLook == nullptr || riderLook->transportSlots == 0)
			return false;
		const auto *hold = lookup.template Get<engine::gameplay::Transport>(container);
		return !checkCapacity || (hold != nullptr && hold->occupied + riderLook->transportSlots <= hold->definition.slots);
	}

	// ContainModuleInterface::isValidContainerFor, per contain module.
	bool ValidContainerFor(ecs::Entity container, ecs::Entity rider, bool checkCapacity) const
	{
		const SelectionLook *look = LookOf(container);
		if (look == nullptr)
			return false;
		const auto *hold = lookup.template Get<engine::gameplay::Transport>(container);
		switch (look->contain)
		{
		case ContainKind::None: return false;
		case ContainKind::Open:
		case ContainKind::Heal: return OpenAllows(container, *look, rider);
		case ContainKind::Transport: return TransportAllows(container, *look, rider, checkCapacity);
		// HelixContain: a PORTABLE_STRUCTURE while it carries none; else a transport's rules.
		case ContainKind::Helix:
		{
			const auto *mount = lookup.template Get<engine::gameplay::Mount>(container);
			if (Is(rider, context_kind::PortableStructure) && (mount == nullptr || !lookup.IsAlive(mount->rider)))
				return true;
			return TransportAllows(container, *look, rider, checkCapacity);
		}
		// OverlordContain: its portable structure's contain answers (getRedirectedContain), else a transport's rules.
		case ContainKind::Overlord:
			if (const auto *mount = lookup.template Get<engine::gameplay::Mount>(container);
				mount != nullptr && lookup.IsAlive(mount->rider) && lookup.template Get<engine::gameplay::Transport>(mount->rider) != nullptr)
				return ValidContainerFor(mount->rider, rider, checkCapacity);
			return TransportAllows(container, *look, rider, checkCapacity);
		// RiderChangeContain: a transport's rules without room (its rider kicks the other out), not scuttled, one of its riders.
		case ContainKind::RiderChange:
		{
			const auto *change = lookup.template Get<generalszh::gameplay::RiderChange>(container);
			const auto *ref = lookup.template Get<engine::gameplay::DefinitionRef>(rider);
			if (!TransportAllows(container, *look, rider, false) || change == nullptr || change->scuttledTick != 0 || ref == nullptr)
				return false;
			for (std::uint32_t slot = 0; slot < change->count; ++slot)
				if (change->definitions[slot] == ref->index)
					return true;
			return false;
		}
		// MobNexusContain: OpenContain's, the rider an ally of it (its view), with a TransportSlotCount, room for it.
		case ContainKind::MobNexus:
		{
			const SelectionLook *riderLook = LookOf(rider);
			if (!OpenAllows(container, *look, rider) || Relation(rider, container) != engine::gameplay::Relationship::Allies || riderLook == nullptr ||
				riderLook->transportSlots == 0)
				return false;
			return !checkCapacity || (hold != nullptr && hold->occupied + riderLook->transportSlots <= hold->definition.slots);
		}
		// GarrisonContain: OpenContain's; not with no health, nor really damaged unless GARRISONABLE_UNTIL_DESTROYED (the
		// container closed); no NO_GARRISON rider; a place free.
		case ContainKind::Garrison:
		{
			const auto *health = lookup.template Get<engine::gameplay::Health>(container);
			if (!OpenAllows(container, *look, rider) || health == nullptr || health->current <= Engine::Math::Fixed{} || hold == nullptr || hold->closed ||
				Is(rider, context_kind::NoGarrison))
				return false;
			return !checkCapacity || ContainCount(container, *look) < hold->definition.slots;
		}
		// TunnelTracker::isValidContainerFor: no AIRCRAFT; a place free in the network.
		case ContainKind::Tunnel:
		case ContainKind::Cave:
			if (Is(rider, context_kind::Aircraft))
				return false;
			return !checkCapacity || (hold != nullptr && ContainCount(container, *look) < hold->definition.slots);
		}
		return false;
	}

	// ActionManager::canEnterObject (CHECK_CAPACITY when `checkCapacity`, else DONT_CHECK_CAPACITY).
	bool CanEnter(ecs::Entity unit, ecs::Entity target, bool checkCapacity) const
	{
		if (unit == target || Dead(target) || Shrouded(target))
			return false;
		if (Building(unit) || Building(target) || Sold(target))
			return false;
		if (Is(unit, context_kind::IgnoredInGui) || Is(unit, context_kind::MobNexus) || Is(target, context_kind::IgnoredInGui))
			return false;
		if (DisabledBy(target, engine::gameplay::disabled_type::Subdued))
			return false;
		if (Is(unit, context_kind::Structure) || Is(unit, context_kind::Immobile))
			return false;
		// Any infantry may take over an unmanned vehicle (unless REJECT_UNMANNED).
		if (Is(unit, context_kind::Infantry) && DisabledBy(target, engine::gameplay::disabled_type::Unmanned) && !Is(unit, context_kind::RejectUnmanned))
			return true;
		// An aircraft lands only at its own player's airfields, airborne: where it holds a space, or one is free for it.
		// (The carrier deck's DECK_HEIGHT_OFFSET test is not ported.)
		if (Is(unit, context_kind::Aircraft) && Is(target, context_kind::FsAirfield))
		{
			if (!AboveTerrain(unit) || PlayerOf(unit) != PlayerOf(target) || lookup.template Get<engine::gameplay::Airfield>(target) == nullptr)
				return false;
			if (const auto *jet = lookup.template Get<engine::gameplay::Jet>(unit); jet != nullptr && jet->airfield == target)
				return true;
			return !Is(unit, context_kind::ProducedAtHelipad) && FreeSpaceAt(target);
		}
		// Its collides' wish (a hijacker, car bomber, saboteur or pilot).
		if (CanHijack(unit, target) || CanConvertToCarBomb(unit, target) || SabotageCollides(unit, target) || PilotJoins(unit, target))
			return true;
		const SelectionLook *look = LookOf(target);
		if (look == nullptr || look->contain == ContainKind::None)
			return false;
		if (look->contain == ContainKind::Heal && Whole(unit))
			return false;
		const std::uint32_t count = ContainCount(target, *look), stealthy = StealthContained(target, *look), plain = count - std::min(count, stealthy);
		if (PlayerOf(target) != PlayerOf(unit))
		{
			if (plain > 0 || FactionStructure(target))
				return false;
			if (stealthy > 0 && plain == 0)
				checkCapacity = false;
		}
		const SelectionLook *self = LookOf(unit);
		if (checkCapacity && (self == nullptr || self->transportSlots == 0))
			return false;
		return ValidContainerFor(target, unit, checkCapacity);
	}

	// ActionManager::canCaptureBuilding: its capture power (the infantry's, else the Black Lotus') fully ready; a
	// STRUCTURE not IMMUNE_TO_CAPTURE, alive, not being built or sold, not fogged; an enemy's, or a CAPTURABLE one not an
	// ally's; not hidden; not garrisoned but by stealthy garrisoners; not seeming to hold friends.
	bool CanCapture(ecs::Entity unit, ecs::Entity target) const
	{
		const auto *power = PowerOf(unit, ContextPower::InfantryCapture);
		if (power == nullptr)
			power = PowerOf(unit, ContextPower::BlackLotusCapture);
		if (!Ready(unit, power) || Is(target, context_kind::ImmuneToCapture) || Dead(target) || !Is(target, context_kind::Structure))
			return false;
		if (Building(target) || Sold(target) || Shrouded(target))
			return false;
		const engine::gameplay::Relationship relation = Relation(unit, target);
		if (!(relation == engine::gameplay::Relationship::Enemies || (Is(target, context_kind::Capturable) && relation != engine::gameplay::Relationship::Allies)))
			return false;
		if (Hidden(target))
			return false;
		if (const SelectionLook *look = LookOf(target); look != nullptr && look->contain == ContainKind::Garrison)
		{
			const std::uint32_t count = ContainCount(target, *look);
			if (count - std::min(count, StealthContained(target, *look)) > 0)
				return false;
		}
		return !AppearsToContainFriendlies(unit, target);
	}

	// ActionManager::canDisableVehicleViaHacking: its vehicle hack fully ready; alive, no AIRCRAFT nor an airborne target,
	// not fogged; an enemy's VEHICLE, not hidden, not seeming to hold friends.
	bool CanDisableVehicle(ecs::Entity unit, ecs::Entity target) const
	{
		if (!Ready(unit, PowerOf(unit, ContextPower::DisableVehicleHack)) || Dead(target))
			return false;
		const auto *body = lookup.template Get<engine::gameplay::Targetable>(target);
		if (Is(target, context_kind::Aircraft) ||
			(body != nullptr && (body->classes & (engine::gameplay::target_class::AirborneVehicle | engine::gameplay::target_class::AirborneInfantry)) != 0))
			return false;
		if (Shrouded(target) || Relation(unit, target) != engine::gameplay::Relationship::Enemies || !Is(target, context_kind::Vehicle))
			return false;
		return !Hidden(target) && !AppearsToContainFriendlies(unit, target);
	}

	// ActionManager::canStealCashViaHacking: its cash hack fully ready; alive, not being built, not fogged; an enemy's
	// CASH_GENERATOR, CAPTURABLE and no REBUILD_HOLE, not hidden, not seeming to hold friends.
	bool CanStealCash(ecs::Entity unit, ecs::Entity target) const
	{
		if (!Ready(unit, PowerOf(unit, ContextPower::StealCashHack)) || Dead(target) || Building(target) || Shrouded(target))
			return false;
		if (Relation(unit, target) != engine::gameplay::Relationship::Enemies || !Is(target, context_kind::CashGenerator))
			return false;
		if (!Is(target, context_kind::Capturable) || Is(target, context_kind::RebuildHole))
			return false;
		return !Hidden(target) && !AppearsToContainFriendlies(unit, target);
	}

	// ActionManager::canDisableBuildingViaHacking: its building hack fully ready; alive, not fogged; an enemy's STRUCTURE,
	// CAPTURABLE and no rebuild hole (or a technology building not IMMUNE_TO_CAPTURE), not a rebuild hole nor being
	// built, not hidden, not seeming to hold friends.
	bool CanDisableBuilding(ecs::Entity unit, ecs::Entity target) const
	{
		if (!Ready(unit, PowerOf(unit, ContextPower::DisableBuildingHack)) || Dead(target) || Shrouded(target))
			return false;
		if (Relation(unit, target) != engine::gameplay::Relationship::Enemies || !Is(target, context_kind::Structure))
			return false;
		if ((!Is(target, context_kind::Capturable) || Is(target, context_kind::RebuildHole)) &&
			!(Is(target, context_kind::FsTechnology) && !Is(target, context_kind::ImmuneToCapture)))
			return false;
		if (Is(target, context_kind::RebuildHole) || Building(target))
			return false;
		return !Hidden(target) && !AppearsToContainFriendlies(unit, target);
	}
};
}

struct PointerInteractionSystem
{
	// The jets (which parking spaces are held: canEnterObject for an aircraft at an airfield).
	using Query = ecs::Query<ecs::Read<engine::gameplay::Jet>>;
	using SideTables = ecs::SideTables<ecs::Write<Selected>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Targetable>, ecs::Read<engine::gameplay::OffMap>,
		ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::Armament>, ecs::Read<engine::gameplay::WeaponSlots>,
		ecs::Read<engine::gameplay::Slaved>, ecs::Read<engine::gameplay::ScriptStatus>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Read<engine::gameplay::StatusFlags>, ecs::Read<engine::gameplay::Loadout>, ecs::Read<generalszh::gameplay::ParticleCannon>,
		ecs::Read<generalszh::gameplay::SpectreGunship>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::WeaponBonusConditions>,
		ecs::Read<engine::gameplay::Transport>, ecs::Read<engine::gameplay::BodyExtent>, ecs::Read<engine::gameplay::Subdual>,
		ecs::Read<engine::gameplay::UnderConstruction>, ecs::Read<engine::gameplay::Dying>, ecs::Read<engine::gameplay::InactiveBody>,
		ecs::Read<engine::gameplay::Sale>, ecs::Read<engine::gameplay::Builder>, ecs::Read<engine::gameplay::TeamMember>, ecs::Read<engine::gameplay::Disabled>,
		ecs::Read<engine::gameplay::Stealth>, ecs::Read<engine::gameplay::Garrison>, ecs::Read<engine::gameplay::Mount>,
		ecs::Read<generalszh::gameplay::RiderChange>, ecs::Read<engine::gameplay::Passenger>, ecs::Read<engine::gameplay::Harvester>,
		ecs::Read<engine::gameplay::ResourceStore>, ecs::Read<engine::gameplay::ResourceDepot>, ecs::Read<engine::gameplay::Dock>,
		ecs::Read<generalszh::gameplay::RailedTransport>,
		ecs::Read<engine::gameplay::HealLock>, ecs::Read<engine::gameplay::SpecialPowerTimers>, ecs::Read<engine::gameplay::Jet>,
		ecs::Read<engine::gameplay::Airfield>, ecs::Read<engine::gameplay::Experience>, ecs::Read<engine::gameplay::Locomotion>,
		ecs::Read<generalszh::gameplay::PilotSeeker>, ecs::Read<generalszh::gameplay::CommandSetOverride>>;
	using Resources = ecs::Resources<ecs::Read<PointerInput>, ecs::Read<InteractionView>, ecs::Write<InteractionState>, ecs::Read<MouseSettings>,
		ecs::Write<SelectionBox>, ecs::Read<SelectionCatalog>, ecs::Read<LocalPlayer>, ecs::Write<PlayerOrders>, ecs::Read<engine::gameplay::VisibleObjects>, ecs::Read<PointerHits>,
		ecs::Read<engine::gameplay::Relationships>, ecs::Read<engine::gameplay::WeaponCatalog>, ecs::Read<engine::gameplay::ArmorCatalog>, ecs::Read<engine::gameplay::GroundHeight>,
		ecs::Read<generalszh::gameplay::Deselections>, ecs::Read<engine::gameplay::ShroudMap>, ecs::Write<CursorState>, ecs::Read<BuildPlacement>, ecs::Write<GuiTargeting>,
		ecs::Read<engine::gameplay::CargoManifest>, ecs::Read<engine::gameplay::SpecialPowerRules>, ecs::Read<engine::gameplay::SharedPowerTimers>,
		ecs::Read<engine::gameplay::PhysicsSettings>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		using namespace interaction_detail;
		// GameLogic::deselectObject: what the logic took out of every selection.
		for (const ecs::Entity entity : context.Read<generalszh::gameplay::Deselections>().list)
			context.Side<SideTables, Selected>().Erase(entity);
		const PointerInput &pointer = context.Read<PointerInput>();
		const InteractionView &view = context.Read<InteractionView>();
		InteractionState &state = context.Write<InteractionState>();
		const MouseSettings &mouse = context.Read<MouseSettings>();
		SelectionBox &box = context.Write<SelectionBox>();
		const LocalPlayer &local = context.Read<LocalPlayer>();
		auto &selected = context.Side<SideTables, Selected>();
		if (!view.valid || !local.valid)
			return;

		// Left button: anchor, drag, release.
		if ((pointer.pressed & pointer_button::Left) != 0 && !pointer.overInterface)
		{
			state.leftDown = true;
			state.leftAnchorX = pointer.x;
			state.leftAnchorY = pointer.y;
		}
		if (state.leftDown && !state.dragSelecting &&
			(std::abs(pointer.x - state.leftAnchorX) > mouse.dragTolerance || std::abs(pointer.y - state.leftAnchorY) > mouse.dragTolerance))
			state.dragSelecting = true;
		box = state.dragSelecting ? SelectionBox{true, state.leftAnchorX, state.leftAnchorY, pointer.x, pointer.y} : SelectionBox{};
		if ((pointer.released & pointer_button::Left) != 0 && state.leftDown)
		{
			state.leftDown = false;
			state.dragSelecting = false;
			box = {};
			const bool isPoint = IsPointClick(state.leftAnchorX, state.leftAnchorY, pointer.x, pointer.y, mouse.dragTolerance);
			// With a command waiting for its target, nothing is selected (currentlyLookingForSelection); a point click
			// on a valid target gives it (handleGuiCommand, DO_COMMAND) and ends the wait.
			if (context.Read<GuiTargeting>().active)
			{
				if (isPoint)
					GuiClick(context, pointer);
			}
			else
				LeftClick(query, context, pointer, isPoint);
		}

		// Right button: a click deselects all.
		if ((pointer.pressed & pointer_button::Right) != 0 && !pointer.overInterface)
		{
			state.rightDown = true;
			state.rightAnchorX = pointer.x;
			state.rightAnchorY = pointer.y;
			state.rightDownTimeMs = pointer.timeMs;
			state.rightDownCamera = view.eye;
		}
		if ((pointer.released & pointer_button::Right) != 0 && state.rightDown)
		{
			state.rightDown = false;
			const float dx = pointer.x - state.rightAnchorX, dy = pointer.y - state.rightAnchorY;
			float moved = 0.0f;
			for (std::size_t axis = 0; axis < 3; ++axis)
				moved += (view.eye[axis] - state.rightDownCamera[axis]) * (view.eye[axis] - state.rightDownCamera[axis]);
			const bool click = pointer.timeMs - state.rightDownTimeMs <= mouse.dragToleranceMs && dx * dx + dy * dy <= mouse.dragTolerance * mouse.dragTolerance &&
				moved <= mouse.dragTolerance3D * mouse.dragTolerance3D;
			// A right click gives a waiting command up without deselecting (onRawMouseRightButtonUp); else deselects all.
			if (click)
			{
				if (GuiTargeting &targeting = context.Write<GuiTargeting>(); targeting.active)
					targeting = {};
				else
					selected.Clear();
			}
		}

		HoverCursor(query, context, pointer);
	}

	// W3DMouse::setCursorDirection: of a cursor's `directions` (frame 0 pointing right, then clockwise on the screen),
	// the one nearest the scroll offset's direction; 0 without an offset.
	static std::uint8_t ScrollDirection(float x, float y, int directions) noexcept
	{
		if (directions <= 1 || (x == 0.0f && y == 0.0f))
			return 0;
		constexpr float turn = 2.0f * std::numbers::pi_v<float>;
		const float theta = std::fmod(std::atan2(y, x) + turn, turn);
		const int frame = static_cast<int>(theta / (turn / static_cast<float>(directions)) + 0.5f);
		return static_cast<std::uint8_t>(frame >= directions ? 0 : frame);
	}

	// What a click would do (CommandTranslator::evaluateContextCommand, or evaluateForceAttack when force-attacking),
	// as the message it would post: nothing (MSG_INVALID), or a hint of the command.
	enum class Hint : std::uint8_t
	{
		Invalid,
		OverrideDestination, // MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION
		ResumeConstruction,  // MSG_RESUME_CONSTRUCTION (handleResumeConstructionCommand)
		Dock,                // MSG_DOCK (handleDockAtCommand)
		Repair,              // MSG_DO_REPAIR (handleRepairObjectCommand)
		GetRepaired,         // MSG_GET_REPAIRED
		GetHealed,           // MSG_GET_HEALED (handleGetHealedAtCommand)
		Hijack,              // MSG_HIJACK_HINT: an enter (createEnterMessage)
		ConvertToCarBomb,    // MSG_CONVERT_TO_CARBOMB
		Sabotage,            // MSG_SABOTAGE_HINT: an enter (handleSabotageBuildingCommand)
		Salvage,             // MSG_DO_SALVAGE: a move to the crate (handleSalvageCommand)
		Enter,               // MSG_ENTER (handleEnterObjectCommand)
		CaptureBuilding,     // MSG_CAPTUREBUILDING_HINT (a hint only)
		Hack,                // MSG_HACK_HINT (a hint only)
		SpecialPower,        // handleCaptureBuildingCommand / handleHackCommand's issueSpecialPowerCommand (a command only)
		AttackObject,        // MSG_DO_ATTACK_OBJECT
		AttackAfterMoving,   // MSG_DO_ATTACK_OBJECT_AFTER_MOVING
		ImpossibleAttack,    // MSG_IMPOSSIBLE_ATTACK
		ForceAttackObject,   // MSG_DO_FORCE_ATTACK_OBJECT
		ForceAttackGround,   // MSG_DO_FORCE_ATTACK_GROUND
		SetRallyPoint,       // MSG_SET_RALLY_POINT (handleSetRallyPointCommand)
		Move,                // handleDefaultMoveCommand (a move only onto the ground)
	};

private:
	template<typename Lookup>
	static std::optional<PickedObject> Classify(ecs::Entity entity, const Lookup &lookup, const SelectionCatalog &catalog, std::uint32_t definition,
		std::uint32_t player, const engine::gameplay::Relationships &relationships, std::uint32_t local)
	{
		const SelectionLook *look = catalog.Of(definition);
		if (look == nullptr)
			return std::nullopt;
		PickedObject picked;
		picked.entity = entity;
		if (player == local)
			picked.side = PickSide::Mine;
		else
			switch (relationships.Between(local, player))
			{
			case engine::gameplay::Relationship::Allies: picked.side = PickSide::Friend; break;
			case engine::gameplay::Relationship::Enemies: picked.side = PickSide::Enemy; break;
			default: picked.side = PickSide::Civilian; break;
			}
		picked.structure = (look->kinds & select_kind::Structure) != 0;
		picked.infantry = (look->kinds & select_kind::Infantry) != 0;
		picked.crate = (look->kinds & select_kind::Crate) != 0;
		picked.contained = lookup.template Get<engine::gameplay::OffMap>(entity) != nullptr;
		return picked;
	}

	// Drawable::isSelectable with addDrawableToList's kinds: SELECTABLE (or ALWAYS_SELECTABLE, FORCEATTACKABLE in
	// force-attack mode), alive unless ALWAYS_SELECTABLE, not UNSELECTABLE (an enslaved drone), not carried.
	template<typename Lookup>
	static bool Pickable(ecs::Entity entity, const SelectionLook &look, const Lookup &lookup, bool forceAttack)
	{
		const std::uint16_t kinds = select_kind::Selectable | select_kind::AlwaysSelectable | (forceAttack ? select_kind::ForceAttackable : 0);
		if ((look.kinds & kinds) == 0)
			return false;
		if (const auto *health = lookup.template Get<engine::gameplay::Health>(entity); health != nullptr && engine::gameplay::IsDead(*health) &&
			(look.kinds & select_kind::AlwaysSelectable) == 0)
			return false;
		if (const auto *slave = lookup.template Get<engine::gameplay::Slaved>(entity); slave != nullptr && slave->enslaved != 0)
			return false;
		if (lookup.template Get<engine::gameplay::OffMap>(entity) != nullptr)
			return false;
		return true;
	}

public:
	// The ground under a pixel (W3DView::screenToTerrain): along the ray until below the ground, then halved in.
	static std::optional<std::array<float, 3>> GroundUnder(const InteractionView &view, const engine::gameplay::GroundHeight &ground, float sx, float sy)
	{
		const auto ray = view.Ray(sx, sy);
		const auto height = [&](float t) {
			const float x = view.eye[0] + ray[0] * t, y = view.eye[1] + ray[1] * t;
			return view.eye[2] + ray[2] * t - Engine::Math::ToFloat(ground.At({interaction_detail::ToFixed(x), interaction_detail::ToFixed(y)}));
		};
		float previous = 0.0f;
		for (float t = 2.0f; t < 8000.0f; t += 4.0f)
		{
			if (height(t) > 0.0f)
			{
				previous = t;
				continue;
			}
			float low = previous, high = t;
			for (int step = 0; step < 16; ++step)
			{
				const float middle = (low + high) * 0.5f;
				(height(middle) > 0.0f ? low : high) = middle;
			}
			return std::array<float, 3>{view.eye[0] + ray[0] * high, view.eye[1] + ray[1] * high, view.eye[2] + ray[2] * high};
		}
		return std::nullopt;
	}

private:
	// W3DView::pickDrawable at the pointer: the nearest object the pick ray meets on its drawn geometry (PointerHits,
	// cast by the models) whose pick type the click takes (getPickTypesForContext: selectable; force-attackable in
	// force-attack mode), as RTS3DScene::castRay keeps the nearest of those whose collision type matches.
	template<typename Lookup>
	static std::optional<interaction_detail::Candidate> PickAt(ecs::SystemContext &context, const Lookup &lookup, float x, float y, bool forceAttack)
	{
		using namespace interaction_detail;
		const PointerHits &hits = context.Read<PointerHits>();
		if (hits.x != x || hits.y != y)
			return std::nullopt;
		const SelectionCatalog &catalog = context.Read<SelectionCatalog>();
		const std::uint32_t local = context.Read<LocalPlayer>().player;
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		const std::uint8_t wanted = static_cast<std::uint8_t>(pick_type::Selectable | (forceAttack ? pick_type::ForceAttackable : 0));
		for (const PointerHit &hit : hits.hits)
		{
			const ecs::Entity entity{static_cast<std::uint32_t>(hit.key >> 32), static_cast<std::uint32_t>(hit.key & 0xFFFFFFFFu)};
			const auto *reference = lookup.template Get<engine::gameplay::DefinitionRef>(entity);
			const auto *owner = lookup.template Get<engine::gameplay::Owner>(entity);
			const auto *transform = lookup.template Get<engine::gameplay::Transform>(entity);
			if (reference == nullptr || owner == nullptr || transform == nullptr)
				continue;
			const SelectionLook *look = catalog.Of(reference->index);
			if (look == nullptr)
				continue;
			const auto *health = lookup.template Get<engine::gameplay::Health>(entity);
			const bool dead = health != nullptr && engine::gameplay::IsDead(*health);
			if ((PickTypes(look->kinds, dead) & wanted) == 0 || !Selectable(entity, lookup))
				continue;
			const auto picked = Classify(entity, lookup, catalog, reference->index, owner->player, relationships, local);
			if (!picked)
				continue;
			return Candidate{entity, hit.fraction, *picked, Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y), reference->index};
		}
		return std::nullopt;
	}

	// Drawable::setSelectable(FALSE)'s cases: an enslaved drone (OBJECT_STATUS_UNSELECTABLE), one carried inside
	// another.
	template<typename Lookup>
	static bool Selectable(ecs::Entity entity, const Lookup &lookup)
	{
		if (const auto *slave = lookup.template Get<engine::gameplay::Slaved>(entity); slave != nullptr && slave->enslaved != 0)
			return false;
		return lookup.template Get<engine::gameplay::OffMap>(entity) == nullptr;
	}

	// What is selected now, and which of it is the player's (by index).
	struct Selection
	{
		std::vector<PickedObject> current;
		std::vector<ecs::Entity> mine;
	};

	template<typename Lookup>
	static Selection SelectedNow(ecs::SystemContext &context, const Lookup &lookup)
	{
		const std::uint32_t local = context.Read<LocalPlayer>().player;
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		Selection selection;
		for (const ecs::Entity entity : context.Side<SideTables, Selected>().Entities())
		{
			const auto *owner = lookup.template Get<engine::gameplay::Owner>(entity);
			if (owner == nullptr)
				continue;
			PickedObject object;
			object.entity = entity;
			object.side = owner->player == local ? PickSide::Mine
				: relationships.Allies(local, owner->player) ? PickSide::Friend
				: relationships.Enemies(local, owner->player) ? PickSide::Enemy : PickSide::Civilian;
			selection.current.push_back(object);
			if (object.side == PickSide::Mine)
				selection.mine.push_back(entity);
		}
		std::sort(selection.mine.begin(), selection.mine.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
		return selection;
	}

	// InGameUI::getCanSelectedObjectsAttack (SELECTION_ANY: the best of the player's selected objects) through
	// ActionManager::getCanAttackObject and WeaponSet::getAbleToAttackSpecificObject (CMD_FROM_PLAYER): nothing against
	// an unattackable object or, not forced, one not an enemy's (unless a script or the map made it player-targetable,
	// OBJECT_STATUS_SCRIPT_TARGETABLE, and it is not an ally's), nor from one with no weapon; an invalid shot when none
	// of its weapons may hit the target's kind or pitch to it, or when it cannot close in (IMMOBILE, SPAWNS_ARE_THE_WEAPONS, or
	// inside something) and its current weapon is out of range; else possible, now within its current weapon's
	// range (with its bonuses, between the bounding circles) or after moving.
	template<typename Lookup>
	static interaction_detail::AttackResult SelectionAttack(ecs::SystemContext &context, const std::vector<ecs::Entity> &mine, ecs::Entity target,
		const Lookup &lookup, bool forceAttack)
	{
		using interaction_detail::AttackResult;
		const auto &weapons = context.Read<engine::gameplay::WeaponCatalog>();
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		const SelectionCatalog &catalog = context.Read<SelectionCatalog>();
		const std::uint32_t local = context.Read<LocalPlayer>().player;
		const auto *targetable = lookup.template Get<engine::gameplay::Targetable>(target);
		const auto *owner = lookup.template Get<engine::gameplay::Owner>(target);
		if (targetable == nullptr || owner == nullptr || (targetable->classes & engine::gameplay::target_class::Unattackable) != 0)
			return AttackResult::NotPossible;
		if (!forceAttack && !relationships.Enemies(local, owner->player))
		{
			const auto *script = lookup.template Get<engine::gameplay::ScriptStatus>(target);
			if (script == nullptr || !script->Has(engine::gameplay::script_status::Targetable) || relationships.Allies(local, owner->player))
				return AttackResult::NotPossible;
		}
		static const std::size_t immobileBit = content::KindOfBit("IMMOBILE"), spawnsBit = content::KindOfBit("SPAWNS_ARE_THE_WEAPONS");
		const auto *targetAt = lookup.template Get<engine::gameplay::Transform>(target);
		AttackResult best = AttackResult::NotPossible;
		for (const ecs::Entity unit : mine)
		{
			if (unit == target)
				continue;
			const auto *armament = lookup.template Get<engine::gameplay::Armament>(unit);
			if (armament == nullptr)
				continue;
			const auto canUse = [&](std::uint32_t weapon) {
				return weapon != engine::gameplay::WeaponCatalog::None && engine::gameplay::CanTarget(weapons.At(weapon), targetable->classes);
			};
			bool usable = canUse(armament->weapon);
			if (const auto *slots = lookup.template Get<engine::gameplay::WeaponSlots>(unit))
				for (const auto &slot : slots->slots)
					usable = usable || canUse(slot.weapon);
			bool inRange = false;
			const auto *unitAt = lookup.template Get<engine::gameplay::Transform>(unit);
			// isAnyWithinTargetPitch: none of its weapons may pitch up or down to the target, an invalid shot.
			if (usable && unitAt != nullptr && targetAt != nullptr &&
				!engine::gameplay::AnyWithinTargetPitch(weapons, *armament, lookup.template Get<engine::gameplay::WeaponSlots>(unit),
					engine::gameplay::PitchBodyOf(unitAt->position, lookup.template Get<engine::gameplay::BodyExtent>(unit)),
					engine::gameplay::PitchBodyOf(targetAt->position, lookup.template Get<engine::gameplay::BodyExtent>(target))))
				usable = false;
			// estimateWeaponDamage: with a damage weapon, none of its weighed weapons is reckoned to hurt the target.
			if (usable)
			{
				const auto *slots = lookup.template Get<engine::gameplay::WeaponSlots>(unit);
				const auto *unitBody = lookup.template Get<engine::gameplay::Targetable>(unit);
				const auto *conditions = lookup.template Get<engine::gameplay::WeaponBonusConditions>(unit);
				const auto *health = lookup.template Get<engine::gameplay::Health>(target);
				engine::gameplay::VictimFitness fitness{targetable->classes, health != nullptr ? health->armor : 0u,
					lookup.template Get<engine::gameplay::UnderConstruction>(target) != nullptr, lookup.template Get<engine::gameplay::Subdual>(target) != nullptr};
				if (engine::gameplay::HasDamageWeapon(weapons, *armament, slots) &&
					!engine::gameplay::AnyWeaponHurts(weapons, context.Read<engine::gameplay::ArmorCatalog>(), *armament, slots, unitBody != nullptr ? unitBody->classes : 0u,
						conditions != nullptr ? conditions->Effective() : 0u, fitness))
					usable = false;
			}
			if (usable && armament->weapon != engine::gameplay::WeaponCatalog::None && unitAt != nullptr && targetAt != nullptr)
			{
				const engine::gameplay::WeaponDefinition &weapon = weapons.At(armament->weapon);
				const auto *conditions = lookup.template Get<engine::gameplay::WeaponBonusConditions>(unit);
				const Engine::Math::Fixed range =
					engine::gameplay::BonusAttackRange(weapon.attackRange, weapons.Bonus(weapon, conditions != nullptr ? conditions->Effective() : 0u));
				const auto *unitBody = lookup.template Get<engine::gameplay::Targetable>(unit);
				const Engine::Math::Fixed reach = range + (unitBody != nullptr ? unitBody->radius : Engine::Math::Fixed{}) + targetable->radius;
				inRange = Engine::Math::DistanceSquared(unitAt->position.XY(), targetAt->position.XY()) <= reach * reach;
			}
			const auto *ref = lookup.template Get<engine::gameplay::DefinitionRef>(unit);
			const bool pinned = lookup.template Get<engine::gameplay::OffMap>(unit) != nullptr ||
				(ref != nullptr && ref->index < catalog.kinds.size() &&
					(content::HasKindOf(catalog.kinds[ref->index], immobileBit) || content::HasKindOf(catalog.kinds[ref->index], spawnsBit)));
			const AttackResult result = (pinned && !inRange) || !usable ? AttackResult::InvalidShot
				: inRange ? AttackResult::Possible : AttackResult::AfterMoving;
			best = std::max(best, result);
		}
		return best;
	}

	// canSelectedObjectsDoAction(ACTIONTYPE_SET_RALLY_POINT, SELECTION_ALL): each selected object AUTO_RALLYPOINT and the
	// player's (isLocallyControlled).
	template<typename Lookup>
	static bool AllSetRallyPoints(ecs::SystemContext &context, const Lookup &lookup, const Selection &selection)
	{
		static const std::size_t rallyBit = content::KindOfBit("AUTO_RALLYPOINT");
		const SelectionCatalog &catalog = context.Read<SelectionCatalog>();
		if (selection.current.empty() || selection.current.size() != selection.mine.size())
			return false;
		return std::ranges::all_of(selection.mine, [&](ecs::Entity entity) {
			const auto *ref = lookup.template Get<engine::gameplay::DefinitionRef>(entity);
			return ref != nullptr && ref->index < catalog.kinds.size() && content::HasKindOf(catalog.kinds[ref->index], rallyBit);
		});
	}

	// What evaluateContextCommand is asked for: a hint (DO_HINT), or the command a click gives (DO_COMMAND; EVALUATE_ONLY
	// finds the same).
	enum class EvaluateMode : std::uint8_t
	{
		Hint,
		Command,
	};

	// The message evaluateContextCommand posts: its hint and, for Hint::SpecialPower, the button whose power is fired (the
	// first selected's).
	struct ContextCommand
	{
		Hint hint{Hint::Invalid};
		const ContextButton *button{nullptr};
	};

private:
	template<typename Lookup>
	static auto SceneOf(ecs::SystemContext &context, const Lookup &lookup, Query &jets)
	{
		return interaction_detail::ContextScene<Lookup, Query>{lookup, context.Read<SelectionCatalog>(), context.Read<engine::gameplay::Relationships>(),
			context.Read<engine::gameplay::ShroudMap>(), context.Read<engine::gameplay::GroundHeight>(), context.Read<engine::gameplay::CargoManifest>(),
			context.Read<engine::gameplay::SpecialPowerRules>(), context.Read<engine::gameplay::SharedPowerTimers>(),
			context.Read<engine::gameplay::PhysicsSettings>(), jets, context.Read<LocalPlayer>().player};
	}

	// InGameUI::getFirstSelectedDrawable: the one selected last (selectDrawable puts each at the front).
	static ecs::Entity FirstSelected(const Selection &selection) { return selection.current.empty() ? ecs::Entity{} : selection.current.back().entity; }

	// issueSpecialPowerCommand for a context command's button (no specific source: the whole selection): at an object
	// (COMMAND_OPTION_NEED_OBJECT_TARGET) only one the button takes as the first selected stands to it
	// (isValidObjectTarget); else at the spot (NEED_TARGET_POS) or with no target.
	template<typename Scene>
	static ContextCommand IssueSpecialPower(const Scene &scene, ecs::Entity first, ecs::Entity target, const ContextButton &button)
	{
		namespace bo = content::button_option;
		if ((button.options & bo::NeedObjectTarget) != 0)
		{
			const engine::gameplay::Relationship relation = scene.Relation(first, target);
			const std::uint32_t wanted = relation == engine::gameplay::Relationship::Enemies ? bo::NeedTargetEnemy
				: relation == engine::gameplay::Relationship::Allies                          ? bo::NeedTargetAlly
																								: bo::NeedTargetNeutral;
			if ((button.options & wanted) == 0)
				return {};
		}
		return {Hint::SpecialPower, &button};
	}

public:
	// CommandTranslator::evaluateContextCommand (evaluateForceAttack when force-attacking) for the object `under` the
	// pointer (none: the ground `at`), with the player's selection, in its order: each ACTIONTYPE through
	// InGameUI::canSelectedObjectsDoAction (SELECTION_ANY: one of them may; DOCK_AT and SET_RALLY_POINT SELECTION_ALL:
	// each) and ActionManager's can* tests, the attack through getCanSelectedObjectsAttack.
	template<typename Lookup>
	static ContextCommand Evaluate(ecs::SystemContext &context, const Lookup &lookup, Query &jets, const Selection &selection, const PickedObject *under,
		const std::optional<std::array<float, 3>> &at, bool forceAttack, bool prefer, EvaluateMode mode, bool allSetRallyPoints = false)
	{
		using namespace interaction_detail;
		const std::vector<ecs::Entity> &mine = selection.mine;
		// One of mine under prefer-selection is never a command; nothing is with none of mine selected
		// (areSelectedObjectsControllable).
		if (under != nullptr && under->side == PickSide::Mine && prefer && !forceAttack)
			return {};
		if (mine.empty())
			return {};
		if (forceAttack)
		{
			// canAnyForceAttack: an object, else the ground (any of mine able to shoot at it).
			if (under != nullptr)
			{
				const AttackResult result = SelectionAttack(context, mine, under->entity, lookup, true);
				return result >= AttackResult::AfterMoving ? ContextCommand{Hint::ForceAttackObject} : result == AttackResult::InvalidShot ? ContextCommand{Hint::ImpossibleAttack} : ContextCommand{};
			}
			if (!at)
				return {};
			const auto &weapons = context.Read<engine::gameplay::WeaponCatalog>();
			const bool any = std::ranges::any_of(mine, [&](ecs::Entity unit) {
				const auto *armament = lookup.template Get<engine::gameplay::Armament>(unit);
				return armament != nullptr && armament->weapon != engine::gameplay::WeaponCatalog::None &&
					engine::gameplay::CanTarget(weapons.At(armament->weapon), engine::gameplay::target_class::Ground);
			});
			return any ? ContextCommand{Hint::ForceAttackGround} : ContextCommand{};
		}
		// InGameUI::canSelectedObjectsOverrideSpecialPowerDestination (SELECTION_ANY): one of mine has a special power
		// whose destination may be driven now (doesSpecialPowerHaveOverridableDestinationActive: a Particle Cannon
		// pre-firing, firing or after its beam; a Spectre Gunship inserting or orbiting), the spot not black to its
		// player (ActionManager::canOverrideSpecialPowerDestination). It comes before every other context command.
		if (at)
		{
			const auto &shroud = context.Read<engine::gameplay::ShroudMap>();
			const Engine::Math::Fixed x = ToFixed((*at)[0]), y = ToFixed((*at)[1]);
			const bool drives = std::ranges::any_of(mine, [&](ecs::Entity unit) {
				namespace domain = generalszh::gameplay;
				bool active = false;
				if (const auto *cannon = lookup.template Get<domain::ParticleCannon>(unit))
					active = cannon->status == domain::CannonStatus::PreFire || cannon->status == domain::CannonStatus::Firing ||
						cannon->status == domain::CannonStatus::PostFire;
				if (const auto *gunship = lookup.template Get<domain::SpectreGunship>(unit))
					active = active || gunship->status == domain::GunshipStatus::Inserting || gunship->status == domain::GunshipStatus::Orbiting;
				const auto *owner = lookup.template Get<engine::gameplay::Owner>(unit);
				return active && owner != nullptr && shroud.StatusAt(owner->player, x, y) != engine::gameplay::CellShroud::Shrouded;
			});
			if (drives)
				return {Hint::OverrideDestination};
		}
		// ACTIONTYPE_SET_RALLY_POINT (SELECTION_ALL, nothing under the pointer): every selected one AUTO_RALLYPOINT and the
		// player's. Every other context command needs an object.
		if (under == nullptr)
			return {allSetRallyPoints ? Hint::SetRallyPoint : Hint::Move};
		const ecs::Entity target = under->entity;
		const auto scene = SceneOf(context, lookup, jets);
		const auto any = [&](auto &&test) { return std::ranges::any_of(mine, [&](ecs::Entity unit) { return test(unit); }); };
		// ACTIONTYPE_RESUME_CONSTRUCTION (SELECTION_ANY).
		if (any([&](ecs::Entity unit) { return scene.CanResumeConstruction(unit, target); }))
			return {Hint::ResumeConstruction};
		// ACTIONTYPE_DOCK_AT (SELECTION_ALL).
		if (std::ranges::all_of(mine, [&](ecs::Entity unit) { return scene.CanDockAt(unit, target); }))
			return {Hint::Dock};
		// ACTIONTYPE_REPAIR_OBJECT (SELECTION_ANY).
		if (any([&](ecs::Entity unit) { return scene.CanRepairObject(unit, target); }))
			return {Hint::Repair};
		// ACTIONTYPE_GET_REPAIRED_AT (SELECTION_ANY).
		if (any([&](ecs::Entity unit) { return scene.CanGetRepairedAt(unit, target); }))
			return {Hint::GetRepaired};
		// ACTIONTYPE_GET_HEALED_AT (SELECTION_ANY).
		if (any([&](ecs::Entity unit) { return scene.CanGetHealedAt(unit, target); }))
			return {Hint::GetHealed};
		// ACTIONTYPE_HIJACK_VEHICLE (SELECTION_ANY).
		if (any([&](ecs::Entity unit) { return scene.CanHijack(unit, target); }))
			return {Hint::Hijack};
		// ACTIONTYPE_CONVERT_OBJECT_TO_CARBOMB (SELECTION_ANY).
		if (any([&](ecs::Entity unit) { return scene.CanConvertToCarBomb(unit, target); }))
			return {Hint::ConvertToCarBomb};
		// ACTIONTYPE_SABOTAGE_BUILDING (SELECTION_ANY).
		if (any([&](ecs::Entity unit) { return scene.CanSabotage(unit, target); }))
			return {Hint::Sabotage};
		// canSelectionSalvage: a salvage crate (isSalvageCrate) and a SALVAGER selected.
		if (const SelectionLook *crate = scene.LookOf(target);
			crate != nullptr && crate->salvageCrate && any([&](ecs::Entity unit) { return scene.Is(unit, context_kind::Salvager); }))
			return {Hint::Salvage};
		// ACTIONTYPE_ENTER_OBJECT (SELECTION_ANY, CHECK_CAPACITY).
		if (any([&](ecs::Entity unit) { return scene.CanEnter(unit, target, true); }))
			return {Hint::Enter};
		const AttackResult attack = SelectionAttack(context, mine, target, lookup, false);
		if (attack == AttackResult::Possible)
			return {Hint::AttackObject};
		if (attack == AttackResult::AfterMoving)
			return {Hint::AttackAfterMoving};
		const ecs::Entity first = FirstSelected(selection);
		const std::span<const ContextButton> buttons = scene.ButtonsOf(first);
		// ACTIONTYPE_CAPTURE_BUILDING (SELECTION_ANY) -> handleCaptureBuildingCommand, by the first selected's command set:
		// its first capture button fired; as a hint, the Black Lotus' shows HACK and an infantry's CAPTUREBUILDING (the
		// set's last); no such button: nothing.
		if (any([&](ecs::Entity unit) { return scene.CanCapture(unit, target); }))
		{
			ContextCommand command;
			for (const ContextButton &button : buttons)
			{
				if (button.power != ContextPower::BlackLotusCapture && button.power != ContextPower::InfantryCapture)
					continue;
				if (mode == EvaluateMode::Command)
					return IssueSpecialPower(scene, first, target, button);
				command = {button.power == ContextPower::BlackLotusCapture ? Hint::Hack : Hint::CaptureBuilding};
			}
			return command;
		}
		// ACTIONTYPE_DISABLE_VEHICLE_VIA_HACKING, _STEAL_CASH_VIA_HACKING, _DISABLE_BUILDING_VIA_HACKING (SELECTION_ANY) ->
		// handleHackCommand: HACK as a hint; the first selected's button of that power fired (none: nothing).
		const auto hack = [&](ContextPower power) -> ContextCommand {
			if (mode == EvaluateMode::Hint)
				return {Hint::Hack};
			for (const ContextButton &button : buttons)
				if (button.power == power)
					return IssueSpecialPower(scene, first, target, button);
			return {};
		};
		if (any([&](ecs::Entity unit) { return scene.CanDisableVehicle(unit, target); }))
			return hack(ContextPower::DisableVehicleHack);
		if (any([&](ecs::Entity unit) { return scene.CanStealCash(unit, target); }))
			return hack(ContextPower::StealCashHack);
		if (any([&](ecs::Entity unit) { return scene.CanDisableBuilding(unit, target); }))
			return hack(ContextPower::DisableBuildingHack);
		// handleInvalidShotCommand; else handleDefaultMoveCommand (never a move onto an object).
		return {attack == AttackResult::InvalidShot ? Hint::ImpossibleAttack : Hint::Move};
	}

	// The hint as the cursor (CommandTranslator's MSG_MOUSEOVER_*_HINT: evaluateContextCommand with DO_HINT, then
	// InGameUI::createMouseoverHint and createCommandHint), in MOUSEMODE_DEFAULT and MOUSEMODE_BUILD_PLACE; left as it
	// is where the original sets none.
	static void HoverCursor(Query &jets, ecs::SystemContext &context, const PointerInput &pointer)
	{
		using namespace interaction_detail;
		using content::MouseCursorKind;
		CursorState &cursor = context.Write<CursorState>();
		const MouseSettings &mouse = context.Read<MouseSettings>();
		// InGameUI::setScrolling: SCROLL while the camera scrolls (no hints meanwhile), turned the scroll's way.
		if (pointer.scrolling)
		{
			cursor.cursor = MouseCursorKind::Scroll;
			cursor.direction = ScrollDirection(pointer.scrollX, pointer.scrollY, mouse.cursorDirections[static_cast<std::size_t>(MouseCursorKind::Scroll)]);
			return;
		}
		cursor.direction = 0;
		if (cursor.cursor == MouseCursorKind::Scroll)
			cursor.cursor = MouseCursorKind::Arrow; // setScrolling(FALSE)
		if (context.Read<InteractionState>().dragSelecting)
			return; // m_isSelecting: no hints
		// A window that is not see-through under the pointer: the arrow.
		if (pointer.overInterface)
		{
			cursor.cursor = MouseCursorKind::Arrow;
			return;
		}
		// MOUSEMODE_GUI_COMMAND: the button's cursor over a valid target, else its invalid one.
		if (const GuiTargeting &targeting = context.Read<GuiTargeting>(); targeting.active)
		{
			// A context command (a special power): valid or invalid; any other needing a target (a guard): its cursor.
			if (targeting.kind == GuiCommandKind::FireWeapon && (targeting.options & content::button_option::ContextModeCommand) != 0)
				cursor.cursor = FireWeaponValid(context, pointer) ? targeting.cursor : targeting.invalidCursor;
			else if (targeting.kind == GuiCommandKind::CombatDrop)
				cursor.cursor = CombatDropValid(context, pointer) ? targeting.cursor : targeting.invalidCursor;
			else if (targeting.kind != GuiCommandKind::SpecialPower)
				cursor.cursor = targeting.cursor;
			else
				cursor.cursor = GuiTargetValid(context, pointer) ? targeting.cursor : targeting.invalidCursor;
			return;
		}
		const auto lookup = context.Lookup<Lookup>();
		const SelectionCatalog &catalog = context.Read<SelectionCatalog>();
		const bool forceAttack = pointer.ctrl;
		const auto under = PickAt(context, lookup, pointer.x, pointer.y, forceAttack);
		// CanSelectDrawable(draw, FALSE).
		const SelectionLook *underLook = under ? catalog.Of(under->definition) : nullptr;
		const bool drawSelectable = under && underLook != nullptr && Pickable(under->entity, *underLook, lookup, false);
		const Selection selection = SelectedNow(context, lookup);
		const bool placing = context.Read<BuildPlacement>().active;
		if (selection.current.empty())
		{
			// createMouseoverHint, nothing selected: one of mine that may be selected, else the arrow.
			if (!placing)
				cursor.cursor = drawSelectable && under->picked.side == PickSide::Mine ? MouseCursorKind::Select : MouseCursorKind::Arrow;
			return;
		}
		const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
		Hint hint = Evaluate(context, lookup, jets, selection, under ? &under->picked : nullptr, at, forceAttack, pointer.shift, EvaluateMode::Hint,
			AllSetRallyPoints(context, lookup, selection))
						.hint;
		// createCommandHint: an attack hint on an object black to the player is a move hint.
		if (under && (hint == Hint::AttackObject || hint == Hint::AttackAfterMoving) &&
			context.Read<engine::gameplay::ShroudMap>().StatusAt(context.Read<LocalPlayer>().player, ToFixed(under->x), ToFixed(under->y)) ==
				engine::gameplay::CellShroud::Shrouded)
			hint = Hint::Move;
		if (placing)
		{
			// MOUSEMODE_BUILD_PLACE.
			if (hint == Hint::Move)
				cursor.cursor = MouseCursorKind::Build;
			else if (hint == Hint::AttackObject || hint == Hint::AttackAfterMoving)
				cursor.cursor = MouseCursorKind::InvalidBuild;
			return;
		}
		// MOUSEMODE_DEFAULT: one selected object not the player's shows the arrow.
		if (selection.current.size() == 1 && selection.current.front().side != PickSide::Mine)
		{
			cursor.cursor = MouseCursorKind::Arrow;
			return;
		}
		const auto kindsOf = [&](ecs::Entity entity) -> std::uint16_t {
			const auto *ref = lookup.Get<engine::gameplay::DefinitionRef>(entity);
			const SelectionLook *look = ref != nullptr ? catalog.Of(ref->index) : nullptr;
			return look != nullptr ? look->kinds : 0;
		};
		switch (hint)
		{
		case Hint::Invalid: return;
		case Hint::Move:
			if (!drawSelectable && selection.current.size() == 1 && (kindsOf(selection.current.front().entity) & select_kind::Structure) != 0)
				cursor.cursor = MouseCursorKind::GenericInvalid;
			else if (drawSelectable && under->picked.side == PickSide::Mine && (underLook->kinds & select_kind::Mine) == 0)
				cursor.cursor = MouseCursorKind::Select;
			else
				cursor.cursor = MouseCursorKind::Move;
			return;
		case Hint::AttackObject: cursor.cursor = MouseCursorKind::AttackObj; return;
		case Hint::AttackAfterMoving: cursor.cursor = MouseCursorKind::OutRange; return;
		case Hint::ForceAttackObject: cursor.cursor = MouseCursorKind::ForceAttackObj; return;
		case Hint::ForceAttackGround: cursor.cursor = MouseCursorKind::ForceAttackGround; return;
		case Hint::GetRepaired: cursor.cursor = MouseCursorKind::GetRepaired; return;
		case Hint::Dock: cursor.cursor = MouseCursorKind::Dock; return;
		case Hint::GetHealed: cursor.cursor = MouseCursorKind::GetHealed; return;
		case Hint::Repair: cursor.cursor = MouseCursorKind::DoRepair; return;
		case Hint::ResumeConstruction: cursor.cursor = MouseCursorKind::ResumeConstruction; return;
		case Hint::Enter: cursor.cursor = MouseCursorKind::EnterFriendly; return;
		case Hint::Hijack:
		case Hint::ConvertToCarBomb:
		case Hint::Sabotage: cursor.cursor = MouseCursorKind::EnterAggressive; return;
		case Hint::CaptureBuilding: cursor.cursor = MouseCursorKind::CaptureBuilding; return;
		case Hint::Hack: cursor.cursor = MouseCursorKind::Hack; return;
		// MSG_DO_SALVAGE_HINT: MOVETO.
		case Hint::Salvage: cursor.cursor = MouseCursorKind::Move; return;
		case Hint::SpecialPower: return; // a command only
		case Hint::ImpossibleAttack: cursor.cursor = MouseCursorKind::GenericInvalid; return;
		case Hint::OverrideDestination: cursor.cursor = MouseCursorKind::ParticleUplinkCannon; return;
		// MSG_SET_RALLY_POINT_HINT: SET_RALLY_POINT over the ground (SELECTING over something selectable).
		case Hint::SetRallyPoint: cursor.cursor = drawSelectable ? MouseCursorKind::Select : MouseCursorKind::SetRallyPoint; return;
		}
	}

	// COMBATDROP's validity (CommandTranslator::evaluateContextCommand): over an object (any relationship the button takes),
	// valid when a selected unit may drop into it (canSelectedObjectsDoAction(ACTIONTYPE_COMBATDROP_INTO), found by the host);
	// an object it may not drop into is invalid (no drop at its spot instead); over the ground, a drop there.
	static bool CombatDropValid(ecs::SystemContext &context, const PointerInput &pointer)
	{
		GuiTargeting &targeting = context.Write<GuiTargeting>();
		const auto lookup = context.Lookup<Lookup>();
		const auto under = PickAt(context, lookup, pointer.x, pointer.y, false);
		targeting.hovered = under && TakesRelationship(targeting.options, under->picked.side) ? under->entity : ecs::Entity{};
		if (targeting.hovered != ecs::Entity{})
			return targeting.validFor == targeting.hovered;
		return GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y).has_value();
	}

	// handleGuiCommand's validity for GUI_COMMAND_SPECIAL_POWER (canSelectedObjectsDoSpecialPower, the waiting command's
	// object): with COMMAND_OPTION_NEED_OBJECT_TARGET (first), the object under the pointer (resolveGuiCommandTarget: a
	// pickable one) when the host found its power may be fired at it (canDoSpecialPowerAtObject); with NEED_TARGET_POS, a
	// spot where ActionManager::canDoSpecialPowerAtLocation lets its power go for its player.
	static bool GuiTargetValid(ecs::SystemContext &context, const PointerInput &pointer)
	{
		GuiTargeting &targeting = context.Write<GuiTargeting>();
		if ((targeting.options & content::button_option::NeedObjectTarget) != 0)
		{
			const auto lookup = context.Lookup<Lookup>();
			const auto under = PickAt(context, lookup, pointer.x, pointer.y, false);
			targeting.hovered = under ? under->entity : ecs::Entity{};
			return under && targeting.validFor == under->entity;
		}
		if ((targeting.options & content::button_option::NeedTargetPos) == 0)
			return false;
		const auto lookup = context.Lookup<Lookup>();
		const auto *owner = lookup.Get<engine::gameplay::Owner>(targeting.source);
		const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
		if (owner == nullptr || !at)
			return false;
		return generalszh::gameplay::CanDoSpecialPowerAtLocation(targeting.powerType, owner->player,
			{interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])}, context.Read<engine::gameplay::GroundHeight>(),
			context.Read<engine::gameplay::ShroudMap>());
	}

	// CommandButton::isValidRelationshipTarget: the button takes an object of this side (one's own count as allies).
	static bool TakesRelationship(std::uint32_t options, PickSide side) noexcept
	{
		namespace bo = content::button_option;
		const std::uint32_t wanted = side == PickSide::Enemy ? bo::NeedTargetEnemy : side == PickSide::Civilian ? bo::NeedTargetNeutral : bo::NeedTargetAlly;
		return (options & wanted) != 0;
	}

	// InGameUI::canSelectedObjectsEffectivelyUseWeapon (SELECTION_ANY) through ActionManager::canFireWeaponAtObject /
	// canFireWeaponAtLocation: one of the player's selected has a weapon in the button's slot; at an object, one it may
	// attack (getAbleToAttackSpecificObject: an enemy's, or one a script made player-targetable not an ally's) that the
	// slot's weapon can hit (its kind). (A sniper's KILLPILOT rules are not ported.)
	static bool FireWeaponValid(ecs::SystemContext &context, const PointerInput &pointer)
	{
		const GuiTargeting &targeting = context.Read<GuiTargeting>();
		const auto lookup = context.Lookup<Lookup>();
		const Selection selection = SelectedNow(context, lookup);
		const auto &weapons = context.Read<engine::gameplay::WeaponCatalog>();
		const auto slotWeapon = [&](ecs::Entity unit) -> std::uint32_t {
			if (const auto *set = lookup.Get<engine::gameplay::WeaponSlots>(unit))
				return targeting.weaponSlot < set->slots.size() ? set->slots[targeting.weaponSlot].weapon : engine::gameplay::WeaponCatalog::None;
			const auto *armament = lookup.Get<engine::gameplay::Armament>(unit);
			return armament != nullptr && targeting.weaponSlot == 0 ? armament->weapon : engine::gameplay::WeaponCatalog::None;
		};
		if ((targeting.options & content::button_option::NeedObjectTarget) != 0)
		{
			const auto under = PickAt(context, lookup, pointer.x, pointer.y, false);
			if (!under)
				return false;
			const auto *targetable = lookup.Get<engine::gameplay::Targetable>(under->entity);
			if (targetable == nullptr || SelectionAttack(context, selection.mine, under->entity, lookup, false) == interaction_detail::AttackResult::NotPossible)
				return false;
			return std::ranges::any_of(selection.mine, [&](ecs::Entity unit) {
				const std::uint32_t weapon = slotWeapon(unit);
				return weapon != engine::gameplay::WeaponCatalog::None && engine::gameplay::CanTarget(weapons.At(weapon), targetable->classes);
			});
		}
		if ((targeting.options & content::button_option::NeedTargetPos) != 0 &&
			!GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y))
			return false;
		return std::ranges::any_of(selection.mine, [&](ecs::Entity unit) { return slotWeapon(unit) != engine::gameplay::WeaponCatalog::None; });
	}

	// issueSpecialPowerCommand: the power fired for the waiting command's object, and the wait is over
	// (setGUICommand(nullptr)); not on an invalid target.
	static void GuiClick(ecs::SystemContext &context, const PointerInput &pointer)
	{
		if (context.Read<GuiTargeting>().kind == GuiCommandKind::Guard)
		{
			GuardClick(context, pointer);
			return;
		}
		// issueCombatDropCommand: into the object under the pointer (MSG_COMBATDROP_AT_OBJECT), else at the ground clicked
		// (MSG_COMBATDROP_AT_LOCATION), by the player's selection; the wait ends. Not on an invalid target.
		if (context.Read<GuiTargeting>().kind == GuiCommandKind::CombatDrop)
		{
			if (!CombatDropValid(context, pointer))
				return;
			const ecs::Entity into = context.Read<GuiTargeting>().hovered;
			context.Write<GuiTargeting>() = {};
			const auto lookup = context.Lookup<Lookup>();
			const Selection selection = SelectedNow(context, lookup);
			if (selection.mine.empty())
				return;
			Engine::Math::FixedVector2 spot;
			if (into == ecs::Entity{})
				if (const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y))
					spot = {interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])};
			context.Write<PlayerOrders>().pending.push_back(commands::CombatDrop{selection.mine, into, spot});
			return;
		}
		// FIRE_WEAPON: a context command (CONTEXTMODE_COMMAND: handleGuiCommand) only on a target its weapon may be used on
		// (canSelectedObjectsEffectivelyUseWeapon), the wait ending then; otherwise GUICommandTranslator's doFireWeaponCommand:
		// at the ground clicked (NEED_TARGET_POS: MSG_DO_WEAPON_AT_LOCATION), or at the object under the pointer the button
		// takes (validUnderCursor: its relationship to the player; MSG_DO_WEAPON_AT_OBJECT), the wait ending either way.
		if (context.Read<GuiTargeting>().kind == GuiCommandKind::FireWeapon)
		{
			const GuiTargeting waiting = context.Read<GuiTargeting>();
			const bool contextCommand = (waiting.options & content::button_option::ContextModeCommand) != 0;
			if (contextCommand && !FireWeaponValid(context, pointer))
				return;
			context.Write<GuiTargeting>() = {};
			const auto lookup = context.Lookup<Lookup>();
			const Selection selection = SelectedNow(context, lookup);
			if (selection.mine.empty())
				return;
			commands::FireWeapon fire{selection.mine, waiting.weaponSlot, 0, waiting.maxShots, {}, {}};
			if ((waiting.options & content::button_option::NeedTargetPos) != 0)
			{
				const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
				if (!at)
					return;
				fire.at = 1;
				fire.position = {interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])};
			}
			else if ((waiting.options & content::button_option::NeedObjectTarget) != 0)
			{
				const auto under = PickAt(context, lookup, pointer.x, pointer.y, false);
				if (!under || !TakesRelationship(waiting.options, under->picked.side))
					return;
				fire.at = 2;
				fire.target = under->entity;
			}
			context.Write<PlayerOrders>().pending.push_back(fire);
			return;
		}
		// GUICommandTranslator's doAttackMoveCommand: the selection attack-moves to the ground clicked (MSG_DO_ATTACKMOVETO);
		// the wait ends either way.
		if (context.Read<GuiTargeting>().kind == GuiCommandKind::AttackMove)
		{
			context.Write<GuiTargeting>() = {};
			const auto lookup = context.Lookup<Lookup>();
			const Selection selection = SelectedNow(context, lookup);
			const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
			if (!selection.mine.empty() && at)
				context.Write<PlayerOrders>().pending.push_back(
					commands::AttackMoveTo{selection.mine, {interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])}});
			return;
		}
		// GUICommandTranslator's doPlaceBeacon: a beacon at the ground clicked (MSG_PLACE_BEACON); the wait ends either way.
		if (context.Read<GuiTargeting>().kind == GuiCommandKind::PlaceBeacon)
		{
			context.Write<GuiTargeting>() = {};
			if (const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y))
				context.Write<PlayerOrders>().pending.push_back(commands::PlaceBeacon{{interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])}});
			return;
		}
		// GUICommandTranslator's doSetRallyPointCommand: the one selected structure's rally point at the ground clicked
		// (MSG_SET_RALLY_POINT); the wait ends either way.
		if (context.Read<GuiTargeting>().kind == GuiCommandKind::RallyPoint)
		{
			context.Write<GuiTargeting>() = {};
			const auto lookup = context.Lookup<Lookup>();
			const Selection selection = SelectedNow(context, lookup);
			const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
			if (!selection.current.empty() && at)
				context.Write<PlayerOrders>().pending.push_back(commands::SetRallyPoint{selection.current.front().entity,
					{interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])}});
			return;
		}
		if (!GuiTargetValid(context, pointer))
			return;
		GuiTargeting &targeting = context.Write<GuiTargeting>();
		auto &orders = context.Write<PlayerOrders>().pending;
		// MSG_DO_SPECIAL_POWER_AT_OBJECT for an object target, else MSG_DO_SPECIAL_POWER_AT_LOCATION.
		if ((targeting.options & content::button_option::NeedObjectTarget) != 0)
			orders.push_back(commands::UseSpecialPowerAtObject{targeting.source, targeting.power, targeting.validFor});
		else
		{
			const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
			orders.push_back(
				commands::UseSpecialPower{targeting.source, targeting.power, {interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])}, true});
		}
		targeting = {};
	}

	// GUICommandTranslator's doGuardCommand: with COMMAND_OPTION_NEED_OBJECT_TARGET, the selectable object under the pointer
	// whose relationship to the player the button takes (isValidObjectTarget: NEED_TARGET_ENEMY / ALLY / NEUTRAL_OBJECT;
	// one's own count as allies) is guarded (MSG_DO_GUARD_OBJECT); else the ground under the pointer (NEED_TARGET_POS) or,
	// without one, where the first selected stands (MSG_DO_GUARD_POSITION); by the player's selected units. The wait ends
	// either way (COMMAND_COMPLETE).
	static void GuardClick(ecs::SystemContext &context, const PointerInput &pointer)
	{
		GuiTargeting &targeting = context.Write<GuiTargeting>();
		const auto lookup = context.Lookup<Lookup>();
		const Selection selection = SelectedNow(context, lookup);
		auto &orders = context.Write<PlayerOrders>().pending;
		const GuiTargeting waiting = targeting;
		targeting = {};
		if (selection.current.empty())
			return;
		if ((waiting.options & content::button_option::NeedObjectTarget) != 0)
			if (const auto under = PickAt(context, lookup, pointer.x, pointer.y, false))
			{
				namespace bo = content::button_option;
				const std::uint32_t wanted = under->picked.side == PickSide::Enemy ? bo::NeedTargetEnemy
					: under->picked.side == PickSide::Civilian ? bo::NeedTargetNeutral : bo::NeedTargetAlly;
				if ((waiting.options & wanted) != 0)
				{
					orders.push_back(commands::GuardObject{selection.mine, under->entity, waiting.guardMode});
					return;
				}
			}
		Engine::Math::FixedVector2 spot;
		if ((waiting.options & content::button_option::NeedTargetPos) != 0)
		{
			const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
			if (!at)
				return;
			spot = {interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])};
		}
		else
		{
			const auto *first = lookup.Get<engine::gameplay::Transform>(selection.current.front().entity);
			if (first == nullptr)
				return;
			spot = first->position.XY();
		}
		orders.push_back(commands::GuardPosition{selection.mine, spot, waiting.guardMode});
	}

	static void LeftClick(Query &jets, ecs::SystemContext &context, const PointerInput &pointer, bool isPoint)
	{
		using namespace interaction_detail;
		const InteractionView &view = context.Read<InteractionView>();
		const InteractionState &state = context.Read<InteractionState>();
		const SelectionCatalog &catalog = context.Read<SelectionCatalog>();
		const std::uint32_t local = context.Read<LocalPlayer>().player;
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		auto &selected = context.Side<SideTables, Selected>();
		const auto lookup = context.Lookup<Lookup>();
		const bool forceAttack = pointer.ctrl;
		const bool prefer = pointer.shift;

		// What is under the pointer or in the box.
		std::vector<Candidate> candidates;
		std::optional<Candidate> nearest;
		if (isPoint)
		{
			nearest = PickAt(context, lookup, pointer.x, pointer.y, forceAttack);
			if (nearest)
				candidates.push_back(*nearest);
		}
		else
		{
			const float x0 = std::min(state.leftAnchorX, pointer.x), x1 = std::max(state.leftAnchorX, pointer.x);
			const float y0 = std::min(state.leftAnchorY, pointer.y), y1 = std::max(state.leftAnchorY, pointer.y);
			context.Read<engine::gameplay::VisibleObjects>().ForEach([&](const engine::gameplay::VisibleObject &object) {
				const SelectionLook *look = catalog.Of(object.definition);
				if (look == nullptr || !Pickable(object.entity, *look, lookup, forceAttack))
					return;
				const float px = Engine::Math::ToFloat(object.transform.position.x), py = Engine::Math::ToFloat(object.transform.position.y),
							pz = Engine::Math::ToFloat(object.transform.position.z);
				const auto picked = Classify(object.entity, lookup, catalog, object.definition, object.player, relationships, local);
				float sx = 0, sy = 0;
				if (picked && view.Project(px, py, pz, sx, sy) && sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1)
					candidates.push_back({object.entity, 0.0f, *picked, px, py, object.definition});
			});
		}

		const Selection now = SelectedNow(context, lookup);
		const std::vector<ecs::Entity> &mine = now.mine;
		std::vector<PickedObject> picked;
		bool allSelected = true;
		for (const Candidate &candidate : candidates)
		{
			picked.push_back(candidate.picked);
			allSelected = allSelected && selected.Get(candidate.entity) != nullptr;
		}
		const auto underPointer = GroundUnder(view, context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
		const bool allRally = AllSetRallyPoints(context, lookup, now);
		const auto hasCommand = [&](std::size_t index) {
			// evaluateContextCommand (EVALUATE_ONLY): a command other than a move onto an object.
			const Hint hint = Evaluate(context, lookup, jets, now, &picked[index], underPointer, forceAttack, prefer, EvaluateMode::Command).hint;
			return hint != Hint::Invalid && hint != Hint::Move;
		};
		const ClickSelection selection = LeftClickSelection(now.current, picked, isPoint, prefer, forceAttack, false, allSelected, hasCommand);
		switch (selection.outcome)
		{
		case ClickOutcome::Replace:
			selected.Clear();
			[[fallthrough]];
		case ClickOutcome::Add:
			for (const ecs::Entity entity : selection.entities)
				if (selected.Get(entity) == nullptr)
					selected.Emplace(entity);
			return;
		case ClickOutcome::Deselect:
			for (const ecs::Entity entity : selection.entities)
				selected.Erase(entity);
			return;
		case ClickOutcome::Pass:
			break;
		}

		// CommandTranslator, MSG_MOUSE_LEFT_CLICK: a point, with a selection of the player's.
		if (!isPoint || mine.empty())
			return;
		auto &orders = context.Write<PlayerOrders>().pending;
		const ContextCommand command = Evaluate(context, lookup, jets, now, nearest ? &nearest->picked : nullptr, underPointer, forceAttack, prefer,
			EvaluateMode::Command, allRally);
		switch (command.hint)
		{
		// handleSpecialPowerOverrideDestinationCommand: MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION at the spot, for the
		// whole selection (no specific source).
		case Hint::OverrideDestination:
			orders.push_back(commands::SpecialPowerDestination{mine, {ToFixed((*underPointer)[0]), ToFixed((*underPointer)[1])}});
			return;
		// handleResumeConstructionCommand (MSG_RESUME_CONSTRUCTION), handleDockAtCommand (MSG_DOCK), handleRepairObjectCommand
		// (MSG_DO_REPAIR), handleGetRepairedAtCommand (MSG_GET_REPAIRED), handleGetHealedAtCommand (MSG_GET_HEALED): the
		// object, for the whole selection.
		case Hint::ResumeConstruction: orders.push_back(commands::ResumeConstruction{mine, nearest->entity}); return;
		case Hint::Dock: orders.push_back(commands::Dock{mine, nearest->entity}); return;
		case Hint::Repair: orders.push_back(commands::Repair{mine, nearest->entity}); return;
		case Hint::GetRepaired: orders.push_back(commands::GetRepaired{mine, nearest->entity}); return;
		case Hint::GetHealed: orders.push_back(commands::GetHealed{mine, nearest->entity}); return;
		// handleHijackVehicleCommand, handleConvertObjectToCarBombCommand, handleSabotageBuildingCommand and
		// handleEnterObjectCommand: an enter order (createEnterMessage: MSG_ENTER) for the whole selection.
		case Hint::Hijack:
		case Hint::ConvertToCarBomb:
		case Hint::Sabotage:
		case Hint::Enter: orders.push_back(commands::Enter{mine, nearest->entity}); return;
		// handleSalvageCommand: MSG_DO_SALVAGE, a move (onDoMoveto) to where the crate is.
		case Hint::Salvage:
			if (const auto *crate = lookup.Get<engine::gameplay::Transform>(nearest->entity))
				orders.push_back(commands::MoveTo{mine, crate->position.XY()});
			return;
		// issueSpecialPowerCommand with no specific source: the selection's group fires the button's power
		// (groupDoSpecialPowerAtObject: each of them that may), at the object (MSG_DO_SPECIAL_POWER_AT_OBJECT), at the spot
		// (MSG_DO_SPECIAL_POWER_AT_LOCATION) or with no target (MSG_DO_SPECIAL_POWER), with the button's options.
		case Hint::SpecialPower:
		{
			const ContextButton &button = *command.button;
			for (const ecs::Entity unit : mine)
			{
				if ((button.options & content::button_option::NeedObjectTarget) != 0)
					orders.push_back(commands::UseSpecialPowerAtObject{unit, button.specialPower, nearest->entity});
				else if ((button.options & content::button_option::NeedTargetPos) != 0 && underPointer)
					orders.push_back(commands::UseSpecialPower{unit, button.specialPower, {ToFixed((*underPointer)[0]), ToFixed((*underPointer)[1])}, true, button.options});
				else if ((button.options & content::button_option::NeedTargetPos) == 0)
					orders.push_back(commands::UseSpecialPower{unit, button.specialPower, {}, false, button.options});
			}
			return;
		}
		case Hint::CaptureBuilding:
		case Hint::Hack: return; // hints only
		case Hint::AttackObject:
		case Hint::AttackAfterMoving:
		case Hint::ForceAttackObject: orders.push_back(commands::Attack{mine, nearest->entity}); return;
		// MSG_DO_FORCE_ATTACK_GROUND: fire at the spot.
		case Hint::ForceAttackGround:
			orders.push_back(commands::AttackPosition{mine, {ToFixed((*underPointer)[0]), ToFixed((*underPointer)[1])}});
			return;
		// handleDefaultMoveCommand: a move only onto the ground (never onto an object).
		case Hint::Move:
			if (!nearest && underPointer)
				orders.push_back(commands::MoveTo{mine, {ToFixed((*underPointer)[0]), ToFixed((*underPointer)[1])}});
			return;
		// handleSetRallyPointCommand: each selected one's rally point there.
		case Hint::SetRallyPoint:
			for (const ecs::Entity factory : now.current | std::views::transform([](const PickedObject &object) { return object.entity; }))
				orders.push_back(commands::SetRallyPoint{factory, {ToFixed((*underPointer)[0]), ToFixed((*underPointer)[1])}});
			return;
		case Hint::ImpossibleAttack:
		case Hint::Invalid: return;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::PointerInteractionSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.pointer_interaction";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
