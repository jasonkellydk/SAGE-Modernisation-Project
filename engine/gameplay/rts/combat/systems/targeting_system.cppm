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
		ecs::Optional<Targetable>, ecs::Optional<WeaponBonusConditions>, ecs::Optional<AiActivity>, ecs::Optional<Health>, ecs::Optional<TeamMember>, ecs::Optional<AttackMove>, ecs::Optional<BodyExtent>, ecs::Optional<MoveEnded>, ecs::Exclude<UnderConstruction>,
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
		ecs::Read<TriggerAreas>, ecs::Read<AttackPriorities>, ecs::Read<MoodRanges>, ecs::Read<RandomSeed>>;
	using Lookup = ecs::Lookup<ecs::Read<DefinitionRef>, ecs::Read<BodyExtent>, ecs::Read<Health>, ecs::Read<Subdual>, ecs::Read<UnderConstruction>>;

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
	// kept, a tie going to the greater priority. (The original also raised a container's priority to the greatest of
	// what it held: not yet.)
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
		Engine::Math::Fixed selfRadius = {}, const AttackGate *gate = nullptr)
	{
		// (The gate last: it may look the victim up.)
		const auto accepted = [&](const SpatialEntry &entry) {
			return Acceptable(relationships, entry, self, team, player, weapon, aggression, unfogged) && InArea(area, entry) &&
				(withinAttack == nullptr || WithinAttackRange(withinAttack->range, from, withinAttack->radius, entry));
		};
		if (prioritized != nullptr && prioritized->set != 0)
		{
			std::vector<std::pair<Engine::Math::Fixed, const SpatialEntry *>> near;
			const auto gather = [&](const SpatialEntry &entry) {
				if (!accepted(entry) || (gate != nullptr && !gate->Allows(entry)))
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
				const std::int64_t priority = prioritized->priorities.Priority(prioritized->set, ref != nullptr ? ref->index : 0xFFFFFFFFu);
				if (priority == 0)
					continue;
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
			if ((best == nullptr || gap < bestGap) && (gate == nullptr || gate->Allows(entry)))
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

			// Look for a new one on the scan tick (an undetected defector sees everyone as neutral: it looks for no one).
			const bool defecting = !targetables.empty() && (targetables[row].classes & target_class::Undetected) != 0;
			if (current == nullptr && tick >= aggression.nextScan && !defecting)
			{
				// getNextMoodTarget: the next look MoodAttackCheckRate on, the first after an idle entry or a wake-up
				// moved by GameLogicRandomValue(-half, half).
				aggression.nextScan = tick + aggression.scanInterval;
				if ((aggression.moodFlags & mood_flag::OffsetNext) != 0 && aggression.stance == Stance::Idle)
				{
					const std::int64_t half = static_cast<std::int64_t>(aggression.scanInterval >> 1);
					auto random = Engine::Math::Stream(context.Read<RandomSeed>().value, {tick, entities[row].index, entities[row].generation, 0x4D6Fu});
					aggression.nextScan = static_cast<std::uint64_t>(static_cast<std::int64_t>(aggression.nextScan) + Engine::Math::UniformInt(random, -half, half));
					aggression.moodFlags &= static_cast<std::uint8_t>(~mood_flag::OffsetNext);
				}
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
					// getNextMoodTarget (AI::getAdjustedVisionRangeForObject, owner type and mood): its vision times its
					// controller's guard outer modifier; carried, its longest weapon's reach instead, and its carrier's
					// bounding radius on top; a computer's unit by its mood (asleep: none; a passive one strikes back at its
					// last attacker only, unless that last hit was a heal); a human's only at what its weapons reach from
					// where it stands (WITHIN_ATTACK_RANGE).
					const MoodRanges &moods = context.Read<MoodRanges>();
					const bool human = unfogged;
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
						break;
					if (!human && aggression.attitude == attitude::Passive)
					{
						const Health *health = healthRows.empty() ? nullptr : &healthRows[row];
						// (aiAttackObject then: its attack state takes only what it may attack.)
						const SpatialEntry *attacker =
							health != nullptr && health->lastDamageType != moods.healingDamageType ? spatial.Find(health->lastAttacker) : nullptr;
						if (attacker != nullptr && Acceptable(relationships, *attacker, entities[row], selfTeam, player, weapon, looking, unfogged, gate))
							current = attacker;
						break;
					}
					const Reachable reach{weapon.attackRange, radius};
					// Within `range` of the gap between the bounding circles.
					current = Closest(spatial, relationships, self, self, range + radius, false, entities[row], selfTeam, player, weapon, looking, unfogged, nullptr, ranked,
						human ? &reach : nullptr, radius, gate);
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

			// Close in on targets beyond weapon range; stop once in range.
			if (move == nullptr || aggression.stance == Stance::Hold)
				continue;
			if (current != nullptr)
			{
				// outOfWeaponRangeObject: a weapon with leech range active is never out of range (it fires on where it stands).
				const std::uint8_t slot = sets.empty() ? std::uint8_t{0} : sets[row].current;
				const bool leeching = (target.leech >> slot & 1u) != 0;
				if (!leeching && !WithinAttackRange(weapon.attackRange, self, radius, *current))
				{
					// Pathfinder::findAttackPath: to a spot it may attack from, on the line between them (halfway into its
					// reach beyond the two bodies), slowing to arrive there rather than running on into the target; it
					// stops once in reach.
					const Engine::Math::FixedVector2 away = self - current->position.XY();
					const Engine::Math::Fixed apart = Engine::Math::Length(away);
					const Engine::Math::Fixed reach = weapon.attackRange / Engine::Math::Fixed::FromInt(2) + radius + current->radius;
					Engine::Math::FixedVector2 approach = current->position.XY();
					if (reach > Engine::Math::Fixed{} && apart > reach)
						approach = approach + away * (reach / apart);
					const Engine::Math::Fixed repath = Engine::Math::Fixed::FromInt(10);
					if (move->mode != MoveMode::Point || Engine::Math::DistanceSquared(move->destination, approach) > repath * repath)
						*move = MoveToPoint(approach, GoalClaim::Keep); // AIAttackApproachTargetState: no adjusting, its path's end claimed
				}
				else if (move->mode == MoveMode::Point)
					move->mode = MoveMode::Idle;
			}
			else if (aggression.stance == Stance::Guard && move->mode == MoveMode::Idle)
			{
				const Engine::Math::Fixed home = aggression.guardRadius / Engine::Math::Fixed::FromInt(2);
				if (Engine::Math::DistanceSquared(self, aggression.guardCenter) > home * home)
					*move = MoveToPoint(aggression.guardCenter);
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
