export module games.generalszh.gameplay.abilities.systems.special_ability_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.abilities.components.special_abilities;
export import games.generalszh.gameplay.abilities.resources.ability_notices;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.navigation.components.navigation;
export import engine.gameplay.rts.combat.components.aggression;
export import engine.gameplay.rts.construction.components.builder;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.rts.stealth.components.stealth;
export import engine.gameplay.rts.powers.components.special_power_timers;
export import engine.gameplay.rts.powers.resources.shared_power_timers;
export import engine.gameplay.rts.powers.algorithms.special_power_timing;
import engine.gameplay.common.spatial.algorithms.geometry_collision;
import games.generalszh.content.objects.model_conditions;
import games.generalszh.content.powers.special_ability_content;
import Engine.Core.Math.FixedRandom;

// SpecialAbilityUpdate::update for every object with special abilities, chunk-parallel: each unit's abilities step on
// through approach, facing, unpacking, preparation, trigger, packing and finish, writing only the unit's own components.
// What reaches beyond the unit goes out as its chunk's AbilityEvents, in order, for the session to carry out once the
// systems have run (ApplyAbilityEvents): its power fired (markSpecialPowerTriggered) or recharging, the effect on its
// target (a capture), experience and skill points, EVA, the radar and the presentation's notices. Targets are only read
// (a Lookup); random numbers come from a stream keyed by the tick and the unit, so any number of workers agrees.
export namespace generalszh::gameplay
{
// Something an ability did beyond its unit, for ApplyAbilityEvents.
struct AbilityEvent
{
	enum class Kind : std::uint8_t
	{
		PowerTriggered, // markSpecialPowerTriggered(nullptr): the scripts hear it, its recharge starts
		PowerRecharge,  // startPowerRecharge
		Effect,         // triggerAbilityEffect's effect on the target (captures, charges, booby traps)
		KillObjects,    // killSpecialObjects (onExit of a module whose special objects are not persistent)
		TrapCheck,      // the target's booby trap set off by the unit (an infantry capture's startPreparation)
		IntoTarget,     // a contact approach routes into its target (its cells open to the unit's route searches)
		Award,          // AwardXPForTriggering / SkillPointsForTriggering (`amount`, `skill`)
		Notice,         // an AbilityNotice (`cue`, `success`)
		Eva,            // an EVA cue for `player`
		Infiltration,   // Radar::tryInfiltrationEvent for `player` at the target
		Detect,         // LoseStealthOnTrigger: the unit's StealthUpdate::markAsDetected
	};
	ecs::Entity unit;
	ecs::Entity target;
	std::uint32_t power{0};
	std::int32_t amount{0};
	std::int32_t skill{0};
	std::uint32_t player{0};
	Kind kind{Kind::Notice};
	AbilityKind ability{AbilityKind::Other};
	AbilityCue cue{AbilityCue::Unpack};
	std::uint8_t eva{0};
	bool success{false};
	bool positioned{false}; // Effect: the module had a target position (m_targetPos)
	bool afterTrap{false};  // Effect: going on after the target's booby trap went off (its blast landed)
};

struct AbilityEvents : ecs::ChunkOutputs<AbilityEvent>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::AbilityEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.ability_events";
};
}

export namespace generalszh::gameplay
{

namespace ability_system_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;
using Engine::Math::FixedVector3;

namespace mc
{
inline constexpr std::uint32_t Unpacking = content::ModelConditionBit("UNPACKING");
inline constexpr std::uint32_t Packing = content::ModelConditionBit("PACKING");
inline constexpr std::uint32_t FiringA = content::ModelConditionBit("FIRING_A");
inline constexpr std::uint32_t RaisingFlag = content::ModelConditionBit("RAISING_FLAG");
}

using TargetLookup = ecs::Lookup<ecs::Read<gp::Transform>, ecs::Read<gp::TeamMember>, ecs::Read<gp::Owner>, ecs::Read<gp::DefinitionRef>,
	ecs::Read<gp::Stealth>, ecs::Read<gp::Health>, ecs::Read<gp::Dying>>;

// What the system reads beyond the unit, and where its events go.
struct Env
{
	const ecs::EntityLookup<TargetLookup> &lookup;
	const ObjectTemplates &templates;
	const gp::Relationships &relationships;
	const gp::GroundHeight &ground;
	const gp::SpatialIndex &spatial;
	const gp::SpecialPowerRules &rules;
	const gp::SharedPowerTimers &shared;
	std::vector<AbilityEvent> &events;
	std::uint64_t tick;
	std::uint64_t seed;
};

// The unit's own components (its row).
struct Unit
{
	ecs::Entity entity;
	gp::Transform &transform;
	gp::Locomotion *motion;
	gp::MoveOrder *order;
	gp::Route *route;
	gp::AttackTarget *attack;
	gp::Aggression *aggression;
	gp::AiActivity &activity;
	gp::Appearance *look;
	std::uint32_t player;
	std::uint32_t team;
	std::uint32_t definition;
	bool building;
	const gp::SpecialPowerTimers *timers;
	bool dead;
};

inline bool IsCapture(AbilityKind kind) noexcept
{
	return kind == AbilityKind::InfantryCaptureBuilding || kind == AbilityKind::BlackLotusCaptureBuilding;
}

inline void Look(Unit &unit, std::uint32_t bit, bool on)
{
	if (unit.look != nullptr)
		unit.look->Set(bit, on);
}

inline void Emit(Env &env, Unit &unit, const AbilitySlot &slot, AbilityEvent event)
{
	event.unit = unit.entity;
	event.power = slot.power;
	event.ability = slot.kind;
	if (event.target == ecs::Entity{})
		event.target = slot.target;
	env.events.push_back(event);
}

inline void Notify(Env &env, Unit &unit, const AbilitySlot &slot, AbilityCue cue, bool success = false)
{
	AbilityEvent event;
	event.kind = AbilityEvent::Kind::Notice;
	event.cue = cue;
	event.success = success;
	Emit(env, unit, slot, event);
}

// The target, while it is in the world (findObjectByID: dying objects are still there).
inline ecs::Entity TargetOf(const Env &env, const AbilitySlot &slot)
{
	return slot.target != ecs::Entity{} && env.lookup.IsAlive(slot.target) ? slot.target : ecs::Entity{};
}

inline bool HasPosition(const AbilitySlot &slot) noexcept
{
	return slot.targetPos.x != Fixed{} || slot.targetPos.y != Fixed{} || slot.targetPos.z != Fixed{};
}

inline std::uint32_t TeamOf(const Env &env, ecs::Entity entity)
{
	const auto *member = env.lookup.Get<gp::TeamMember>(entity);
	return member != nullptr ? member->team : 0xFFFFFFFFu;
}

inline std::uint32_t PlayerOf(const Env &env, ecs::Entity entity)
{
	const auto *owner = env.lookup.Get<gp::Owner>(entity);
	return owner != nullptr ? owner->player : 0u;
}

inline const content::ObjectDefinition *DefinitionOf(const Env &env, ecs::Entity entity)
{
	const auto *ref = env.lookup.Get<gp::DefinitionRef>(entity);
	return ref != nullptr ? &env.templates.DefinitionAt(ref->index) : nullptr;
}

inline Fixed CircleRadius(const content::ObjectDefinition *kind)
{
	return kind != nullptr ? content::BoundingCircleRadius(kind->geometry) : Fixed{};
}

// GeometryInfo::getMaxHeightAbovePosition: a sphere's radius, else its height.
inline Fixed TopAbovePosition(const content::ObjectDefinition *kind)
{
	if (kind == nullptr)
		return {};
	return kind->geometry.shape == content::GeometryShape::Sphere ? kind->geometry.majorRadius : kind->geometry.height;
}

// Object::isEffectivelyDead.
inline bool EffectivelyDead(const Env &env, ecs::Entity entity)
{
	if (env.lookup.Get<gp::Dying>(entity) != nullptr)
		return true;
	const auto *health = env.lookup.Get<gp::Health>(entity);
	return health != nullptr && gp::IsDead(*health);
}

// ThePartitionManager->getDistanceSquared(..., FROM_BOUNDINGSPHERE_2D): between the bounding circles (the unit's and the
// target object's; a position has none), never below zero, squared.
inline Fixed BoundaryDistanceSquared(const Env &env, const Unit &unit, FixedVector2 to, ecs::Entity target)
{
	const Fixed radii = CircleRadius(&env.templates.DefinitionAt(unit.definition)) + (target != ecs::Entity{} ? CircleRadius(DefinitionOf(env, target)) : Fixed{});
	const Fixed apart = Engine::Math::Length(to - unit.transform.position.XY()) - radii;
	return apart > Fixed{} ? apart * apart : Fixed{};
}

// Object::getRelationship (the teams' overrides, then the players').
inline bool Allied(const Env &env, const Unit &unit, ecs::Entity other)
{
	const auto *member = env.lookup.Get<gp::TeamMember>(other);
	return env.relationships.Allies(unit.team, unit.player, member != nullptr ? member->team : gp::Relationships::NoTeam, PlayerOf(env, other));
}

inline bool Hidden(const Env &env, ecs::Entity entity)
{
	const auto *stealth = env.lookup.Get<gp::Stealth>(entity);
	return stealth != nullptr && stealth->Hidden();
}

inline bool HasAi(const Unit &unit) noexcept { return unit.order != nullptr; }

// AIUpdateInterface::isMoving: under way somewhere (turning to face something is not).
inline bool Moving(const Unit &unit) noexcept
{
	return unit.order != nullptr &&
		(unit.order->mode == gp::MoveMode::Point || unit.order->mode == gp::MoveMode::Path || unit.order->mode == gp::MoveMode::PathExact || unit.order->mode == gp::MoveMode::Direct ||
		gp::Wandering(unit.order->mode) || unit.order->mode == gp::MoveMode::WanderInPlace);
}

// AIUpdateInterface::isIdle, as the port has states.
inline bool IsIdle(const Unit &unit, const gp::Builder *builder) noexcept
{
	if (unit.order != nullptr && unit.order->mode != gp::MoveMode::Idle)
		return false;
	if (unit.attack != nullptr && unit.attack->target != ecs::Entity{})
		return false;
	if (unit.activity.busy != 0)
		return false;
	return builder == nullptr;
}

// aiIdle(CMD_FROM_AI): it stops, stops attacking, guarding and hunting end, not busy, its last order its AI's.
inline void AiIdle(Unit &unit)
{
	if (unit.route != nullptr)
		unit.route->planned = false;
	if (unit.order != nullptr)
		unit.order->mode = gp::MoveMode::Idle;
	if (unit.attack != nullptr)
		*unit.attack = {};
	if (unit.aggression != nullptr && unit.aggression->stance != gp::Stance::Hold)
		unit.aggression->stance = gp::Stance::Idle;
	unit.activity.busy = 0;
	unit.activity.commanded = 0;
}

// aiBusy(CMD_FROM_AI).
inline void AiBusyOn(Unit &unit)
{
	AiIdle(unit);
	unit.activity.busy = 1;
}

inline bool HasPower(const Unit &unit, const AbilitySlot &slot)
{
	return unit.timers != nullptr && unit.timers->Find(slot.power) != nullptr;
}

// GameLogicRandomValueReal(1 - PackUnpackVariationFactor, 1 + PackUnpackVariationFactor) times the time, truncated (a
// stream of its own per unit and tick).
inline std::uint32_t Varied(const Env &env, const Unit &unit, const AbilitySlot &slot, std::uint32_t ticks)
{
	auto random = Engine::Math::Stream(env.seed, {env.tick, unit.entity.index, unit.entity.generation, 0xAB111u});
	const Fixed factor = Engine::Math::UniformFixed(random, Fixed::One() - slot.variation, Fixed::One() + slot.variation);
	return static_cast<std::uint32_t>(std::max<std::int64_t>(0, (Fixed::FromInt(ticks) * factor).Floor()));
}

inline bool PreparationComplete(const AbilitySlot &slot) noexcept { return slot.prepTicks == 0; }
inline bool Persistent(const AbilitySlot &slot) noexcept { return slot.persistentPrepTicks > 0; }

inline bool NeedToPack(const AbilitySlot &slot) noexcept
{
	return slot.packing == AbilityPacking::Unpacked && !(slot.Option(ability_option::SkipPackingWithNoTarget) && slot.Has(ability_flag::NoTargetCommand)) &&
		slot.packTicks != 0;
}
inline bool NeedToUnpack(const AbilitySlot &slot) noexcept
{
	return slot.packing == AbilityPacking::Packed && !(slot.Option(ability_option::SkipPackingWithNoTarget) && slot.Has(ability_flag::NoTargetCommand)) &&
		slot.unpackTicks != 0;
}

inline bool InUse(const AbilitySlot &slot) noexcept { return slot.packing != AbilityPacking::None && slot.Has(ability_flag::WithinRange); }

// isWithinStartAbilityRange (see special_ability_update for the rules).
// A body for the collide test, as its geometry and where it stands.
inline gp::CollisionBody BodyOf(const content::ObjectDefinition &kind, const FixedVector3 &position, Engine::Math::TurnAngle angle)
{
	const gp::BodyShape shape = kind.geometry.shape == content::GeometryShape::Box ? gp::BodyShape::Box
		: kind.geometry.shape == content::GeometryShape::Cylinder                  ? gp::BodyShape::Cylinder
																				   : gp::BodyShape::Sphere;
	return {position, angle, shape, kind.geometry.majorRadius, kind.geometry.minorRadius, kind.geometry.height};
}

// ThePartitionManager->iteratePotentialCollisions(position, geometry, 0) finding the target: within 1.1 of the unit's
// bounding sphere (bounding spheres apart, in 2D) and touching it (PartitionFilterWouldCollide; the unit unturned).
inline bool Touching(const Env &env, const Unit &unit, ecs::Entity target)
{
	const content::ObjectDefinition &self = env.templates.DefinitionAt(unit.definition);
	const content::ObjectDefinition *other = DefinitionOf(env, target);
	const auto *at = env.lookup.Get<gp::Transform>(target);
	if (other == nullptr || at == nullptr)
		return false;
	const Fixed reach = content::BoundingSphereRadius(self.geometry) * Fixed::FromRatio(11, 10);
	const Fixed apart = Engine::Math::Length(at->position.XY() - unit.transform.position.XY()) - content::BoundingSphereRadius(self.geometry) -
		content::BoundingSphereRadius(other->geometry);
	if (apart > reach)
		return false;
	return gp::WouldCollide(BodyOf(self, unit.transform.position, {}), BodyOf(*other, at->position, at->facing));
}

inline bool WithinStartRange(const Env &env, const Unit &unit, AbilitySlot &slot)
{
	if (slot.Has(ability_flag::WithinRange))
		return true;
	const Fixed range = std::max(Fixed{}, slot.startRange - Fixed::FromRatio(5, 2));
	const ecs::Entity target = slot.target != ecs::Entity{} ? TargetOf(env, slot) : ecs::Entity{};
	Fixed distance;
	if (slot.target != ecs::Entity{})
	{
		if (target != ecs::Entity{})
			distance = BoundaryDistanceSquared(env, unit, env.lookup.Get<gp::Transform>(target)->position.XY(), target);
	}
	else if (HasPosition(slot))
		distance = BoundaryDistanceSquared(env, unit, slot.targetPos.XY(), {});
	else
		return true;
	if (distance > slot.startRange * slot.startRange)
		return false;
	// A start range under a quarter cell is a contact approach: it must run into its target first.
	if (range == Fixed{} && slot.target != ecs::Entity{})
		return target != ecs::Entity{} && Touching(env, unit, target);
	if (!slot.Option(ability_option::ApproachRequiresLos))
		return true;
	if (target == ecs::Entity{} || distance > range * range)
		return false;
	FixedVector3 from = unit.transform.position;
	FixedVector3 to = env.lookup.Get<gp::Transform>(target)->position;
	from.z += TopAbovePosition(&env.templates.DefinitionAt(unit.definition));
	to.z += TopAbovePosition(DefinitionOf(env, target));
	return env.ground.ClearLineOfSight(from, to);
}

inline bool WithinAbortRange(const Env &env, const Unit &unit, const AbilitySlot &slot)
{
	Fixed distance;
	if (slot.target != ecs::Entity{})
	{
		if (const ecs::Entity target = TargetOf(env, slot); target != ecs::Entity{})
			distance = BoundaryDistanceSquared(env, unit, env.lookup.Get<gp::Transform>(target)->position.XY(), target);
	}
	else if (HasPosition(slot))
		distance = BoundaryDistanceSquared(env, unit, slot.targetPos.XY(), {});
	else
		return true;
	return distance <= slot.abortRange * slot.abortRange;
}

inline void EndPreparation(Env &env, Unit &unit, const AbilitySlot &slot)
{
	unit.activity.usingAbility = 0;
	Notify(env, unit, slot, AbilityCue::PreparationEnd);
}

inline void Exit(Env &env, Unit &unit, AbilitySlot &slot)
{
	Look(unit, mc::Unpacking, false);
	Look(unit, mc::Packing, false);
	Look(unit, mc::FiringA, false);
	Look(unit, mc::RaisingFlag, false);
	EndPreparation(env, unit, slot);
	// Special objects that are not persistent go as the module leaves off (those that do not outlive their owner go as
	// it dies: ApplySpecialObjectCasualties).
	if (!slot.ObjectOption(special_object_option::Persistent))
	{
		AbilityEvent kill;
		kill.kind = AbilityEvent::Kind::KillObjects;
		Emit(env, unit, slot, kill);
	}
	slot.Set(ability_flag::Active, false);
	slot.Set(ability_flag::WithinRange, false);
	slot.packing = AbilityPacking::None;
}

// finishAbility: flee FleeRangeAfterCompletion (from its own nearest mine within that range, that far beyond it), else
// idle; then onExit.
inline void Finish(Env &env, Unit &unit, AbilitySlot &slot)
{
	slot.Set(ability_flag::WithinRange, false);
	slot.packing = AbilityPacking::None;
	const bool validTarget = HasPosition(slot) || slot.target != ecs::Entity{};
	if (slot.fleeRange != Fixed{} && validTarget && HasAi(unit))
	{
		const FixedVector2 direction{Engine::Math::Cos(unit.transform.facing) * slot.fleeRange, Engine::Math::Sin(unit.transform.facing) * slot.fleeRange};
		FixedVector2 to = unit.transform.position.XY();
		to = slot.Option(ability_option::FlipAfterUnpacking) || slot.Option(ability_option::FlipAfterPacking) ? to + direction : to - direction;
		// getClosestObject(FROM_CENTER_2D) of its player's MINEs within the range.
		const gp::SpatialEntry *mine = nullptr;
		Fixed best;
		env.spatial.ForEachWithin(to, slot.fleeRange, [&](const gp::SpatialEntry &entry) {
			if (entry.player != unit.player || (entry.classes & gp::target_class::Mine) == 0)
				return;
			const Fixed distance = Engine::Math::DistanceSquared(entry.position.XY(), to);
			if (distance <= slot.fleeRange * slot.fleeRange && (mine == nullptr || distance < best))
			{
				mine = &entry;
				best = distance;
			}
		});
		if (mine != nullptr)
		{
			const FixedVector2 away = to - mine->position.XY();
			const Fixed length = Engine::Math::Length(away);
			const FixedVector2 along = length > Fixed{} ? FixedVector2{away.x / length, away.y / length} : FixedVector2{};
			to = mine->position.XY() + FixedVector2{along.x * slot.fleeRange, along.y * slot.fleeRange};
		}
		AiIdle(unit);
		*unit.order = gp::MoveToPoint(to);
	}
	else
		AiIdle(unit);
	Exit(env, unit, slot);
}

inline void StartPacking(Env &env, Unit &unit, AbilitySlot &slot, bool success)
{
	slot.packing = AbilityPacking::Packing;
	slot.animTicks = Varied(env, unit, slot, slot.packTicks);
	slot.animTotal = slot.animTicks;
	Look(unit, mc::Unpacking, false);
	Look(unit, mc::RaisingFlag, false);
	Look(unit, mc::Packing, true);
	Notify(env, unit, slot, AbilityCue::Pack, success);
	AiBusyOn(unit);
}

inline void StartUnpacking(Env &env, Unit &unit, AbilitySlot &slot)
{
	slot.packing = AbilityPacking::Unpacking;
	slot.animTicks = Varied(env, unit, slot, slot.unpackTicks);
	slot.animTotal = slot.animTicks;
	Look(unit, mc::Packing, false);
	Look(unit, mc::Unpacking, true);
	Notify(env, unit, slot, AbilityCue::Unpack);
	AiBusyOn(unit);
}

inline bool HandlePacking(Env &env, Unit &unit, AbilitySlot &slot)
{
	if (slot.animTicks == 0)
		return false;
	if (--slot.animTicks == 0)
	{
		Look(unit, mc::Unpacking, false);
		Look(unit, mc::Packing, false);
		if (slot.packing == AbilityPacking::Unpacking)
		{
			if (slot.Option(ability_option::FlipAfterUnpacking))
				unit.transform.facing += Engine::Math::TurnAngle{0x80000000u};
			slot.packing = AbilityPacking::Unpacked;
		}
		else if (slot.packing == AbilityPacking::Packing)
		{
			if (slot.Option(ability_option::FlipAfterPacking))
				unit.transform.facing += Engine::Math::TurnAngle{0x80000000u};
			slot.packing = AbilityPacking::Packed;
			Finish(env, unit, slot);
			return true;
		}
		return false;
	}
	// LoseStealthOnTrigger: in the last PreTriggerUnstealthTime of its packing or unpacking it is revealed, each frame.
	if (slot.Option(ability_option::LoseStealthOnTrigger) && slot.animTicks < slot.preTriggerUnstealthTicks)
	{
		AbilityEvent detect;
		detect.kind = AbilityEvent::Kind::Detect;
		Emit(env, unit, slot, detect);
	}
	return true;
}

inline bool IsFacing(Unit &unit, AbilitySlot &slot, const gp::Builder *builder)
{
	if (!HasAi(unit))
		return true;
	if (!slot.Has(ability_flag::FacingComplete) && slot.Has(ability_flag::FacingInitiated))
	{
		if (IsIdle(unit, builder))
		{
			slot.Set(ability_flag::FacingComplete, true);
			return false;
		}
		return true;
	}
	return false;
}

inline bool NeedToFace(const Unit &unit, const AbilitySlot &slot)
{
	return HasAi(unit) && slot.Option(ability_option::NeedToFaceTarget) && (!slot.Has(ability_flag::FacingInitiated) || !slot.Has(ability_flag::FacingComplete));
}

inline void StartFacing(Env &env, Unit &unit, AbilitySlot &slot)
{
	if (!HasAi(unit))
		return;
	AiIdle(unit);
	if (unit.motion != nullptr)
		unit.motion->speed = {};
	slot.Set(ability_flag::FacingInitiated, true);
	if (const ecs::Entity target = TargetOf(env, slot); target != ecs::Entity{})
		*unit.order = gp::FaceToward(env.lookup.Get<gp::Transform>(target)->position.XY());
	else if (HasPosition(slot))
		*unit.order = gp::FaceToward(slot.targetPos.XY());
}

inline void Approach(Env &env, Unit &unit, AbilitySlot &slot)
{
	if (unit.order == nullptr)
		return;
	if (slot.target != ecs::Entity{})
	{
		if (const ecs::Entity target = TargetOf(env, slot); target != ecs::Entity{})
		{
			AiIdle(unit);
			*unit.order = gp::MoveToPoint(env.lookup.Get<gp::Transform>(target)->position.XY());
			// A contact approach (start range under a quarter cell) must run into its target: the port's router would stop
			// it at the free cell beside it, so it routes through the target's cells (as a contact weapon's attack).
			if (slot.startRange <= Fixed::FromRatio(5, 2))
			{
				AbilityEvent into;
				into.kind = AbilityEvent::Kind::IntoTarget;
				into.target = target;
				Emit(env, unit, slot, into);
			}
		}
	}
	else if (HasPosition(slot))
	{
		AiIdle(unit);
		*unit.order = gp::MoveToPoint(slot.targetPos.XY());
	}
}

inline void StartPreparation(Env &env, Unit &unit, AbilitySlot &slot)
{
	slot.prepTicks = slot.preparationTicks;
	const ecs::Entity target = TargetOf(env, slot);
	const auto warn = [&](bool eva) {
		const std::uint32_t victim = PlayerOf(env, target);
		if (eva)
		{
			AbilityEvent event;
			event.kind = AbilityEvent::Kind::Eva;
			event.player = victim;
			event.eva = 1; // BuildingBeingStolen
			Emit(env, unit, slot, event);
		}
		AbilityEvent radar;
		radar.kind = AbilityEvent::Kind::Infiltration;
		radar.player = victim;
		Emit(env, unit, slot, radar);
	};
	if (slot.kind == AbilityKind::InfantryCaptureBuilding)
	{
		if (target != ecs::Entity{} && TeamOf(env, target) == unit.team)
			return;
		// checkAndDetonateBoobyTrap: the building's trap goes off at the unit (its blast lands with the next tick's).
		if (target != ecs::Entity{})
		{
			AbilityEvent check;
			check.kind = AbilityEvent::Kind::TrapCheck;
			Emit(env, unit, slot, check);
		}
		Look(unit, mc::Unpacking, false);
		Look(unit, mc::RaisingFlag, true);
		if (target != ecs::Entity{})
			warn(true);
	}
	else if (slot.kind == AbilityKind::HackerDisableBuilding || slot.kind == AbilityKind::BlackLotusCaptureBuilding ||
		slot.kind == AbilityKind::BlackLotusDisableVehicle || slot.kind == AbilityKind::BlackLotusStealCash)
	{
		if (target != ecs::Entity{})
		{
			if (Allied(env, unit, target))
				return;
			// Its laser special object (FIRING_A while it streams) is not ported.
			warn(slot.kind == AbilityKind::BlackLotusCaptureBuilding);
		}
	}
	if (HasPower(unit, slot))
	{
		AbilityEvent event;
		event.kind = AbilityEvent::Kind::PowerTriggered;
		Emit(env, unit, slot, event);
	}
	if (HasAi(unit))
		AiIdle(unit);
	unit.activity.usingAbility = 1;
	Notify(env, unit, slot, AbilityCue::PreparationStart);
}

inline bool ContinuePreparation(Env &env, Unit &unit, AbilitySlot &slot)
{
	if (slot.abortRange < Fixed::FromInt(content::SpecialAbilityContent::HugeDistance) && !WithinAbortRange(env, unit, slot))
		return false;
	const ecs::Entity target = TargetOf(env, slot);
	switch (slot.kind)
	{
	case AbilityKind::LaserGuidedMissiles:
	case AbilityKind::BlackLotusDisableVehicle:
	case AbilityKind::InfantryCaptureBuilding:
	case AbilityKind::BlackLotusCaptureBuilding:
		if (target == ecs::Entity{} || Allied(env, unit, target))
			return false;
		if (slot.kind == AbilityKind::InfantryCaptureBuilding && HasPower(unit, slot))
		{
			AbilityEvent event;
			event.kind = AbilityEvent::Kind::PowerRecharge;
			Emit(env, unit, slot, event);
		}
		break;
	default:
		break;
	}
	return true;
}

inline void Trigger(Env &env, Unit &unit, AbilitySlot &slot)
{
	const std::int32_t skill = slot.skillPoints != -1 ? slot.skillPoints : slot.awardXp;
	if (slot.awardXp != 0 || skill > 0)
	{
		AbilityEvent award;
		award.kind = AbilityEvent::Kind::Award;
		award.amount = slot.awardXp;
		award.skill = skill;
		award.player = unit.player;
		Emit(env, unit, slot, award);
	}
	Notify(env, unit, slot, AbilityCue::Trigger);
	switch (slot.kind)
	{
	case AbilityKind::InfantryCaptureBuilding:
	case AbilityKind::BlackLotusCaptureBuilding:
	case AbilityKind::TankHunterTnt:
	case AbilityKind::TimedCharges:
	case AbilityKind::BoobyTrap:
	case AbilityKind::RemoteCharges:
	case AbilityKind::DisguiseAsVehicle:
	case AbilityKind::HackerDisableBuilding:
	case AbilityKind::BlackLotusDisableVehicle:
	case AbilityKind::BlackLotusStealCash:
	case AbilityKind::LaserGuidedMissiles:
	case AbilityKind::HelixNapalmBomb:
	{
		AbilityEvent effect;
		effect.kind = AbilityEvent::Kind::Effect;
		effect.positioned = HasPosition(slot);
		Emit(env, unit, slot, effect);
		break;
	}
	default:
		break;
	}
	// LoseStealthOnTrigger: it is revealed as it triggers (markAsDetected), unless it set its remote charges off.
	const bool detonating = slot.kind == AbilityKind::RemoteCharges && slot.target == ecs::Entity{} && !HasPosition(slot);
	if (slot.Option(ability_option::LoseStealthOnTrigger) && !detonating)
	{
		AbilityEvent detect;
		detect.kind = AbilityEvent::Kind::Detect;
		Emit(env, unit, slot, detect);
	}
}

inline void AfterPreparation(Env &env, Unit &unit, AbilitySlot &slot, bool success)
{
	EndPreparation(env, unit, slot);
	if (NeedToPack(slot))
		StartPacking(env, unit, slot, success);
	else
		Finish(env, unit, slot);
}

inline bool TargetAborts(const Env &env, const Unit &unit, const AbilitySlot &slot)
{
	const ecs::Entity target = TargetOf(env, slot);
	if (target == ecs::Entity{})
		return false;
	if (EffectivelyDead(env, target))
		return true;
	switch (slot.kind)
	{
	case AbilityKind::InfantryCaptureBuilding:
	case AbilityKind::BlackLotusCaptureBuilding:
	case AbilityKind::HackerDisableBuilding:
		if (TeamOf(env, target) == unit.team)
			return true;
		[[fallthrough]];
	case AbilityKind::BlackLotusStealCash:
	case AbilityKind::BoobyTrap:
		return Hidden(env, target) && !PreparationComplete(slot);
	case AbilityKind::RemoteCharges:
	case AbilityKind::TimedCharges:
		return !NeedToUnpack(slot) && Hidden(env, target) && !PreparationComplete(slot);
	case AbilityKind::LaserGuidedMissiles:
		if (const auto *kind = DefinitionOf(env, target); kind != nullptr && kind->Is("STRUCTURE"))
			return true;
		[[fallthrough]];
	case AbilityKind::BlackLotusDisableVehicle:
		return Hidden(env, target);
	default:
		return false;
	}
}

// update, one module.
inline void UpdateSlot(Env &env, Unit &unit, AbilitySlot &slot, const gp::Builder *builder)
{
	// A trigger whose target's booby trap went off last tick goes on, its blast landed.
	if (slot.trapTarget != ecs::Entity{})
	{
		AbilityEvent effect;
		effect.kind = AbilityEvent::Kind::Effect;
		effect.target = slot.trapTarget;
		effect.afterTrap = true;
		Emit(env, unit, slot, effect);
		slot.trapTarget = {};
		if (!slot.Has(ability_flag::Active))
			return;
	}
	if (unit.dead)
	{
		Exit(env, unit, slot);
		return;
	}
	if (!slot.Has(ability_flag::Active))
		return;
	if (!HasAi(unit) || unit.activity.commanded != 0)
	{
		Exit(env, unit, slot);
		return;
	}
	// A capture is broken by moving off, as by an order (turning to face its target is no moving off).
	if (Moving(unit) && InUse(slot) && !slot.Has(ability_flag::FacingInitiated) && IsCapture(slot.kind))
	{
		Exit(env, unit, slot);
		return;
	}
	if (HandlePacking(env, unit, slot))
		return;
	if (TargetAborts(env, unit, slot) || !HasPower(unit, slot))
	{
		AiIdle(unit);
		Exit(env, unit, slot);
		return;
	}
	// aiFaceObject follows its target while it turns.
	if (slot.Has(ability_flag::FacingInitiated) && !slot.Has(ability_flag::FacingComplete) && unit.order->mode == gp::MoveMode::Face)
		if (const ecs::Entity target = TargetOf(env, slot); target != ecs::Entity{})
			unit.order->destination = env.lookup.Get<gp::Transform>(target)->position.XY();
	if (!PreparationComplete(slot))
	{
		// A persistent ability that needs its power recharged waits for it (ready, and ready before this tick).
		bool ready = true;
		if (Persistent(slot) && slot.Option(ability_option::PersistenceRequiresRecharge))
		{
			const gp::SpecialPowerTimer &timer = *unit.timers->Find(slot.power);
			ready = gp::PeekIsReady(timer, env.rules, env.shared, unit.player, env.tick) &&
				gp::PeekReadyFrame(timer, false, env.rules, env.shared, unit.player, env.tick) < env.tick;
		}
		if (ready)
			--slot.prepTicks;
		if (PreparationComplete(slot))
		{
			Trigger(env, unit, slot);
			if (Persistent(slot))
			{
				slot.prepTicks = slot.persistentPrepTicks;
				if (slot.Option(ability_option::PersistenceRequiresRecharge))
				{
					AbilityEvent event;
					event.kind = AbilityEvent::Kind::PowerRecharge;
					Emit(env, unit, slot, event);
				}
			}
			else
				AfterPreparation(env, unit, slot, true);
		}
		else if (!ContinuePreparation(env, unit, slot))
			AfterPreparation(env, unit, slot, false);
	}
	else if (WithinStartRange(env, unit, slot))
	{
		slot.Set(ability_flag::WithinRange, true);
		if (!IsFacing(unit, slot, builder) && NeedToFace(unit, slot))
		{
			StartFacing(env, unit, slot);
			return;
		}
		if (NeedToUnpack(slot))
		{
			StartUnpacking(env, unit, slot);
			return;
		}
		if (slot.packing == AbilityPacking::Unpacked)
		{
			StartPreparation(env, unit, slot);
			if (PreparationComplete(slot))
			{
				Trigger(env, unit, slot);
				if (Persistent(slot) && slot.Option(ability_option::PersistenceRequiresRecharge))
				{
					slot.prepTicks = slot.persistentPrepTicks;
					AbilityEvent event;
					event.kind = AbilityEvent::Kind::PowerRecharge;
					Emit(env, unit, slot, event);
					return;
				}
				AfterPreparation(env, unit, slot, true);
			}
		}
	}
	else if (IsIdle(unit, builder))
		Approach(env, unit, slot);
}
}

struct SpecialAbilitySystem
{
	using Query = ecs::Query<ecs::Write<SpecialAbilities>, ecs::Write<engine::gameplay::AiActivity>, ecs::Write<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Optional<engine::gameplay::TeamMember>,
		ecs::OptionalWrite<engine::gameplay::Locomotion>, ecs::OptionalWrite<engine::gameplay::MoveOrder>, ecs::OptionalWrite<engine::gameplay::Route>,
		ecs::OptionalWrite<engine::gameplay::AttackTarget>, ecs::OptionalWrite<engine::gameplay::Aggression>, ecs::OptionalWrite<engine::gameplay::Appearance>,
		ecs::Optional<engine::gameplay::Builder>, ecs::Optional<engine::gameplay::SpecialPowerTimers>, ecs::Optional<engine::gameplay::Health>,
		ecs::Optional<engine::gameplay::Dying>>;
	using Lookup = ability_system_detail::TargetLookup;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::Relationships>, ecs::Read<engine::gameplay::GroundHeight>,
		ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::SpecialPowerRules>, ecs::Read<engine::gameplay::SharedPowerTimers>,
		ecs::Read<engine::gameplay::RandomSeed>, ecs::Write<AbilityEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<AbilityEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using namespace ability_system_detail;
		const auto lookup = context.Lookup<Lookup>();
		Env env{lookup, context.Read<ObjectTemplates>(), context.Read<gp::Relationships>(), context.Read<gp::GroundHeight>(), context.Read<gp::SpatialIndex>(),
			context.Read<gp::SpecialPowerRules>(), context.Read<gp::SharedPowerTimers>(), context.Write<AbilityEvents>().Slot(context), context.Tick(),
			context.Read<gp::RandomSeed>().value};
		auto abilities = chunk.Get<SpecialAbilities>();
		auto activities = chunk.Get<gp::AiActivity>();
		auto transforms = chunk.Get<gp::Transform>();
		const auto owners = chunk.Get<gp::Owner>();
		const auto refs = chunk.Get<gp::DefinitionRef>();
		const auto members = chunk.Get<gp::TeamMember>();
		auto motions = chunk.Get<gp::Locomotion>();
		auto orders = chunk.Get<gp::MoveOrder>();
		auto routes = chunk.Get<gp::Route>();
		auto attacks = chunk.Get<gp::AttackTarget>();
		auto aggressions = chunk.Get<gp::Aggression>();
		auto looks = chunk.Get<gp::Appearance>();
		const auto builders = chunk.Get<gp::Builder>();
		const auto timers = chunk.Get<gp::SpecialPowerTimers>();
		const auto healths = chunk.Get<gp::Health>();
		const auto dying = chunk.Get<gp::Dying>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < abilities.size(); ++row)
		{
			SpecialAbilities &own = abilities[row];
			bool active = false;
			for (std::uint8_t index = 0; index < own.count; ++index)
				active = active || own.slots[index].Has(ability_flag::Active) || own.slots[index].trapTarget != ecs::Entity{};
			if (!active)
				continue;
			Unit unit{entities[row], transforms[row], motions.empty() ? nullptr : &motions[row], orders.empty() ? nullptr : &orders[row],
				routes.empty() ? nullptr : &routes[row], attacks.empty() ? nullptr : &attacks[row], aggressions.empty() ? nullptr : &aggressions[row],
				activities[row], looks.empty() ? nullptr : &looks[row], owners[row].player, members.empty() ? 0xFFFFFFFFu : members[row].team, refs[row].index,
				false, timers.empty() ? nullptr : &timers[row], !dying.empty() || (!healths.empty() && gp::IsDead(healths[row]))};
			const gp::Builder *builder = builders.empty() ? nullptr : &builders[row];
			for (std::uint8_t index = 0; index < own.count; ++index)
				UpdateSlot(env, unit, own.slots[index], builder);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::SpecialAbilitySystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.special_abilities";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
