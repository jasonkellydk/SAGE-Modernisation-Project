export module engine.gameplay.rts.combat.systems.targeting_system;
import std;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.common.areas.resources.trigger_areas;
export import engine.gameplay.rts.combat.resources.attack_priorities;
export import engine.gameplay.rts.combat.resources.mood_ranges;
export import engine.gameplay.common.random.resources.random_seed;
import Engine.Core.Math.FixedRandom;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.components.definition_ref;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.algorithms.attack_goal;
export import engine.gameplay.rts.combat.algorithms.target_pitch;
export import engine.gameplay.rts.combat.algorithms.weapon_fitness;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.construction.components.sale;
export import engine.gameplay.rts.combat.components.aggression;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.systems.movement_system;
export import engine.gameplay.rts.combat.components.attack_move;
export import engine.gameplay.rts.combat.components.contained_definitions;
export import engine.gameplay.rts.movement.components.pursuit;
export import engine.gameplay.rts.combat.components.sight_looker;
export import engine.gameplay.rts.combat.algorithms.attack_pursuit;
export import engine.gameplay.rts.collision.components.collider;
export import engine.gameplay.rts.collision.components.squishable;
export import engine.gameplay.common.physics.resources.physics_settings;
export import engine.gameplay.rts.slaves.components.slaved;
export import engine.gameplay.rts.blocking.components.blocked_state;
export import engine.gameplay.common.spatial.components.surface_layer;
export import engine.gameplay.rts.movement.components.attack_approach;
import engine.gameplay.rts.blocking.algorithms.blocking_rules;

// Picks and keeps targets for armed entities, chunk-parallel: drops targets
// that are gone or out of reach, scans for the closest enemy its stance
// allows on its scan tick, and moves toward targets out of weapon range.
// Targets are found in this tick's spatial index, never by reading other
// entities, so chunks never race.
export namespace engine::gameplay
{
// Weapon::isWithinAttackRange (ATTACK_RANGE_CALC_TYPE = FROM_BOUNDINGSPHERE_2D): the distance between the two
// bounding circles (never below zero) within `range` (the bonus attack range, BonusAttackRange).
inline bool WithinAttackRange(Engine::Math::Fixed range, Engine::Math::FixedVector2 from, Engine::Math::Fixed radius, const SpatialEntry &target) noexcept
{
	const Engine::Math::Fixed reach = range + radius + target.radius;
	return Engine::Math::DistanceSquared(from, target.position.XY()) <= reach * reach;
}

// Weapon::isWithinAttackRange's too-close test: the bounding circles' distance below the undersized minimum range.
inline bool TooCloseToAttack(Engine::Math::Fixed minimumRange, Engine::Math::FixedVector2 from, Engine::Math::Fixed radius, const SpatialEntry &target) noexcept
{
	const Engine::Math::Fixed minimum = UndersizedMinimumRange(minimumRange);
	if (minimum <= Engine::Math::Fixed{})
		return false;
	const Engine::Math::Fixed centre = Engine::Math::Length(target.position.XY() - from);
	const Engine::Math::Fixed apart = centre - radius - target.radius;
	return (apart > Engine::Math::Fixed{} ? apart : Engine::Math::Fixed{}) < minimum;
}

struct TargetingSystem
{
	using Query = ecs::Query<ecs::Write<Aggression>, ecs::Write<AttackTarget>, ecs::Read<Transform>, ecs::Read<Owner>, ecs::Read<Armament>,
		ecs::OptionalWrite<MoveOrder>, ecs::Optional<WeaponSlots>, ecs::Optional<OffMap>, ecs::Optional<Disabled>,
		ecs::Optional<Targetable>, ecs::Optional<WeaponBonusConditions>, ecs::Optional<AiActivity>, ecs::Optional<Health>, ecs::Optional<TeamMember>, ecs::Optional<AttackMove>, ecs::Optional<BodyExtent>, ecs::Optional<MoveEnded>, ecs::OptionalWrite<Pursuit>, ecs::Optional<SightLooker>, ecs::Optional<SurfaceLayer>, ecs::OptionalWrite<AttackApproach>, ecs::Exclude<UnderConstruction>,
		ecs::Exclude<Sale>>; // isAbleToAttack: not unbuilt or sold

	// What a weapon set can do, for choosing and keeping targets: whatever any of its weapons may target, as far
	// as the longest reaches with the firer's weapon bonuses (Weapon::getAttackRange; the weapon to fire is
	// chosen per shot). A locked set (WeaponSet::setWeaponLock) uses only its locked weapon: its reach is that one's.
	static WeaponDefinition Reach(const WeaponCatalog &weapons, const Armament &armament, const WeaponSlots *set, std::uint32_t conditions)
	{
		WeaponDefinition reach = weapons.At(armament.weapon);
		reach.attackRange = BonusAttackRange(reach.attackRange, weapons.Bonus(reach, conditions));
		if (set == nullptr)
			return reach;
		if (set->locked < WeaponSlotCount && set->slots[set->locked].weapon != WeaponCatalog::None)
		{
			reach = weapons.At(set->slots[set->locked].weapon);
			reach.attackRange = BonusAttackRange(reach.attackRange, weapons.Bonus(reach, conditions));
			return reach;
		}
		for (const WeaponSlot &slot : set->slots)
			if (slot.weapon != WeaponCatalog::None)
			{
				const WeaponDefinition &weapon = weapons.At(slot.weapon);
				reach.anti |= weapon.anti;
				reach.attackRange = std::max(reach.attackRange, BonusAttackRange(weapon.attackRange, weapons.Bonus(weapon, conditions)));
				reach.minimumRange = std::min(reach.minimumRange, weapon.minimumRange);
			}
		return reach;
	}
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Read<Relationships>, ecs::Read<WeaponCatalog>, ecs::Read<ArmorCatalog>, ecs::Read<TeamRoster>,
		ecs::Read<TriggerAreas>, ecs::Read<AttackPriorities>, ecs::Read<MoodRanges>, ecs::Read<RandomSeed>, ecs::Read<GroundHeight>, ecs::Read<NavigationGrid>,
		ecs::Read<PhysicsSettings>, ecs::Read<ChaseRules>>;
	using Lookup = ecs::Lookup<ecs::Read<DefinitionRef>, ecs::Read<BodyExtent>, ecs::Read<Health>, ecs::Read<Subdual>, ecs::Read<UnderConstruction>,
		ecs::Read<Locomotion>, ecs::Read<Transform>, ecs::Read<Collider>, ecs::Read<Squishable>, ecs::Read<Slaved>, ecs::Read<BlockedState>, ecs::Read<SurfaceLayer>,
		ecs::Read<ContainedDefinitions>>;

	// What an attacker's view check reads of a victim: its place, top and centre (BodyExtent), and its slaver (Slaved).
	static SightTarget SightTargetOf(const ecs::EntityLookup<Lookup> &lookup, const SpatialEntry &entry)
	{
		const BodyExtent *extent = lookup.IsAlive(entry.entity) ? lookup.template Get<BodyExtent>(entry.entity) : nullptr;
		return SightTarget{entry.position, extent != nullptr ? extent->maxHeight : Engine::Math::Fixed{}, extent != nullptr ? extent->centerZ : Engine::Math::Fixed{}};
	}
	// isAttackViewBlockedByObstacle's layer: the victim's, or (the victim on the ground) the looker's own (in `eye`).
	static SightEye EyeFor(const ecs::EntityLookup<Lookup> &lookup, SightEye eye, ecs::Entity victim)
	{
		const SurfaceLayer *layer = lookup.IsAlive(victim) ? lookup.template Get<SurfaceLayer>(victim) : nullptr;
		if (layer != nullptr && layer->layer != 0)
			eye.layer = layer->layer;
		return eye;
	}
	static ecs::Entity SlaverOf(const ecs::EntityLookup<Lookup> &lookup, ecs::Entity entity)
	{
		const Slaved *slaved = lookup.IsAlive(entity) ? lookup.template Get<Slaved>(entity) : nullptr;
		return slaved != nullptr ? slaved->master : ecs::Entity{};
	}

	// AI::CAN_SEE (PartitionFilterLineOfSight) for a looker that needs a line of sight (SightLooker): the terrain clear from
	// its top to the victim's, and no obstacle blocking its view (isViewBlockedByObstacle) but its own, the victim's, the
	// victim's slaver's, its container's and its slaver's (attackBlockedByObstacleCallback).
	struct SightFilter
	{
		const GroundHeight &ground;
		const NavigationGrid &grid;
		Engine::Math::Fixed significantHeight;
		SightEye eye;
		ecs::Entity self;
		ecs::Entity container;
		ecs::Entity slaver;
		const ecs::EntityLookup<Lookup> &lookup;

		bool Clear(const SpatialEntry &entry) const
		{
			const ViewIgnores ignore{self, entry.entity, SlaverOf(lookup, entry.entity), container, slaver};
			return LineOfSightClear(ground, grid, significantHeight, EyeFor(lookup, eye, entry.entity), SightTargetOf(lookup, entry), ignore);
		}
	};

	// WeaponSet::getAbleToUseWeaponAgainstTarget for an object, past what the victim's classes already settle: with a
	// damage weapon, a victim none of its weapons may pitch to (isAnyWithinTargetPitch), or none of the weighed ones is
	// reckoned to hurt (estimateWeaponDamage), is an invalid shot.
	struct AttackGate
	{
		const WeaponCatalog &weapons;
		const ArmorCatalog &armors;
		const Armament &armament;
		const WeaponSlots *set;
		PitchBody source;
		std::uint32_t sourceClasses{0};
		std::uint32_t conditions{0};
		const ecs::EntityLookup<Lookup> &lookup;
		bool damaging{false};
		bool pitched{false};
		bool details{false};

		static AttackGate For(const WeaponCatalog &weapons, const ArmorCatalog &armors, const Armament &armament, const WeaponSlots *set, PitchBody source,
			std::uint32_t sourceClasses, std::uint32_t conditions, const ecs::EntityLookup<Lookup> &lookup)
		{
			AttackGate gate{weapons, armors, armament, set, source, sourceClasses, conditions, lookup};
			gate.damaging = HasDamageWeapon(weapons, armament, set);
			gate.pitched = AnyPitchLimited(weapons, armament, set);
			gate.details = DetailsMatter(weapons, armors, armament, set, sourceClasses);
			return gate;
		}

		bool Allows(const SpatialEntry &entry) const
		{
			if (!damaging)
				return true;
			if (pitched)
			{
				const BodyExtent *extent = lookup.IsAlive(entry.entity) ? lookup.template Get<BodyExtent>(entry.entity) : nullptr;
				if (!AnyWithinTargetPitch(weapons, armament, set, source, PitchBodyOf(entry.position, extent)))
					return false;
			}
			if (!FitnessMatters(weapons, armors, armament, set, sourceClasses, entry.classes))
				return true;
			return AnyWeaponHurts(weapons, armors, armament, set, sourceClasses, conditions, FitnessOf(entry, lookup, details));
		}
	};

	// `unfogged`: only what is clear to the player through the shroud (AI::UNFOGGED, PartitionFilterFreeOfFog).
	// `team`: its team (Object::getRelationship: its team's view, overrides included; none: NoTeam).
	static bool Acceptable(const Relationships &relationships, const SpatialEntry &entry, ecs::Entity self, std::uint32_t team, std::uint32_t player,
		const WeaponDefinition &weapon, const Aggression &aggression, bool unfogged = false, const AttackGate *gate = nullptr)
	{
		if (entry.entity == self || (entry.classes & (target_class::Unattackable | target_class::Undetected | target_class::NoAttackFromAi)) != 0 ||
			HiddenFrom(entry.classes, entry.disguisePlayer, entry.disguiseTeam, player, relationships) || !relationships.Enemies(team, player, entry.team, entry.player))
			return false;
		if (unfogged && (player >= 64 || (entry.clearTo & (std::uint64_t{1} << player)) == 0))
			return false;
		// PartitionFilterRejectBuildings: never a building unless it is a base defence (KINDOF_FS_BASE_DEFENSE) or a
		// container able to attack (a garrison firing out).
		if ((entry.classes & target_class::Structure) != 0 && !aggression.attackBuildings && aggression.stance != Stance::Hunt &&
			(entry.classes & (target_class::BaseDefense | target_class::ArmedContainer)) == 0)
			return false;
		return CanTarget(weapon, entry.classes) && (gate == nullptr || gate->Allows(entry));
	}

	// Inside the area (PartitionFilterPolygonTrigger: the object's whole position; none: anywhere).
	static bool InArea(const TriggerArea *area, const SpatialEntry &entry) noexcept
	{
		if (area == nullptr)
			return true;
		const auto whole = [](Engine::Math::Fixed value) {
			const std::int64_t raw = value.Raw();
			return static_cast<std::int32_t>(raw >= 0 ? raw >> 16 : -((-raw) >> 16));
		};
		return area->Contains(whole(entry.position.x), whole(entry.position.y));
	}

	// AI::findClosestEnemy with an attack priority set: nearest first (the distance between bounding circles), each
	// enemy's priority (0: never attacked) less one per `distanceModifier` of that distance, at least 1; the greatest
	// kept, a tie going to the greater priority; a container is worth the greatest of its own and what it holds
	// (iterateContained: ContainedDefinitions).
	struct Prioritized
	{
		const AttackPriorities &priorities;
		std::uint16_t set{0};
		Engine::Math::Fixed selfRadius;
		const ecs::EntityLookup<Lookup> *lookup{nullptr};
	};

	// `withinAttack`: only those its weapons reach from where it stands (AI::WITHIN_ATTACK_RANGE; its bounding radius).
	struct Reachable
	{
		Engine::Math::Fixed range;
		Engine::Math::Fixed radius;
	};

	static const SpatialEntry *Closest(const SpatialIndex &spatial, const Relationships &relationships, Engine::Math::FixedVector2 from, Engine::Math::FixedVector2 around, Engine::Math::Fixed range, bool anywhere,
		ecs::Entity self, std::uint32_t team, std::uint32_t player, const WeaponDefinition &weapon, const Aggression &aggression, bool unfogged = false,
		const TriggerArea *area = nullptr, const Prioritized *prioritized = nullptr, const Reachable *withinAttack = nullptr,
		Engine::Math::Fixed selfRadius = {}, const AttackGate *gate = nullptr, bool ignoreInsignificant = false, const SightFilter *sight = nullptr)
	{
		// (The gate last: it may look the victim up.) `ignoreInsignificant`: AI::IGNORE_INSIGNIFICANT_BUILDINGS
		// (PartitionFilterInsignificantBuildings).
		const auto accepted = [&](const SpatialEntry &entry) {
			return Acceptable(relationships, entry, self, team, player, weapon, aggression, unfogged) && InArea(area, entry) &&
				(!ignoreInsignificant || (entry.classes & target_class::Insignificant) == 0) &&
				(withinAttack == nullptr || WithinAttackRange(withinAttack->range, from, withinAttack->radius, entry));
		};
		if (prioritized != nullptr && prioritized->set != 0)
		{
			std::vector<std::pair<Engine::Math::Fixed, const SpatialEntry *>> near;
			const auto gather = [&](const SpatialEntry &entry) {
				if (!accepted(entry) || (gate != nullptr && !gate->Allows(entry)) || (sight != nullptr && !sight->Clear(entry)))
					return;
				const Engine::Math::Fixed gap = Engine::Math::Distance(from, entry.position.XY()) - entry.radius - prioritized->selfRadius;
				near.emplace_back(std::max(gap, Engine::Math::Fixed{}), &entry);
			};
			if (anywhere)
				for (const SpatialEntry &entry : spatial.Entries())
					gather(entry);
			else
				spatial.ForEachWithin(around, range, gather);
			std::ranges::stable_sort(near, [](const auto &a, const auto &b) {
				return a.first != b.first ? a.first < b.first : a.second->entity.index < b.second->entity.index;
			});
			const SpatialEntry *best = nullptr;
			std::int64_t effective = 0, actual = 0;
			for (const auto &[gap, entry] : near)
			{
				const DefinitionRef *ref = prioritized->lookup->template Get<DefinitionRef>(entry->entity);
				std::int64_t priority = prioritized->priorities.Priority(prioritized->set, ref != nullptr ? ref->index : 0xFFFFFFFFu);
				if (priority == 0)
					continue;
				// A garrison or transport is worth what the most wanted thing inside it is (iterateContained: priorityFunc).
				if (const ContainedDefinitions *held = prioritized->lookup->template Get<ContainedDefinitions>(entry->entity))
					for (std::size_t index = 0; index < held->count; ++index)
						priority = (std::max)(priority, static_cast<std::int64_t>(prioritized->priorities.Priority(prioritized->set, held->definitions[index])));
				const std::int64_t less = prioritized->priorities.distanceModifier > Engine::Math::Fixed{}
					? (gap / prioritized->priorities.distanceModifier).Floor() : 0;
				const std::int64_t modified = std::max<std::int64_t>(priority - less, 1);
				if (modified > effective || (modified == effective && priority > actual))
				{
					effective = modified;
					actual = priority;
					best = entry;
				}
			}
			return best;
		}
		// PartitionManager::getClosestObject, FROM_BOUNDINGSPHERE_2D: the gap between the two bounding circles (none when
		// they overlap), the first of equals kept.
		const SpatialEntry *best = nullptr;
		Engine::Math::Fixed bestGap;
		const auto consider = [&](const SpatialEntry &entry) {
			if (!accepted(entry))
				return;
			const Engine::Math::Fixed gap = std::max(Engine::Math::Distance(from, entry.position.XY()) - entry.radius - selfRadius, Engine::Math::Fixed{});
			// (CAN_SEE last: it walks the terrain and the grid, only for one that would be nearest.)
			if ((best == nullptr || gap < bestGap) && (gate == nullptr || gate->Allows(entry)) && (sight == nullptr || sight->Clear(entry)))
			{
				best = &entry;
				bestGap = gap;
			}
		};
		if (anywhere)
			for (const SpatialEntry &entry : spatial.Entries())
				consider(entry);
		else
			spatial.ForEachWithin(around, range, consider);
		return best;
	}

	static const TriggerArea *AreaOf(ecs::SystemContext &context, const Aggression &aggression)
	{
		if (aggression.area == TriggerAreas::None)
			return nullptr;
		const TriggerAreas &areas = context.Read<TriggerAreas>();
		return aggression.area < areas.areas.size() ? &areas.areas[aggression.area] : nullptr;
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const Relationships &relationships = context.Read<Relationships>();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const ArmorCatalog &armors = context.Read<ArmorCatalog>();
		const TeamRoster &roster = context.Read<TeamRoster>();
		auto aggressions = chunk.Get<Aggression>();
		auto targets = chunk.Get<AttackTarget>();
		const auto transforms = chunk.Get<Transform>();
		const auto owners = chunk.Get<Owner>();
		const auto armaments = chunk.Get<Armament>();
		auto moves = chunk.Get<MoveOrder>();
		const auto sets = chunk.Get<WeaponSlots>();
		const auto offMap = chunk.Get<OffMap>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		const auto disabledRows = chunk.Get<Disabled>();
		const auto targetables = chunk.Get<Targetable>();
		const auto bonuses = chunk.Get<WeaponBonusConditions>();
		const auto activityRows = chunk.Get<AiActivity>();
		const auto attackMovesAll = chunk.Get<AttackMove>();
		const auto healthRows = chunk.Get<Health>();
		const auto teamRows = chunk.Get<TeamMember>();
		const auto extents = chunk.Get<BodyExtent>();
		const auto endedRows = chunk.Get<MoveEnded>();
		const auto lookup = context.Lookup<Lookup>();
		auto pursuits = chunk.Get<Pursuit>();
		const auto lookers = chunk.Get<SightLooker>();
		const auto layerRows = chunk.Get<SurfaceLayer>();
		auto approachRows = chunk.Get<AttackApproach>();
		const GroundHeight *groundHeight = context.Find<GroundHeight>();
		const NavigationGrid *navigationGrid = context.Find<NavigationGrid>();
		const PhysicsSettings *physicsSettings = context.Find<PhysicsSettings>();
		const Engine::Math::Fixed significantHeight = (physicsSettings != nullptr ? *physicsSettings : PhysicsSettings{}).SignificantHeight();
		const ChaseRules *chaseFound = context.Find<ChaseRules>();
		const ChaseRules chaseRules = chaseFound != nullptr ? *chaseFound : ChaseRules{};
		for (std::size_t row = 0; row < aggressions.size(); ++row)
		{
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::Held))
				continue;
			if (armaments[row].weapon == WeaponCatalog::None)
				continue;
			// Carried: only a passenger its carrier lets fire looks for victims, and it never walks off after them.
			const bool carried = !offMap.empty();
			if (carried && !offMap[row].armed)
				continue;
			const WeaponDefinition weapon = Reach(weapons, armaments[row], sets.empty() ? nullptr : &sets[row], bonuses.empty() ? 0u : bonuses[row].Effective());
			const Engine::Math::Fixed radius = targetables.empty() ? Engine::Math::Fixed{} : targetables[row].radius;
			Aggression &aggression = aggressions[row];
			AttackTarget &target = targets[row];
			const std::uint32_t selfTeam = teamRows.empty() ? Relationships::NoTeam : teamRows[row].team;
			const Engine::Math::FixedVector2 self = transforms[row].position.XY();
			MoveOrder *move = moves.empty() || carried ? nullptr : &moves[row];
			const AttackGate attackGate = AttackGate::For(weapons, armors, armaments[row], sets.empty() ? nullptr : &sets[row],
				PitchBodyOf(transforms[row].position, extents.empty() ? nullptr : &extents[row]), targetables.empty() ? 0u : targetables[row].classes,
				bonuses.empty() ? 0u : bonuses[row].Effective(), lookup);
			const AttackGate *gate = &attackGate;

			// Keep the target while it is there and within reach of the stance (a spot on the ground stays).
			SpatialEntry point;
			const SpatialEntry *current = AttackGoal(spatial, target, point);
			// A target masked or unattackable is no target (getAbleToAttackSpecificObject: OBJECT_STATUS_MASKED,
			// KINDOF_UNATTACKABLE); one that went into hiding is lost, unless its weapon has a continue range and has not fired in this attack
			// yet (AIAttackState's IGNORING_STEALTH).
			if (current != nullptr && target.atPosition == 0 && (current->classes & target_class::Unattackable) != 0)
				current = nullptr;
			if (current != nullptr && target.atPosition == 0 && HiddenFrom(current->classes, current->disguisePlayer, current->disguiseTeam, owners[row].player, relationships) &&
				!(target.fired == 0 && weapons.At(armaments[row].weapon).continueAttackRange > Engine::Math::Fixed{}))
				current = nullptr;
			if (current != nullptr && !target.ordered)
			{
				const Engine::Math::FixedVector2 anchor = aggression.stance == Stance::Guard ? aggression.guardCenter : self;
				const Engine::Math::Fixed leash = aggression.stance == Stance::Hunt ? Engine::Math::Fixed{}
					: aggression.stance == Stance::Guard ? aggression.guardRadius + weapon.attackRange
					: aggression.stance == Stance::Hold ? weapon.attackRange + current->radius
					: std::max(aggression.scanRange, weapon.attackRange) * Engine::Math::Fixed::FromRatio(3, 2);
				if (aggression.stance != Stance::Hunt && Engine::Math::DistanceSquared(anchor, current->position.XY()) > leash * leash)
					current = nullptr;
			}
			// cannotPossiblyAttackObject: its attack ends once none of its weapons may pitch to the victim or hurt it.
			if (current != nullptr && target.atPosition == 0 && !gate->Allows(*current))
				current = nullptr;
			if (current == nullptr)
				target = {};

			// getNextMoodTarget from the idle state: stealthed (detected or not), a unit without AutoAcquireEnemiesWhenIdle
			// Stealthed picks nothing, unless it rides in something that lets it fire.
			const bool stealthVeto = !targetables.empty() && (targetables[row].classes & target_class::Stealthed) != 0 && !aggression.acquireStealthed && !carried;
			// getNextMoodTarget's team victim: an idle unit of normal mood or more whose team attacks together goes for the
			// team's target (only a computer player's team has one) at once, before its own look and its timer, if it may.
			if (current == nullptr && aggression.stance == Stance::Idle && aggression.autoAcquire && !stealthVeto && aggression.attitude >= attitude::Normal &&
				!teamRows.empty() && teamRows[row].team < roster.TeamCount() && (move == nullptr || move->mode == MoveMode::Idle) &&
				(activityRows.empty() || !activityRows[row].Occupied()))
			{
				const Team &team = roster.TeamAt(teamRows[row].team);
				if (team.attackCommonTarget && team.commonTarget != ecs::Entity{})
					if (const SpatialEntry *victim = spatial.Find(team.commonTarget);
						victim != nullptr && Acceptable(relationships, *victim, entities[row], selfTeam, owners[row].player, weapon, aggression, false, gate))
					{
						current = victim;
						target = {victim->entity, false};
					}
			}

			// AIIdleState::onEnter (resetNextMoodCheckTime): a unit that has just gone idle (nothing to attack, going nowhere,
			// not busy) first looks ForceIdleMSEC later, its look after that randomly offset.
			const bool idleNow = current == nullptr && aggression.stance == Stance::Idle && (move == nullptr || move->mode == MoveMode::Idle) &&
				(activityRows.empty() || !activityRows[row].Occupied()) && attackMovesAll.empty();
			if (!idleNow)
				aggression.moodFlags &= static_cast<std::uint8_t>(~mood_flag::SeenIdle);
			else if ((aggression.moodFlags & mood_flag::SeenIdle) == 0)
			{
				aggression.moodFlags |= mood_flag::SeenIdle | mood_flag::OffsetNext;
				// Gone idle as its move ended: on the tick it ended.
				const std::uint64_t entered = !endedRows.empty() && endedRows[row].tick + 1 >= tick ? endedRows[row].tick : tick;
				aggression.nextScan = entered + context.Read<MoodRanges>().forceIdleTicks;
			}

			// AIUpdateInterface::getNextMoodTarget's look (called by its AI), past its gates and its timer:
			// (AI::getAdjustedVisionRangeForObject, owner type and mood) its vision times its controller's guard outer
			// modifier; carried, its longest weapon's reach instead, and its carrier's bounding radius on top; a computer's
			// unit by its mood (asleep: none; a passive one strikes back at its last attacker only, unless that last hit was
			// a heal); a human's only at what its weapons reach from where it stands (WITHIN_ATTACK_RANGE) and what is clear
			// to its player; a computer player's units take enemy buildings too (PartitionFilterRejectBuildings:
			// m_acquireEnemies); by its attack priority set (getAttackInfo), if it has one.
			// A looker's eye (its top; its weapon's terrain check unless IMMOBILE; 3 cells seen past first off the ground) and
			// what its view passes over for a victim.
			const auto lookerEye = [&]() {
				const std::uint8_t own = layerRows.empty() ? GroundLayer : layerRows[row].layer;
				return SightEye{transforms[row].position, extents.empty() ? Engine::Math::Fixed{} : extents[row].maxHeight,
					lookers.empty() || lookers[row].immobile == 0, own != GroundLayer ? RaisedLookerSkipCells : 0, own};
			};
			const auto viewIgnores = [&](ecs::Entity victim) {
				return ViewIgnores{entities[row], victim, SlaverOf(lookup, victim), carried ? offMap[row].holder : ecs::Entity{}, SlaverOf(lookup, entities[row])};
			};
			const auto moodLook = [&]() -> const SpatialEntry * {
				const std::uint32_t player = owners[row].player;
				const bool human = player < roster.PlayerCount() && roster.PlayerAt(player).human;
				Aggression looking = aggression;
				if (player < roster.PlayerCount() && !roster.PlayerAt(player).human)
					looking.attackBuildings = true;
				std::optional<Prioritized> prioritized;
				if (aggression.prioritySet != 0)
					prioritized.emplace(Prioritized{context.Read<AttackPriorities>(), aggression.prioritySet, radius, &lookup});
				const MoodRanges &moods = context.Read<MoodRanges>();
				Engine::Math::Fixed range;
				if (carried)
				{
					range = weapon.attackRange;
					if (const SpatialEntry *holder = spatial.Find(offMap[row].holder))
						range += holder->radius;
				}
				else
				{
					range = aggression.vision * (human ? moods.guardOuterHuman : moods.guardOuterAi);
					if (!human)
						range = aggression.attitude == attitude::Sleep ? Engine::Math::Fixed{}
							: aggression.attitude == attitude::Alert ? range * moods.alert
							: aggression.attitude == attitude::Aggressive ? range * moods.aggressive
							: range;
				}
				if (range <= Engine::Math::Fixed{})
					return nullptr;
				if (!human && aggression.attitude == attitude::Passive)
				{
					const Health *health = healthRows.empty() ? nullptr : &healthRows[row];
					// (aiAttackObject then: its attack state takes only what it may attack.)
					const SpatialEntry *attacker =
						health != nullptr && health->lastDamageType != moods.healingDamageType ? spatial.Find(health->lastAttacker) : nullptr;
					if (attacker != nullptr && Acceptable(relationships, *attacker, entities[row], selfTeam, player, weapon, looking, human, gate))
						return attacker;
					return nullptr;
				}
				const Reachable reach{weapon.attackRange, radius};
				// AttackUsesLineOfSight: a KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT looker asks for CAN_SEE too.
				std::optional<SightFilter> sight;
				if (!lookers.empty() && groundHeight != nullptr && navigationGrid != nullptr)
					sight.emplace(SightFilter{*groundHeight, *navigationGrid, significantHeight, lookerEye(), entities[row],
						carried ? offMap[row].holder : ecs::Entity{}, SlaverOf(lookup, entities[row]), lookup});
				// Within `range` of the gap between the bounding circles; AttackIgnoreInsignificantBuildings: it asks for
				// IGNORE_INSIGNIFICANT_BUILDINGS.
				return Closest(spatial, relationships, self, self, range + radius, false, entities[row], selfTeam, player, weapon, looking, human, nullptr,
					prioritized ? &*prioritized : nullptr, human ? &reach : nullptr, radius, gate, moods.ignoreInsignificantBuildings, sight ? &*sight : nullptr);
			};
			// getNextMoodTarget's timer (called by its AI): the next look MoodAttackCheckRate on, the first after an idle
			// entry or a wake-up moved by GameLogicRandomValue(-half, half) (`offset`).
			const auto nextMoodCheck = [&](bool offset) {
				aggression.nextScan = tick + aggression.scanInterval;
				if ((aggression.moodFlags & mood_flag::OffsetNext) != 0 && offset)
				{
					const std::int64_t half = static_cast<std::int64_t>(aggression.scanInterval >> 1);
					auto random = Engine::Math::Stream(context.Read<RandomSeed>().value, {tick, entities[row].index, entities[row].generation, 0x4D6Fu});
					aggression.nextScan = static_cast<std::uint64_t>(static_cast<std::int64_t>(aggression.nextScan) + Engine::Math::UniformInt(random, -half, half));
					aggression.moodFlags &= static_cast<std::uint8_t>(~mood_flag::OffsetNext);
				}
			};

			// Look for a new one on the scan tick (an undetected defector sees everyone as neutral: it looks for no one).
			const bool defecting = !targetables.empty() && (targetables[row].classes & target_class::Undetected) != 0;
			if (current == nullptr && tick >= aggression.nextScan && !defecting)
			{
				// getNextMoodTarget: the next look MoodAttackCheckRate on, the first after an idle entry or a wake-up
				// moved by GameLogicRandomValue(-half, half).
				nextMoodCheck(aggression.stance == Stance::Idle);
				const bool idle = move == nullptr || move->mode == MoveMode::Idle;
				// Mood targeting (AIUpdateInterface::getNextMoodTarget called by the AI) of a human player's unit takes only
				// what is clear to its player.
				const std::uint32_t player = owners[row].player;
				const bool unfogged = player < roster.PlayerCount() && roster.PlayerAt(player).human;
				// PartitionFilterRejectBuildings: a computer player's units take enemy buildings too (m_acquireEnemies).
				Aggression looking = aggression;
				if (player < roster.PlayerCount() && !roster.PlayerAt(player).human)
					looking.attackBuildings = true;
				// Its attack priority set (AIUpdateInterface::getAttackInfo), if it has one.
				std::optional<Prioritized> prioritized;
				if (aggression.prioritySet != 0)
					prioritized.emplace(Prioritized{context.Read<AttackPriorities>(), aggression.prioritySet, radius, &lookup});
				const Prioritized *ranked = prioritized ? &*prioritized : nullptr;
				// Busy on its own or using an ability, its AI picks nothing (getNextMoodTarget; no idle state runs).
				const bool occupied = !activityRows.empty() && activityRows[row].Occupied();
				switch (occupied ? Stance::Idle : aggression.stance)
				{
				case Stance::Idle:
				{
					// Attack-moving (AIAttackMoveToState: getNextMoodTarget as it moves), it looks whether or not it would idle.
					const bool attackMoving = !attackMovesAll.empty();
					if (occupied || (!attackMoving && (!aggression.autoAcquire || !idle || stealthVeto)))
						break;
					current = moodLook();
					break;
				}
				case Stance::Hold:
					current = Closest(spatial, relationships, self, self, weapon.attackRange + radius, false, entities[row], selfTeam, player, weapon, looking, unfogged, nullptr,
						ranked, nullptr, radius, gate);
					break;
				case Stance::Guard:
					current = Closest(spatial, relationships, self, aggression.guardCenter, aggression.guardRadius, false, entities[row], selfTeam, owners[row].player, weapon, looking,
						false, AreaOf(context, aggression), ranked, nullptr, {}, gate);
					break;
				case Stance::Hunt:
					current = Closest(spatial, relationships, self, self, {}, true, entities[row], selfTeam, owners[row].player, weapon, aggression, false, AreaOf(context, aggression), ranked,
						nullptr, {}, gate);
					// AIHuntState: its player all hunting (getUnitsShouldHunt), none found by its priorities, it looks again without.
					if (current == nullptr && ranked != nullptr && player < roster.PlayerCount() && roster.PlayerAt(player).unitsShouldHunt)
						current = Closest(spatial, relationships, self, self, {}, true, entities[row], selfTeam, owners[row].player, weapon, aggression, false, AreaOf(context, aggression), nullptr,
							nullptr, {}, gate);
					// AIAttackAreaState: no enemy left in the area, it is done (and idles).
					if (current == nullptr && aggression.area != TriggerAreas::None)
					{
						aggression.stance = Stance::Idle;
						aggression.area = TriggerAreas::None;
					}
					break;
				}
				if (current != nullptr)
					target = {current->entity, false};
			}

			// AIAttackApproachTargetState::update and AIAttackPursueTargetState::update, each on its first approach
			// (m_isInitialApproach) with its current weapon on a turret (getWhichTurretForCurWeapon): getNextMoodTarget(true,
			// false) gives that turret a temporary target. None while it uses an ability, nor with NotWhileAttacking (isAttacking:
			// it is); its team's common target at once (attitude normal or more, if it may attack it); else what its look finds,
			// on its mood timer.
			const auto turretTemporary = [&] {
				if (!armaments[row].turret || aggression.notWhileAttacking || (!activityRows.empty() && activityRows[row].usingAbility != 0))
					return;
				const SpatialEntry *temporary = nullptr;
				if (aggression.attitude >= attitude::Normal && !teamRows.empty() && teamRows[row].team < roster.TeamCount())
				{
					const Team &team = roster.TeamAt(teamRows[row].team);
					if (team.attackCommonTarget && team.commonTarget != ecs::Entity{})
						if (const SpatialEntry *victim = spatial.Find(team.commonTarget);
							victim != nullptr && Acceptable(relationships, *victim, entities[row], selfTeam, owners[row].player, weapon, aggression, false, gate))
							temporary = victim;
				}
				if (temporary == nullptr && tick >= aggression.nextScan)
				{
					nextMoodCheck(true);
					temporary = moodLook();
				}
				if (temporary != nullptr)
				{
					target.temporary = temporary->entity;
					target.temporaryTick = tick;
				}
			};
			// Close in on targets beyond weapon range; stop once in range.
			// (An attack-spot search asked for only while it approaches: set again below each tick it does.)
			const bool wasApproaching = !approachRows.empty() && approachRows[row].active != 0;
			if (!approachRows.empty())
				approachRows[row].active = 0;
			Pursuit *pursuit = pursuits.empty() ? nullptr : &pursuits[row];
			const auto stopPursuit = [&] {
				if (pursuit != nullptr)
				{
					// onExit: its chase's first approach is over.
					if (pursuit->active != 0)
						target.chased = 1;
					pursuit->active = 0;
					pursuit->matched = 0;
				}
			};
			if (move == nullptr || aggression.stance == Stance::Hold)
			{
				stopPursuit();
				continue;
			}
			if (current == nullptr)
				stopPursuit();
			if (current != nullptr)
			{
				// outOfWeaponRangeObject: a weapon with leech range active is never out of range (it fires on where it stands).
				const std::uint8_t slot = sets.empty() ? std::uint8_t{0} : sets[row].current;
				const bool leeching = (target.leech >> slot & 1u) != 0;
				// outOfWeaponRangeObject: on the ground with a weapon that is no contact weapon (isContactWeapon: its range less a
				// quarter cell under a cell), a looker that needs a line of sight whose view of a victim on the ground is blocked
				// (isAttackViewBlockedByObstacle) is out of range too.
				bool viewBlockedInRange = false;
				if (!leeching && !lookers.empty() && groundHeight != nullptr && navigationGrid != nullptr && target.atPosition == 0 &&
					weapons.At(armaments[row].weapon).attackRange - Engine::Math::Fixed::FromRatio(5, 2) >= Engine::Math::Fixed::FromInt(10))
				{
					const Locomotion *own = lookup.IsAlive(entities[row]) ? lookup.template Get<Locomotion>(entities[row]) : nullptr;
					if (carried || own == nullptr || !IsAirborne(own->locomotor))
					{
						viewBlockedInRange = ViewBlocked(*groundHeight, *navigationGrid, significantHeight, EyeFor(lookup, lookerEye(), current->entity), SightTargetOf(lookup, *current),
							viewIgnores(current->entity));
					}
				}
				// (Weapon::isWithinAttackRange: inside its minimum range it is not in range either.)
				const bool inRange = leeching || (!viewBlockedInRange && WithinAttackRange(weapon.attackRange, self, radius, *current) &&
					!TooCloseToAttack(weapon.minimumRange, self, radius, *current));
				// AttackStateMachine's CHASE_TARGET for an object (AIAttackPursueTargetState): it chases a victim that runs from it
				// (canPursue) when out of range (outOfWeaponRangeObject) or, a computer player's crusher, to run it over
				// (wantToSquishTarget); not a human player's unit that picked its victim itself (CMD_FROM_AI). It drives at the
				// victim's position, working its way out again when the victim has moved more than a tenth of their distance and
				// MIN_RECOMPUTE_TIME has passed (or it has no way), until canPursue fails; within range with a clear view it
				// matches the victim's speed (95% of it once there), a crusher as fast as it can.
				const bool pursuing = [&]() -> bool {
					if (pursuit == nullptr || target.atPosition != 0)
						return false;
					// (Cheap refusals first: most attackers in range never chase.)
					if (pursuit->active == 0)
					{
						const std::uint32_t owner = owners[row].player;
						const bool humanOwner = owner < roster.PlayerCount() && roster.PlayerAt(owner).human;
						if (!armaments[row].turret || (humanOwner && target.source == CommandSource::Ai && target.retaliating == 0))
							return false;
						if (inRange && (humanOwner || !chaseRules.aiCrushesInfantry || pursuit->autoCrush == 0))
							return false;
					}
					const bool alive = lookup.IsAlive(entities[row]) && lookup.IsAlive(current->entity);
					const Locomotion *own = alive ? lookup.template Get<Locomotion>(entities[row]) : nullptr;
					if (own == nullptr || own->locomotor.maxSpeed <= Engine::Math::Fixed{}) // isMobile
						return false;
					const Locomotion *theirs = lookup.template Get<Locomotion>(current->entity);
					const Transform *theirPlace = lookup.template Get<Transform>(current->entity);
					const std::uint32_t player = owners[row].player;
					const bool human = player < roster.PlayerCount() && roster.PlayerAt(player).human;
					const Collider *crusher = lookup.template Get<Collider>(entities[row]);
					const Collider *crushed = lookup.template Get<Collider>(current->entity);
					const bool unmanned = !disabledRows.empty() && (disabledRows[row].mask & disabled_type::Unmanned) != 0;
					const bool allies = relationships.Between(selfTeam, player, current->team, current->player) == Relationship::Allies;
					const bool canCrush = crusher != nullptr &&
						CanCrushOrSquish(crusher->crusherLevel, unmanned, allies, lookup.template Get<Squishable>(current->entity) != nullptr,
							crushed != nullptr ? crushed->crushableLevel : 255u);
					ChaseView view;
					view.turret = armaments[row].turret;
					view.computer = !human;
					view.aiCrushes = chaseRules.aiCrushesInfantry;
					view.canCrush = canCrush;
					view.tooClose = TooCloseToAttack(weapon.minimumRange, self, radius, *current);
					view.ourMaxSpeed = own->locomotor.maxSpeed;
					view.victimPhysics = theirs != nullptr && theirPlace != nullptr;
					view.victimSpeed = theirs != nullptr ? theirs->speed : Engine::Math::Fixed{};
					view.toVictim = current->position.XY() - self;
					view.victimHeading = theirPlace != nullptr ? Engine::Math::Direction(theirPlace->facing) : Engine::Math::FixedVector2{};
					if (pursuit->active == 0)
					{
						const bool squish = WantToSquish(false, view.turret, view.aiCrushes, view.computer, canCrush, pursuit->autoCrush != 0);
						if (inRange && !squish)
							return false;
						// onEnter: a human player's auto-acquired attack does not chase; nor without a turret weapon or a victim
						// it can pursue.
						if ((human && target.source == CommandSource::Ai && target.retaliating == 0) || !CanPursue(view))
							return false;
						pursuit->active = 1;
						pursuit->prevVictim = {};
						pursuit->approachTick = 0; // m_approachTimestamp = -MIN_RECOMPUTE_TIME: work its way out at once
					}
					// updateInternal: a victim gone into hiding (stealthed, undetected, not disguised) ends it.
					if ((current->classes & target_class::Hidden) != 0)
						return false;
					// computePath: blocked and stuck (isBlockedAndStuck, as its last collisions left it before it planned again), it
					// gives the chase up.
					if (const BlockedState *blocked = lookup.template Get<BlockedState>(entities[row]); blocked != nullptr && blocked->stuckSeen != 0)
						return false;
					const bool force = move->mode == MoveMode::Idle;
					if (force || tick >= pursuit->approachTick)
					{
						pursuit->approachTick = tick + PursuitRecomputeTicks;
						if (force || !SamePosition(self, pursuit->prevVictim, current->position.XY()))
						{
							if (!CanPursue(view))
								return false;
							pursuit->prevVictim = current->position.XY();
							*move = Replanned(MoveToPoint(pursuit->prevVictim)); // setAdjustsDestination(true)
						}
					}
					// The speed it asks for.
					bool viewBlocked = false;
					if (!IsAirborne(own->locomotor) && !lookers.empty() && groundHeight != nullptr && navigationGrid != nullptr &&
						current->position.z - groundHeight->At(current->position.XY()) <= significantHeight)
					{
						viewBlocked = AttackViewBlocked(*groundHeight, *navigationGrid, EyeFor(lookup, lookerEye(), current->entity), SightTargetOf(lookup, *current),
							viewIgnores(current->entity));
					}
					pursuit->matched = 0;
					if (!viewBlocked && view.victimPhysics && WithinAttackRange(weapon.attackRange, self, radius, *current) && !view.tooClose)
					{
						Engine::Math::Fixed speed = view.victimSpeed;
						// isGoalPosWithinAttackRange: the range less a quarter of a pathfind cell.
						const Engine::Math::Fixed goalRange = weapon.attackRange - Engine::Math::Fixed::FromRatio(5, 2);
						if (WithinAttackRange(goalRange, self, radius, *current))
							speed = speed * Engine::Math::Fixed::FromRatio(95, 100);
						if (!canCrush)
						{
							pursuit->matched = 1;
							pursuit->speed = speed;
						}
						// m_isInitialApproach = false; setTurretTargetObject(victim): its turret back on its victim.
						target.chased = 1;
						target.temporary = {};
					}
					// update: on its chase's first approach, a temporary target for its turret.
					if (target.chased == 0)
						turretTemporary();
					return true;
				}();
				if (!pursuing)
					stopPursuit();
				if (pursuing)
				{
					// The chase has its move (CHASE_TARGET in place of APPROACH_TARGET).
				}
				else if (!inRange)
				{
					// AIAttackApproachTargetState::computePath -> requestAttackPath -> AIUpdateInterface::computeAttackPath:
					// - flying (an airborne locomotor): straight for Weapon::computeApproachTarget's spot (0.9 of its range from the
					//   victim, on the line to it; too close inside a minimum range over a cell, halfway between the two ranges plus
					//   both bounding circles, away, not turning about);
					// - a contact weapon: to the victim's position itself;
					// - on the ground: to the victim's position, the route search stopping at the first spot it may fire from
					//   (Pathfinder::findAttackPath: AttackApproach, planned by the route requests).
					// It works its way out again only with no way (or stuck: isBlockedAndStuck), else MIN_RECOMPUTE_TIME (10 frames)
					// on and with the victim moved more than a tenth of their distance (isSamePosition).
					AttackApproach *searched = approachRows.empty() ? nullptr : &approachRows[row];
					const Locomotion *ownMotion = lookup.IsAlive(entities[row]) ? lookup.template Get<Locomotion>(entities[row]) : nullptr;
					const bool flying = ownMotion != nullptr && IsAirborne(ownMotion->locomotor);
					const WeaponDefinition &held = weapons.At(armaments[row].weapon);
					const bool contact = held.attackRange - Engine::Math::Fixed::FromRatio(5, 2) < Engine::Math::Fixed::FromInt(10); // isContactWeapon
					const Engine::Math::FixedVector2 victimAt = current->position.XY();
					Engine::Math::FixedVector2 approach = victimAt;
					if (flying && !contact)
					{
						const Engine::Math::FixedVector2 away = self - victimAt;
						const Engine::Math::Fixed apart = Engine::Math::Length(away);
						const Engine::Math::Fixed cell = Engine::Math::Fixed::FromInt(10);
						// Weapon::computeApproachTarget uses getMinimumAttackRange (already undersized), and
						// getVectorTo(FROM_BOUNDINGSPHERE_2D): the gap between both bounding circles.
						const Engine::Math::Fixed minimumRange = UndersizedMinimumRange(held.minimumRange);
						const Engine::Math::Fixed gap = std::max(Engine::Math::Fixed{}, apart - current->radius - radius);
						if (minimumRange > cell && gap < minimumRange)
						{
							Engine::Math::FixedVector2 direction = apart > Engine::Math::Fixed{} ? away / apart : Engine::Math::FixedVector2{Engine::Math::Fixed::One(), Engine::Math::Fixed{}};
							// Airborne and too close: not a turn about (its way on the far side when it faces the victim's).
							const std::int32_t turn = static_cast<std::int32_t>((transforms[row].facing - Engine::Math::Heading(Engine::Math::FixedVector2{} - direction)).units);
							if (turn > -0x40000000 && turn < 0x40000000)
								direction = Engine::Math::FixedVector2{} - direction;
							const Engine::Math::Fixed spacing = (weapon.attackRange + minimumRange) / Engine::Math::Fixed::FromInt(2) + current->radius + radius;
							approach = victimAt + direction * spacing;
						}
						else if (apart < Engine::Math::Fixed::FromRatio(1, 1000))
							approach = self;
						else
							approach = victimAt + (away / apart) * (weapon.attackRange * Engine::Math::Fixed::FromRatio(9, 10));
					}
					const bool searching = searched != nullptr && !flying && !contact;
					if (searched != nullptr)
					{
						const BlockedState *blockedNow = lookup.template Get<BlockedState>(entities[row]);
						const bool force = move->mode != MoveMode::Point || (blockedNow != nullptr && blockedNow->stuckSeen != 0) || !wasApproaching;
						bool request = force;
						if (!force && tick >= searched->approachTick)
						{
							searched->approachTick = tick + PursuitRecomputeTicks;
							request = !SamePosition(self, searched->prevVictim, victimAt);
						}
						if (force)
							searched->approachTick = tick + PursuitRecomputeTicks;
						if (request)
						{
							searched->prevVictim = victimAt;
							*move = Replanned(MoveToPoint(approach, GoalClaim::Keep)); // AIAttackApproachTargetState: its path's end claimed
							// The search's view of things as they are now (m_requestedDestination: the victim where it is now).
							const SightEye eye = EyeFor(lookup, lookerEye(), current->entity);
							const SightTarget seen = SightTargetOf(lookup, *current);
							const ViewIgnores ignores = viewIgnores(current->entity);
							const AttackApproach kept = *searched;
							*searched = AttackApproach{seen.at, seen.top, seen.centre, current->radius, radius, weapon.attackRange - Engine::Math::Fixed::FromRatio(5, 2),
								held.minimumRange, eye.top, kept.prevVictim, kept.approachTick, target.atPosition != 0 ? ecs::Entity{} : current->entity,
								ignores.victimSlaver, ignores.container, ignores.slaver, eye.skipCount, 1, eye.weaponTerrain ? std::uint8_t{1} : std::uint8_t{0},
								lookers.empty() ? std::uint8_t{0} : std::uint8_t{1}, 0, eye.layer};
							searched->victimAt = {approach.x, approach.y, seen.at.z};
						}
						// Only a ground route searches for its spot (flying and contact weapons go where they were sent).
						searched->active = 1;
						searched->search = searching ? 1 : 0;
						// A contact weapon at an object: ignoreObstacle(victim) while it approaches.
						searched->ignoreVictim = contact && target.atPosition == 0 ? 1 : 0;
					}
					else if (move->mode != MoveMode::Point || move->destination != approach)
						*move = Replanned(MoveToPoint(approach, GoalClaim::Keep));
					// Its first approach: a temporary target for its turret.
					if (target.approached == 0)
						turretTemporary();
				}
				else
				{
					// In reach: its first approach is over (onExit: m_isInitialApproach false), and its turret is back on its
					// victim (AIAttackAimAtTargetState::onEnter: setTurretTargetObject).
					target.approached = 1;
					target.temporary = {};
					// AIAttackAimAtTargetState::update: one that cannot turn in place (m_canTurnInPlace: its locomotor's MinSpeed
					// 0) and aims with its body flies straight at its victim (setLocomotorGoalPositionExplicit) to face it.
					const Locomotion *own = lookup.IsAlive(entities[row]) ? lookup.template Get<Locomotion>(entities[row]) : nullptr;
					if (!armaments[row].turret && own != nullptr && own->locomotor.minSpeed > Engine::Math::Fixed{})
					{
						MoveOrder aim{current->position.XY(), 0xFFFFFFFFu, MoveMode::Direct};
						aim.claim = GoalClaim::None;
						aim.explicitGoal = 1;
						*move = aim;
					}
					else if (move->mode == MoveMode::Point)
						move->mode = MoveMode::Idle;
				}
			}
			else if (aggression.stance == Stance::Guard && move->mode == MoveMode::Idle)
			{
				const Engine::Math::Fixed home = aggression.guardRadius / Engine::Math::Fixed::FromInt(2);
				if (Engine::Math::DistanceSquared(self, aggression.guardCenter) > home * home)
					*move = Replanned(MoveToPoint(aggression.guardCenter));
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::TargetingSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.targeting";
	// Its rows are independent: large chunks are shared out in pieces of 32 rows.
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<engine::gameplay::MovementSystem>;
	using After = SystemTypeList<>;
};
}
