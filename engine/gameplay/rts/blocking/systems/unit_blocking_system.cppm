export module engine.gameplay.rts.blocking.systems.unit_blocking_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.blocking.components.blocking_unit;
export import engine.gameplay.rts.blocking.components.blocked_state;
export import engine.gameplay.rts.blocking.components.block_contact;
export import engine.gameplay.rts.blocking.algorithms.blocking_rules;
export import engine.gameplay.rts.collision.components.body_collision;
export import engine.gameplay.rts.collision.components.collider;
export import engine.gameplay.rts.collision.components.squishable;
export import engine.gameplay.rts.collision.resources.contacts;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.movement.algorithms.steering;
export import engine.gameplay.rts.navigation.components.navigation;
export import engine.gameplay.rts.navigation.components.pathfind_goal;
export import engine.gameplay.rts.navigation.components.ignored_obstacle;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.bounding_volume;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.components.carried;
export import engine.gameplay.common.identity.components.object_id;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.movement.components.move_away;
export import engine.gameplay.rts.movement.components.formation_member;
export import engine.gameplay.rts.docking.components.docking;
export import engine.gameplay.rts.harvesting.components.harvester;
export import engine.gameplay.rts.blocking.resources.move_away_requests;

// The units a moving ground unit runs into, as its AI weighs them (AIUpdateInterface::processCollision for a unit that is
// moving, EA's Zero Hour source), at the end of the tick once everything moved and the colliders are indexed. Every pair the partition
// finds touching (footprints within reach, heights overlapping; touching pairs are checked every tick) where both have a
// ground AI, unless one passes through the other (setIgnoreCollisionsWith, the obstacle its route ignores) or the crush
// deals with them (checkForOverlapCollision: it moves and one crushes the other), and unless it is ignoring collisions
// for now, or moving through units (canPathThroughUnits). A unit making way moves along its own route (MoveAway). A unit
// in its way (BlockedBy):
//   panicking infantry bounces off instead (not blocked; the bounce's push is not ported);
//   it is blocked, and goes no faster than the unit lets it (MaxBlockedSpeed) unless that unit, moving, waits for a route;
//   infantry in a non-infantry's way that is not already making way for it makes way (aiMoveAwayFromUnit, a
//     MoveAwayRequest), unless busy or using an ability, and that is all;
//   a first block counts one tick; facing its way (no need to rotate), blocked by something standing it is stuck; blocked
//     by a moving unit it blocks in turn, both facing their ways, the lower path priority makes way; still turning, its
//     count starts over.
// Each unit writes only its own BlockContact, weighing the others as the tick left them, so the order units are taken in
// does not matter (the original's one-by-one pass let a unit see a count another collision had just reset).
// A unit not moving (processCollision's other branch) that stands within half a cell of another not moving: each of the
// two that is idle (not busy, not using an ability, attacking nothing) moves to a spot it may stand on (UnitSettles).
// (Dead infantry pushed about by a crusher is not ported: physics pushes no unit with an AI.)
// The units moving and those not are gathered apart, a block of rows at a time, so each kind's pairs are weighed for it
// alone.
export namespace engine::gameplay
{
struct UnitBlockingSystem
{
	using Query = ecs::Query<ecs::Write<BlockContact>, ecs::Read<BlockingUnit>, ecs::Read<BlockedState>, ecs::Read<MoveOrder>, ecs::Read<Locomotion>,
		ecs::Read<Transform>, ecs::Optional<MoveAway>, ecs::Exclude<OffMap>, ecs::Exclude<Carried>>;
	using Lookup = ecs::Lookup<ecs::Read<Transform>, ecs::Read<Locomotion>, ecs::Read<MoveOrder>, ecs::Read<BlockingUnit>, ecs::Read<BlockedState>,
		ecs::Read<ObjectId>, ecs::Read<BoundingVolume>, ecs::Read<Collider>, ecs::Read<Route>, ecs::Read<PathfindGoal>, ecs::Read<Disabled>,
		ecs::Read<Health>, ecs::Read<Owner>, ecs::Read<Squishable>, ecs::Read<BodyCollision>, ecs::Read<IgnoredObstacle>, ecs::Read<AiActivity>,
		ecs::Read<MoveAway>, ecs::Read<AttackTarget>, ecs::Read<FormationMember>, ecs::Read<Docking>, ecs::Read<Harvester>>;
	using Resources = ecs::Resources<ecs::Read<ColliderIndex>, ecs::Read<Relationships>, ecs::Write<MoveAwayRequests>, ecs::Write<UnitSettles>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const
	{
		context.Write<MoveAwayRequests>().Reset(query.PreparedChunkCount());
		context.Write<UnitSettles>().Reset(query.PreparedChunkCount());
	}

	// A unit as the rules see it, with what the collision test needs.
	struct Seen
	{
		BlockingBody body;
		FixedVector2 steering; // the point it steers at along its route (needToRotate's point on its path)
		ecs::Entity entity;
		ecs::Entity awayFrom, awayFromBefore; // whom it makes way for, while it does (isMovingAwayFrom)
		const BoundingVolume *volume{nullptr};
		const Collider *collider{nullptr};
		Fixed height;
		ecs::Entity ignored, obstacle;
		std::uint32_t player{0};
		bool unmanned{false};
		bool squishable{false};
		bool busy{false};
		bool idle{false}; // isIdle: no order, attacking nothing, not busy
	};

	// A ground unit with an AI as the rules see it; none when it is not one (any more: the collider index was built before
	// the tick, so what it finds may since have died or been carried off).
	template<typename Lookups>
	static std::optional<Seen> See(const Lookups &lookup, ecs::Entity entity)
	{
		const Transform *transformAt = lookup.template Get<Transform>(entity);
		const Locomotion *motionAt = lookup.template Get<Locomotion>(entity);
		const MoveOrder *orderAt = lookup.template Get<MoveOrder>(entity);
		const BlockedState *stateAt = lookup.template Get<BlockedState>(entity);
		const BlockingUnit *unitAt = lookup.template Get<BlockingUnit>(entity);
		if (transformAt == nullptr || motionAt == nullptr || orderAt == nullptr || stateAt == nullptr || unitAt == nullptr)
			return std::nullopt;
		Seen seen;
		const Transform &transform = *transformAt;
		const Locomotion &motion = *motionAt;
		const MoveAway *away = lookup.template Get<MoveAway>(entity);
		// Making way: its own order and route wait.
		const MoveOrder &order = away != nullptr ? away->order : *orderAt;
		const BlockedState &state = *stateAt;
		const Route *route = away != nullptr ? &away->route : lookup.template Get<Route>(entity);
		const Disabled *disabled = lookup.template Get<Disabled>(entity);
		const Health *health = lookup.template Get<Health>(entity);
		const PathfindGoal *goal = lookup.template Get<PathfindGoal>(entity);
		const ObjectId *id = lookup.template Get<ObjectId>(entity);
		const Owner *owner = lookup.template Get<Owner>(entity);
		const BodyCollision *collision = lookup.template Get<BodyCollision>(entity);
		const IgnoredObstacle *obstacle = lookup.template Get<IgnoredObstacle>(entity);
		const AiActivity *activity = lookup.template Get<AiActivity>(entity);
		const std::uint32_t disabledMask = disabled != nullptr ? disabled->mask : 0u;
		seen.entity = entity;
		if (away != nullptr)
		{
			seen.awayFrom = state.awayFrom;
			seen.awayFromBefore = state.awayFromBefore;
		}
		BlockingBody &body = seen.body;
		body.position = transform.position.XY();
		body.facing = transform.facing;
		body.direction = UnitDirection(transform.facing);
		body.goal = order.destination;
		body.speed = motion.speed;
		body.id = id != nullptr ? id->value : 0u;
		body.frames = state.frames;
		body.kinds = unitAt->kinds;
		body.moving = order.mode != MoveMode::Idle && order.held == 0;
		body.ground = IsGroundLocomotor(motion.locomotor) && (disabledMask & disabled_type::Held) == 0;
		body.dead = health != nullptr && IsDead(*health);
		body.backwards = motion.backwards != 0;
		body.goalCell = goal != nullptr && goal->x > 0 && goal->y > 0;
		const bool planned = route != nullptr && route->planned;
		// Routed (a point, or a waypoint path's leg once it has a route): waiting for its route.
		const bool routed = order.mode == MoveMode::Point || (RoutedMode(order.mode) && route != nullptr);
		body.waiting = (routed && !planned) || state.replanAt != 0;
		body.wanderer = motion.locomotor.wanderWidth > Fixed{};
		body.panicking = order.mode == MoveMode::Panic;
		if (const FormationMember *formation = lookup.template Get<FormationMember>(entity))
			body.formation = formation->id;
		seen.steering = order.destination;
		if (routed && planned && route->next < route->count && !(route->complete && route->next + 1u >= route->count && route->destination != order.destination))
			seen.steering = route->points[route->next];
		seen.volume = lookup.template Get<BoundingVolume>(entity);
		seen.collider = lookup.template Get<Collider>(entity);
		seen.height = transform.position.z;
		seen.ignored = collision != nullptr ? collision->ignored : ecs::Entity{};
		seen.obstacle = state.ignoring != ecs::Entity{} ? state.ignoring : obstacle != nullptr ? obstacle->obstacle : ecs::Entity{};
		seen.player = owner != nullptr ? owner->player : 0u;
		seen.unmanned = (disabledMask & disabled_type::Unmanned) != 0;
		seen.squishable = lookup.template Get<Squishable>(entity) != nullptr;
		seen.busy = activity != nullptr && activity->Occupied();
		const AttackTarget *attack = lookup.template Get<AttackTarget>(entity);
		// AIUpdateInterface::isIdle: not while its dock machine runs (a mover waiting its turn at a dock stands still but is
		// docking: nothing moves it aside), nor while a supply truck's round has it wanting, docking or regrouping (its
		// SupplyTruckStateMachine runs in its own AI update: done docking, it is off to its next dock at once).
		const Docking *docking = lookup.template Get<Docking>(entity);
		const Harvester *harvester = lookup.template Get<Harvester>(entity);
		const bool onRound = harvester != nullptr && (harvester->state == HarvesterState::Wanting || harvester->state == HarvesterState::Docking ||
			harvester->state == HarvesterState::Regrouping);
		seen.idle = away == nullptr && order.mode == MoveMode::Idle && !seen.busy && (attack == nullptr || !attack->target.IsValid()) &&
			(docking == nullptr || !IsDocking(*docking)) && !onRound;
		return seen;
	}

	// The partition's overlap of `self` and `entity`: footprints within reach, heights overlapping.
	template<typename Lookups>
	static bool Touching(const Lookups &lookup, const Seen &self, ecs::Entity entity)
	{
		return Touching(lookup, self.body.position, self.height, *self.volume, entity);
	}
	// The same from the unit's place, height and volume alone (before its whole view is worked out).
	template<typename Lookups>
	static bool Touching(const Lookups &lookup, FixedVector2 position, Fixed selfHeight, const BoundingVolume &selfVolume, ecs::Entity entity)
	{
		const Transform *at = lookup.template Get<Transform>(entity);
		const BoundingVolume *volume = lookup.template Get<BoundingVolume>(entity);
		if (at == nullptr || volume == nullptr)
			return false;
		const Fixed reach = selfVolume.circleRadius + volume->circleRadius;
		if (Engine::Math::DistanceSquared(position, at->position.XY()) > reach * reach)
			return false;
		const Fixed height = at->position.z;
		return !(selfHeight - selfVolume.below > height + volume->above || height - volume->below > selfHeight + selfVolume.above);
	}

	// A unit's own view, worked out once, when first needed (See only reads: when it is worked out never shows). Null: the
	// unit is no grounded, living ground unit with a volume, and weighs nothing.
	template<typename Lookups>
	struct LazySelf
	{
		const Lookups &lookup;
		ecs::Entity entity;
		std::optional<Seen> seen{};
		bool resolved{false};

		const Seen *Get()
		{
			if (!resolved)
			{
				resolved = true;
				seen = See(lookup, entity);
				if (seen && (!seen->body.ground || seen->body.dead || seen->volume == nullptr))
					seen.reset();
			}
			return seen ? &*seen : nullptr;
		}
		bool Rejected() const noexcept { return resolved && !seen; }
	};

	static bool Crushes(const Seen &crusher, const Seen &crushed, const Relationships &relationships, bool squish)
	{
		if (crusher.collider == nullptr || crushed.collider == nullptr)
			return false;
		const bool allies = relationships.Between(crusher.player, crushed.player) == Relationship::Allies;
		return CanCrushOrSquish(crusher.collider->crusherLevel, crusher.unmanned, allies, squish && crushed.squishable, crushed.collider->crushableLevel);
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const ColliderIndex &index = context.Read<ColliderIndex>();
		const Relationships &relationships = context.Read<Relationships>();
		const auto lookup = context.Lookup<Lookup>();
		const std::uint64_t tick = context.Tick();
		auto contacts = chunk.Get<BlockContact>();
		const auto states = chunk.Get<BlockedState>();
		const auto mainOrders = chunk.Get<MoveOrder>();
		const auto aways = chunk.Get<MoveAway>();
		const auto entities = chunk.Entities();
		// A chunk of units making way moves by their own orders.
		const auto orderOf = [&](std::size_t row) -> const MoveOrder & { return aways.empty() ? mainOrders[row] : aways[row].order; };
		auto &requests = context.Write<MoveAwayRequests>().Slot(context);
		auto &settles = context.Write<UnitSettles>().Slot(context);
		constexpr std::size_t Block = 64;
		std::array<std::uint16_t, Block> moving{};
		for (std::size_t first = 0; first < contacts.size(); first += Block)
		{
			// The block's moving units, gathered without branching (isMoving, not ignoring collisions for now).
			const std::size_t last = std::min(contacts.size(), first + Block);
			std::size_t count = 0;
			for (std::size_t row = first; row < last; ++row)
			{
				moving[count] = static_cast<std::uint16_t>(row);
				const MoveOrder &order = orderOf(row);
				count += static_cast<std::size_t>(order.mode != MoveMode::Idle) & static_cast<std::size_t>(order.held == 0) &
					static_cast<std::size_t>(states[row].ignoreUntil <= tick) & static_cast<std::size_t>((states[row].throughUnits | states[row].docking) == 0);
			}
			for (std::size_t at = 0; at < count; ++at)
			{
				const std::size_t row = moving[at];
				// Its place and volume first (a unit without either weighs nothing: See has no volume or nothing for it); its whole
				// view only once something touches it.
				const Transform *selfAt = lookup.template Get<Transform>(entities[row]);
				const BoundingVolume *selfVolume = lookup.template Get<BoundingVolume>(entities[row]);
				if (selfAt == nullptr || selfVolume == nullptr)
					continue;
				const FixedVector2 selfPosition = selfAt->position.XY();
				const Fixed selfHeight = selfAt->position.z;
				LazySelf<decltype(lookup)> lazySelf{lookup, entities[row]};
				BlockContact &contact = contacts[row];
				index.ForEachWithin(selfPosition, selfVolume->circleRadius + Fixed::FromInt(64), [&](const SpatialEntry &entry) {
					const ecs::Entity entity = entry.entity;
					if (lazySelf.Rejected() || entity == entities[row])
						return;
					// The partition's overlap first (footprints and heights, from two lookups), the unit's whole view only for
					// what touches it.
					if (!Touching(lookup, selfPosition, selfHeight, *selfVolume, entity))
						return;
					const Seen *selfSeen = lazySelf.Get();
					if (selfSeen == nullptr)
						return;
					const Seen &self = *selfSeen;
					const std::optional<Seen> seenOther = See(lookup, entity);
					if (!seenOther || seenOther->volume == nullptr)
						return;
					const Seen &other = *seenOther;
					// What either passes through.
					if (self.ignored == entity || other.ignored == entities[row] || self.obstacle == entity || other.obstacle == entities[row])
						return;
					// The crush's own business while it moves (checkForOverlapCollision: TEST_CRUSH_ONLY either way).
					if (self.body.speed != Fixed{} && (Crushes(other, self, relationships, false) || Crushes(self, other, relationships, false)))
						return;
					if (!other.body.ground)
						return;
					Weigh(self, other, contact, relationships, requests);
				});
			}
			// The block's units not moving.
			count = 0;
			for (std::size_t row = first; row < last; ++row)
			{
				const MoveOrder &order = orderOf(row);
				moving[count] = static_cast<std::uint16_t>(row);
				count += static_cast<std::size_t>(order.mode == MoveMode::Idle || order.held != 0) & static_cast<std::size_t>(states[row].ignoreUntil <= tick) &
					static_cast<std::size_t>((states[row].throughUnits | states[row].docking) == 0);
			}
			for (std::size_t at = 0; at < count; ++at)
			{
				const std::size_t row = moving[at];
				// Its place first (none: See has nothing for it); its whole view only once something stands within half a cell.
				const Transform *selfAt = lookup.template Get<Transform>(entities[row]);
				if (selfAt == nullptr)
					continue;
				const FixedVector2 selfPosition = selfAt->position.XY();
				LazySelf<decltype(lookup)> lazySelf{lookup, entities[row]};
				// Only what stands within half a cell can count (and the index was built before the tick: a margin for what
				// moved since).
				index.ForEachWithin(selfPosition, Fixed::FromInt(5 + 16), [&](const SpatialEntry &entry) {
					const ecs::Entity entity = entry.entity;
					if (lazySelf.Rejected() || entity == entities[row])
						return;
					const Transform *at = lookup.template Get<Transform>(entity);
					if (at == nullptr || Engine::Math::DistanceSquared(selfPosition, at->position.XY()) >= Fixed::FromInt(25))
						return;
					const Seen *selfSeen = lazySelf.Get();
					if (selfSeen == nullptr)
						return;
					const Seen &self = *selfSeen;
					const std::optional<Seen> seenOther = See(lookup, entity);
					if (!seenOther || seenOther->volume == nullptr || !seenOther->body.ground || seenOther->body.moving)
						return;
					const Seen &other = *seenOther;
					if (self.ignored == entity || other.ignored == entities[row] || self.obstacle == entity || other.obstacle == entities[row])
						return;
					// Half a cell: PATHFIND_CELL_SIZE_F^2 x 0.25.
					if (Engine::Math::DistanceSquared(self.body.position, other.body.position) >= Fixed::FromInt(25))
						return;
					if (self.height - self.volume->below > other.height + other.volume->above || other.height - other.volume->below > self.height + self.volume->above)
						return;
					if (self.busy)
						return;
					if (self.idle)
						settles.push_back(self.entity);
					if (other.idle)
						settles.push_back(other.entity);
				});
			}
		}
	}

	// processCollision for a moving unit against `other`.
	static void Weigh(const Seen &self, const Seen &other, BlockContact &contact, const Relationships &relationships, std::vector<MoveAwayRequest> &requests)
	{
		if (!BlockedBy(self.body, other.body, Crushes(self, other, relationships, true)))
			return;
		if ((self.body.kinds & blocking_kind::Infantry) != 0 && self.body.panicking)
			return;
		contact.blocked = 1;
		if (other.body.moving && other.body.waiting)
			return;
		const Fixed most = MaxBlockedSpeed(self.body, other.body, contact.maxSpeed);
		if (most < contact.maxSpeed)
			contact.maxSpeed = most;
		// Infantry in a non-infantry's way, not already making way for it, makes way unless busy; that is all.
		const bool makingWay = other.awayFrom == self.entity || other.awayFromBefore == self.entity;
		if (!makingWay && (other.body.kinds & blocking_kind::Infantry) != 0 && (self.body.kinds & blocking_kind::Infantry) == 0)
		{
			if (!other.busy)
				requests.push_back({other.entity, self.entity});
			return;
		}
		contact.frames = std::max(contact.frames, block_frames::AtLeastOne);
		if (NeedToRotate(self.body, self.steering))
		{
			contact.frames = block_frames::One;
			return;
		}
		if (!other.body.moving)
		{
			contact.stuck = 1;
			return;
		}
		// Deadlocked with a unit it blocks in turn, both facing their ways: the lower path priority makes way.
		if (BlockedBy(other.body, self.body, Crushes(other, self, relationships, true)) && !NeedToRotate(other.body, other.steering) &&
			!HasHigherPathPriority(self.body, other.body))
			requests.push_back({self.entity, other.entity});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::UnitBlockingSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.unit_blocking";
	// Its rows are independent: chunks are shared out in pieces of 8 rows (each piece its own output slot, in row order: the
	// requests and settles come out in the same order however the rows are cut). Its cost sits in a few moving rows, so
	// small pieces let the scheduler spread them.
	static constexpr std::size_t PieceRows = 32;
	// The partition's collisions at the end of the frame, once everything moved (the collider index, built before the tick,
	// only finds the candidates: their positions are read as they are now).
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
