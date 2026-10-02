export module games.generalszh.gameplay.abilities.algorithms.special_ability_update;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.abilities.components.special_abilities;
export import games.generalszh.gameplay.abilities.components.ability_laser;
export import games.generalszh.gameplay.abilities.systems.special_ability_system;
import games.generalszh.gameplay.powers.algorithms.power_trigger;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.teams.algorithms.defection;
import games.generalszh.gameplay.construction.algorithms.selling;
import games.generalszh.gameplay.veterancy.algorithms.veterancy_placement;
import games.generalszh.gameplay.sciences.algorithms.general_ranks;
import games.generalszh.gameplay.eva.resources.eva_notices;
import engine.ecs.query.query;
import engine.gameplay.rts.stealth.systems.stealth_system;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.common.weapons.components.weapon_slots;
import engine.gameplay.common.identity.resources.relationships;
import games.generalszh.gameplay.hacking.algorithms.hack_effects;
import games.generalszh.gameplay.abilities.resources.ability_notices;
import games.generalszh.content.objects.model_conditions;
export import games.generalszh.gameplay.abilities.algorithms.ability_setup;
import engine.gameplay.common.appearance.components.appearance;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.status.components.ai_activity;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.rts.containment.components.garrison;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.navigation.components.ignored_obstacle;
import engine.gameplay.rts.navigation.components.navigation;
export import games.generalszh.gameplay.abilities.algorithms.special_objects;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.gameplay.stealth.algorithms.disguises;
import games.generalszh.gameplay.academy.algorithms.academy_records;

// SpecialAbilityUpdate's ends outside its system (SpecialAbilitySystem steps the abilities): starting one (its
// SpecialAbility module's initiateIntentToDoSpecialPower, an order's entry point), and, once the systems have run, what
// the tick's abilities did beyond their units (ApplyAbilityEvents), in order.
export namespace generalszh::gameplay
{
namespace ability_detail
{
namespace gp = engine::gameplay;

namespace mc
{
inline constexpr std::uint32_t Unpacking = content::ModelConditionBit("UNPACKING");
inline constexpr std::uint32_t Packing = content::ModelConditionBit("PACKING");
inline constexpr std::uint32_t FiringA = content::ModelConditionBit("FIRING_A");
inline constexpr std::uint32_t RaisingFlag = content::ModelConditionBit("RAISING_FLAG");
}

inline void Look(GameWorld &game, ecs::Entity unit, std::uint32_t bit, bool on)
{
	if (auto *look = game.world.Get<gp::Appearance>(unit))
		look->Set(bit, on);
}

// onExit, for an ability another one ends.
inline void Exit(GameWorld &game, ecs::Entity unit, AbilitySlot &slot)
{
	Look(game, unit, mc::Unpacking, false);
	Look(game, unit, mc::Packing, false);
	Look(game, unit, mc::FiringA, false);
	Look(game, unit, mc::RaisingFlag, false);
	if (auto *activity = game.world.Get<gp::AiActivity>(unit))
		activity->usingAbility = 0;
	if (auto *notices = game.world.FindResource<AbilityNotices>())
		notices->abilities.push_back({unit, slot.power, AbilityCue::PreparationEnd, false});
	slot.Set(ability_flag::Active, false);
	slot.Set(ability_flag::WithinRange, false);
	slot.packing = AbilityPacking::None;
}

inline std::uint32_t TeamOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *member = game.world.Get<gp::TeamMember>(entity);
	return member != nullptr ? member->team : 0xFFFFFFFFu;
}

// The unit's module slot for `power`.
inline std::optional<std::uint8_t> SlotOfPower(const GameWorld &game, ecs::Entity unit, std::uint32_t power)
{
	const auto *abilities = game.world.IsAlive(unit) ? game.world.Get<SpecialAbilities>(unit) : nullptr;
	if (abilities != nullptr)
		for (std::uint8_t index = 0; index < abilities->count; ++index)
			if (abilities->slots[index].power == power)
				return index;
	return std::nullopt;
}

// triggerAbilityEffect's checkAndDetonateBoobyTrap on the target: false when the effect goes on now. A trap that went
// off holds the effect till its blast has landed (the next tick): then it goes on unless the unit or the target is dead
// ("Whoops, it was mined").
// Object::getRelationship == ALLIES (the teams' overrides, then the players').
// GeometryInfo::getFootprintArea: a circle of its radius (sphere, cylinder) or its box.
inline Engine::Math::Fixed FootprintArea(const content::Geometry &geometry)
{
	using Engine::Math::Fixed;
	if (geometry.shape == content::GeometryShape::Box)
		return Fixed::FromInt(4) * geometry.majorRadius * geometry.minorRadius;
	return Fixed::FromRatio(355, 113) * geometry.majorRadius * geometry.majorRadius; // PI (within 3e-7)
}

inline bool AlliedTo(GameWorld &game, ecs::Entity unit, ecs::Entity other)
{
	const auto team = [&](ecs::Entity entity) {
		const auto *member = game.world.Get<gp::TeamMember>(entity);
		return member != nullptr ? member->team : gp::Relationships::NoTeam;
	};
	return game.world.Resource<gp::Relationships>().Allies(team(unit), OwnerPlayer(game, unit), team(other), OwnerPlayer(game, other));
}

// WakeIdleEnemiesSystem for one revealed unit: idle enemies (standing, attacking nothing, not busy) that see it (within
// their vision, 3D) look for a target on their next mood check, now.
inline void WakeIdleEnemies(GameWorld &game, ecs::Entity revealed)
{
	const auto *at = game.world.Get<gp::Transform>(revealed);
	if (at == nullptr)
		return;
	const auto *member = game.world.Get<gp::TeamMember>(revealed);
	const std::uint32_t team = member != nullptr ? member->team : gp::Relationships::NoTeam, player = OwnerPlayer(game, revealed);
	const auto &relationships = game.world.Resource<gp::Relationships>();
	ecs::Query<ecs::Write<gp::Aggression>, ecs::Read<gp::Transform>, ecs::Read<gp::Owner>, ecs::Optional<gp::MoveOrder>, ecs::Optional<gp::AttackTarget>,
		ecs::Optional<gp::AiActivity>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		auto aggressions = chunk.template Get<gp::Aggression>();
		const auto transforms = chunk.template Get<gp::Transform>();
		const auto owners = chunk.template Get<gp::Owner>();
		const auto orders = chunk.template Get<gp::MoveOrder>();
		const auto targets = chunk.template Get<gp::AttackTarget>();
		const auto activities = chunk.template Get<gp::AiActivity>();
		for (std::size_t row = 0; row < aggressions.size(); ++row)
		{
			const bool idle = (orders.empty() || orders[row].mode == gp::MoveMode::Idle) && (targets.empty() || !targets[row].target.IsValid()) &&
				(activities.empty() || activities[row].busy == 0);
			if (!idle || relationships.Between(gp::Relationships::NoTeam, owners[row].player, team, player) != gp::Relationship::Enemies)
				continue;
			const Engine::Math::FixedVector3 apart = transforms[row].position - at->position;
			const Engine::Math::Fixed vision = aggressions[row].vision;
			if (apart.x * apart.x + apart.y * apart.y + apart.z * apart.z <= vision * vision)
				gp::WakeToTarget(aggressions[row], game.tick);
		}
	});
}

inline bool HeldByTrap(GameWorld &game, const AbilityEvent &event, std::uint8_t slot)
{
	if (event.afterTrap)
		return EffectivelyDead(game, event.target) || EffectivelyDead(game, event.unit);
	if (!CheckAndDetonateBoobyTrap(game, event.target, event.unit))
		return false;
	game.world.Get<SpecialAbilities>(event.unit)->slots[slot].trapTarget = event.target;
	return true;
}

// A charge (or booby trap) placed on the target: createSpecialObject, then its StickyBombUpdate's initStickyBomb (one
// without: the module's special objects destroyed).
inline void PlaceCharge(GameWorld &game, ecs::Entity unit, std::uint8_t slot, ecs::Entity target)
{
	const ecs::Entity charge = CreateSpecialObject(game, unit, slot);
	if (charge == ecs::Entity{})
		return;
	if (game.templates.StickyBombOf(game.world.Get<gp::DefinitionRef>(charge)->index) == nullptr)
	{
		KillSpecialObjects(game, unit, slot);
		return;
	}
	InitStickyBomb(game, charge, target, unit);
}
}

// SpecialAbilityUpdate::initiateIntentToDoSpecialPower: the module for the power starts over, packed, at the target
// object or position (none: its no-target command); its AI idles (the order is its own from now); unpacked already
// when it has no UnpackTime (or skips packing without a target). The unit's other mutually exclusive abilities (the
// Black Lotus' hacks and capture, charges, infantry capture, booby traps) end. Returns whether a module took it.
inline bool InitiateAbility(GameWorld &game, ecs::Entity unit, std::uint32_t power, ecs::Entity target, std::optional<Engine::Math::FixedVector3> position)
{
	using namespace ability_detail;
	auto *abilities = game.world.IsAlive(unit) ? game.world.Get<SpecialAbilities>(unit) : nullptr;
	if (abilities == nullptr)
		return false;
	AbilitySlot *slot = nullptr;
	for (std::uint8_t index = 0; index < abilities->count && slot == nullptr; ++index)
		if (abilities->slots[index].power == power)
			slot = &abilities->slots[index];
	if (slot == nullptr)
		return false;
	slot->target = {};
	slot->targetPos = {};
	slot->prepTicks = 0;
	slot->animTicks = 0;
	slot->animTotal = 0;
	slot->packing = AbilityPacking::Packed;
	slot->Set(ability_flag::FacingInitiated, false);
	slot->Set(ability_flag::FacingComplete, false);
	slot->Set(ability_flag::WithinRange, false);
	Look(game, unit, mc::Unpacking, false);
	Look(game, unit, mc::Packing, false);
	Look(game, unit, mc::FiringA, false);
	Look(game, unit, mc::RaisingFlag, false);
	if (target != ecs::Entity{})
		slot->target = target;
	else if (position)
		slot->targetPos = *position;
	if (!HasAi(game, unit))
		return false;
	AiIdle(game, unit);
	slot->Set(ability_flag::NoTargetCommand, target == ecs::Entity{} && !position);
	if (slot->unpackTicks == 0 || (slot->Has(ability_flag::NoTargetCommand) && slot->Option(ability_option::SkipPackingWithNoTarget)))
		slot->packing = AbilityPacking::Unpacked;
	slot->Set(ability_flag::Active, true);
	constexpr AbilityKind exclusive[] = {AbilityKind::BlackLotusDisableVehicle, AbilityKind::BlackLotusStealCash, AbilityKind::BlackLotusCaptureBuilding,
		AbilityKind::RemoteCharges, AbilityKind::TimedCharges, AbilityKind::InfantryCaptureBuilding, AbilityKind::BoobyTrap};
	for (const AbilityKind kind : exclusive)
		for (std::uint8_t index = 0; index < abilities->count; ++index)
			if (abilities->slots[index].kind == kind)
			{
				// findSpecialAbilityUpdate: the first of the kind.
				if (&abilities->slots[index] != slot)
				{
					Exit(game, unit, abilities->slots[index]);
					if (!abilities->slots[index].ObjectOption(special_object_option::Persistent))
						KillSpecialObjects(game, unit, index);
				}
				break;
			}
	return true;
}

// The tick's AbilityEvents, in order: a power fired (markSpecialPowerTriggered: no view object) or recharging; the
// effect on the target (triggerAbilityEffect: a capture of a building not already the unit's team's — a garrison only
// has its occupants put out; another goes over to the unit's player's default team, a tick undetected, and its owner
// hears it was stolen; a Black Lotus' power recharges from now; a trap on the target set off first holds it till its
// blast has landed; a charge or booby trap stuck on the target, a remote module with no target setting off its
// charges; a module's special objects destroyed as it leaves off); experience
// to the unit and skill points to its player; EVA, radar and presentation notices.
inline void ApplyAbilityEvents(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto *resource = game.world.FindResource<AbilityEvents>();
	if (resource == nullptr)
		return;
	std::vector<AbilityEvent> events;
	resource->AppendTo(events);
	resource->Reset(0);
	for (const AbilityEvent &event : events)
	{
		const bool unitAlive = game.world.IsAlive(event.unit);
		switch (event.kind)
		{
		case AbilityEvent::Kind::PowerTriggered:
			if (unitAlive)
				if (const auto *timers = game.world.Get<gp::SpecialPowerTimers>(event.unit); timers != nullptr && timers->Find(event.power) != nullptr)
					TriggerSpecialPower(game, event.unit, event.power, std::nullopt);
			break;
		case AbilityEvent::Kind::PowerRecharge:
			if (unitAlive)
				if (auto *timers = game.world.Get<gp::SpecialPowerTimers>(event.unit))
					if (auto *timer = timers->Find(event.power))
						gp::StartPowerRecharge(*timer, ClockFor(game, OwnerPlayer(game, event.unit)));
			break;
		// initLaser: from the unit (its SpecialObjectAttachToBone, drawn) to the target's centre (getCenterPosition: half
		// its height up, a sphere's own position), fixed there.
		case AbilityEvent::Kind::Laser:
			if (const auto slot = ability_detail::SlotOfPower(game, event.unit, event.power); slot && unitAlive && game.world.IsAlive(event.target))
			{
				const ecs::Entity laser = CreateSpecialObject(game, event.unit, *slot);
				if (!game.world.IsAlive(laser))
					break;
				Engine::Math::FixedVector3 end = game.world.Get<gp::Transform>(event.target)->position;
				if (const auto *ref = game.world.Get<gp::DefinitionRef>(event.target))
					if (const content::Geometry &shape = game.templates.DefinitionAt(ref->index).geometry; shape.shape != content::GeometryShape::Sphere)
						end.z = end.z + shape.height / Engine::Math::Fixed::FromInt(2);
				game.world.Add<AbilityLaser>(laser);
				*game.world.Get<AbilityLaser>(laser) = AbilityLaser{event.unit, end, event.power, 0};
			}
			break;
		case AbilityEvent::Kind::KillObjects:
			if (const auto slot = ability_detail::SlotOfPower(game, event.unit, event.power))
				KillSpecialObjects(game, event.unit, *slot);
			break;
		case AbilityEvent::Kind::IntoTarget:
			if (unitAlive && game.world.IsAlive(event.target))
			{
				if (!game.world.Has<gp::IgnoredObstacle>(event.unit))
					game.world.Add<gp::IgnoredObstacle>(event.unit);
				game.world.Get<gp::IgnoredObstacle>(event.unit)->obstacle = event.target;
				if (auto *route = game.world.Get<gp::Route>(event.unit))
					route->planned = false;
			}
			break;
		case AbilityEvent::Kind::TrapCheck:
			if (unitAlive && game.world.IsAlive(event.target))
				CheckAndDetonateBoobyTrap(game, event.target, event.unit);
			break;
		case AbilityEvent::Kind::Effect:
		{
			const ecs::Entity target = event.target;
			const auto slot = ability_detail::SlotOfPower(game, event.unit, event.power);
			if (!unitAlive || !slot)
				break;
			if (event.ability == AbilityKind::RemoteCharges && target == ecs::Entity{} && !event.positioned)
			{
				// No target nor position: the charges placed go off (detonate).
				for (const ecs::Entity charge : SpecialObjectsOf(game, event.unit, *slot))
					if (game.templates.StickyBombOf(game.world.Get<gp::DefinitionRef>(charge)->index) != nullptr)
						DetonateStickyBomb(game, charge);
				break;
			}
			// SPECIAL_HELIX_NAPALM_BOMB: the special object is the bomb (createSpecialObject).
			if (event.ability == AbilityKind::HelixNapalmBomb)
			{
				CreateSpecialObject(game, event.unit, *slot);
				break;
			}
			// SPECIAL_MISSILE_DEFENDER_LASER_GUIDED_MISSILES: with a SECONDARY weapon, it locks it (LOCKED_TEMPORARILY: until
			// the clip is empty or the attack is done) and its AI attacks the target (aiAttackObject, CMD_FROM_AI).
			if (event.ability == AbilityKind::LaserGuidedMissiles)
			{
				auto *set = game.world.Get<gp::WeaponSlots>(event.unit);
				auto *armament = game.world.Get<gp::Armament>(event.unit);
				if (game.world.IsAlive(target) && set != nullptr && armament != nullptr && set->slots[1].weapon != gp::WeaponCatalog::None)
				{
					gp::LockSlotTemporarily(*set, *armament, 1);
					OrderAttack(game, event.unit, target, 0, gameplay::CommandSource::Ai);
				}
				break;
			}
			// SPECIAL_HACKER_DISABLE_BUILDING / SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK: a target not allied to it is
			// DISABLED_HACKED for EffectDuration (setDisabledUntil). SPECIAL_BLACKLOTUS_STEAL_CASH_HACK: EffectValue (retail:
			// 1000, as every shipped one) or what its player has goes to the unit's (addMoneyEarned), floating over both;
			// the victim's EVA says so.
			// Its DisableFXParticleSystem over the target for the effect's time, every other hack of a small building (one
			// with a footprint under 300) for twice that (m_doDisableFXParticles toggled).
			if (event.ability == AbilityKind::HackerDisableBuilding || event.ability == AbilityKind::BlackLotusDisableVehicle)
			{
				if (game.world.IsAlive(target) && !ability_detail::AlliedTo(game, event.unit, target))
				{
					AbilitySlot &own = game.world.Get<SpecialAbilities>(event.unit)->slots[*slot];
					DisableHacked(game, target, game.tick + own.effectTicks);
					std::uint64_t interleave = 1;
					if (const auto *ref = game.world.Get<gp::DefinitionRef>(target))
					{
						const content::ObjectDefinition &victim = game.templates.DefinitionAt(ref->index);
						if (victim.Is("STRUCTURE") && ability_detail::FootprintArea(victim.geometry) < Engine::Math::Fixed::FromInt(300))
						{
							own.Set(ability_flag::NoDisableFx, !own.Has(ability_flag::NoDisableFx));
							interleave = 2;
						}
					}
					if (!own.Has(ability_flag::NoDisableFx))
						if (auto *notices = game.world.FindResource<AbilityNotices>())
							notices->disableFx.push_back({event.unit, target, own.power, own.effectTicks * interleave});
				}
				break;
			}
			if (event.ability == AbilityKind::BlackLotusStealCash)
			{
				if (game.world.IsAlive(target) && StealCash(game, event.unit, target, game.world.Get<SpecialAbilities>(event.unit)->slots[*slot].effectValue) > 0)
					if (auto *eva = game.world.FindResource<EvaNotices>())
						eva->list.push_back({EvaCue::CashStolen, EvaWeapon::None, OwnerPlayer(game, target)});
				break;
			}
			// SPECIAL_DISGUISE_AS_VEHICLE: the unit's StealthUpdate takes the target's disguise (disguiseAsObject).
			if (event.ability == AbilityKind::DisguiseAsVehicle)
			{
				if (game.world.IsAlive(target))
					DisguiseAsObject(game, event.unit, target);
				break;
			}
			if (!game.world.IsAlive(target) || ability_detail::HeldByTrap(game, event, *slot))
				break;
			if (event.ability == AbilityKind::TankHunterTnt || event.ability == AbilityKind::TimedCharges || event.ability == AbilityKind::BoobyTrap ||
				event.ability == AbilityKind::RemoteCharges)
			{
				// A booby trap never goes on something trapped already (an ally's).
				if (event.ability == AbilityKind::BoobyTrap && special_object_detail::HasStatus(game, target, special_object_detail::BoobyTrapped()))
					break;
				ability_detail::PlaceCharge(game, event.unit, *slot, target);
				break;
			}
			if (ability_detail::TeamOf(game, target) == ability_detail::TeamOf(game, event.unit))
				break;
			if (game.world.Has<gp::Garrison>(target))
			{
				selling_detail::PutOut(game, target, game.manifest.TakeAll(target));
				break;
			}
			if (auto *eva = game.world.FindResource<EvaNotices>())
				eva->list.push_back({EvaCue::BuildingStolen, EvaWeapon::None, OwnerPlayer(game, target)});
			const std::uint32_t player = OwnerPlayer(game, event.unit);
			Defect(game, target, DefaultTeamOf(game, event.unit, player), 1);
			if (event.ability == AbilityKind::BlackLotusCaptureBuilding)
				if (auto *timers = game.world.Get<gp::SpecialPowerTimers>(event.unit))
					if (auto *timer = timers->Find(event.power))
						gp::StartPowerRecharge(*timer, ClockFor(game, player));
			// The capturer's player's academy records a structure captured (recordBuildingCapture).
			RecordAcademy(game, player, AcademyCount::BuildingCapture);
			break;
		}
		case AbilityEvent::Kind::Award:
			if (unitAlive && event.amount != 0)
				AwardExperience(game, event.unit, event.amount, true);
			if (event.skill > 0)
				AddSkillPoints(game, event.player, event.skill);
			break;
		case AbilityEvent::Kind::Notice:
			if (auto *notices = game.world.FindResource<AbilityNotices>())
				notices->abilities.push_back({event.unit, event.power, event.cue, event.success});
			break;
		case AbilityEvent::Kind::Eva:
			if (auto *eva = game.world.FindResource<EvaNotices>())
				eva->list.push_back({EvaCue::BuildingBeingStolen, EvaWeapon::None, event.player});
			break;
		case AbilityEvent::Kind::Detect:
			// StealthUpdate::markAsDetected(): detected for its stealth delay from now; OrderIdleEnemiesToAttackMeUponReveal
			// wakes the idle enemies that see it (their next mood check now).
			if (unitAlive)
				if (auto *stealth = game.world.Get<gp::Stealth>(event.unit))
				{
					const auto *rider = game.world.Get<gp::StealthRider>(event.unit);
					if (gp::MarkAsDetected(*stealth, gp::stealth_detail::RulesOf(*stealth, rider), game.tick, 0))
						ability_detail::WakeIdleEnemies(game, event.unit);
				}
			break;
		case AbilityEvent::Kind::Infiltration:
			if (game.world.IsAlive(event.target))
				if (auto *notices = game.world.FindResource<InfiltrationNotices>())
					notices->list.push_back({event.player, 0u, game.world.Get<gp::Transform>(event.target)->position});
			break;
		}
	}
}
}
