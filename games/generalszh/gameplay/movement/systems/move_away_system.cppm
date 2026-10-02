export module games.generalszh.gameplay.movement.systems.move_away_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.movement.algorithms.goal_claim_rules;
export import engine.gameplay.rts.navigation.algorithms.move_away_search;
export import engine.gameplay.rts.navigation.resources.unit_cells;
export import engine.gameplay.rts.blocking.components.blocking_unit;
export import engine.gameplay.rts.blocking.components.blocked_state;
export import engine.gameplay.rts.blocking.components.block_contact;
export import engine.gameplay.rts.blocking.resources.move_away_requests;
export import engine.gameplay.rts.movement.components.move_away;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.systems.route_request_system;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.health.components.health;

// Units making way for others, at the end of the tick (AIUpdate.cpp AIUpdateInterface::privateMoveAwayFromUnit and
// AIPathfind.cpp Pathfinder::moveAllies, EA's Zero Hour source):
//   a unit with a route planned this tick that wants a clear way (a dozer or harvester) or whose route allies stand fixed
//   on (blocked by allies) asks the allies standing in its footprint along the route to make way, unless they move,
//   attack or are busy, infantry does not move infantry, and infantry moves a vehicle only when blocked by it; then the
//   tick's collisions' requests (UnitBlockingSystem), in their order.
//   A unit asked to make way (alive, not already making way for that one: then, blocked, it ignores collisions for two
//   seconds) remembers whom it makes way for (and the one before), and looks for a route out of both their ways
//   (FindMoveAway); none: it moves through units from now on. Found: it makes way along it for at most ten seconds
//   (MoveAway), and asks the allies on that route in turn, two deep at most (m_moveAlliesDepth).
// A unit already making way that is asked by another gets the new way at once (the original's new temporary state
// destroyed the path it had just been given, so it was left with none: a retail bug, not kept).
// One request after another, as the original's objects updated one after another; each unit's changes are kept for the
// rest of the pass and written once it is done.
export namespace generalszh::gameplay
{
struct MoveAwaySystem
{
	using Query = ecs::Query<ecs::Read<gp::Route>, ecs::Read<gp::BlockingUnit>, ecs::Exclude<gp::OffMap>>;
	using Lookup = ecs::Lookup<ecs::Read<gp::Transform>, ecs::Read<gp::NavigationAgent>, ecs::Read<gp::Owner>, ecs::Read<gp::TeamMember>,
		ecs::Read<gp::Collider>, ecs::Read<gp::Squishable>, ecs::Read<gp::Disabled>, ecs::Read<gp::Dying>, ecs::Read<gp::Passenger>, ecs::Read<gp::OffMap>,
		ecs::Read<gp::BlockingUnit>, ecs::Read<gp::BlockedState>, ecs::Read<gp::BlockContact>, ecs::Read<gp::MoveAway>, ecs::Read<gp::MoveOrder>,
		ecs::Read<gp::Route>, ecs::Read<gp::IgnoredObstacle>, ecs::Read<gp::AiActivity>, ecs::Read<gp::AttackTarget>, ecs::Read<gp::Health>>;
	using Resources = ecs::Resources<ecs::Read<gp::NavigationGrid>, ecs::Read<gp::GoalCells>, ecs::Read<gp::UnitCells>, ecs::Read<gp::Relationships>,
		ecs::Read<gp::TeamRoster>, ecs::Read<gp::MoveAwayRequests>, ecs::Write<gp::RouteScratchPool>>;

	// A unit's AI as this pass left it.
	struct Staged
	{
		ecs::Entity entity;
		gp::BlockedState state;
		std::optional<gp::MoveAway> away;
		bool hadAway{false};
		bool changed{false};
	};

	template<typename Lookups>
	struct Pass
	{
		const Lookups &lookup;
		const gp::NavigationGrid &grid;
		const gp::GoalCells &goals;
		const gp::UnitCells &units;
		const gp::Relationships &relationships;
		const gp::TeamRoster &roster;
		gp::RouteScratch &scratch;
		std::uint64_t tick;
		std::vector<Staged> staged;
		std::uint32_t depth{0}; // m_moveAlliesDepth

		Staged *Stage(ecs::Entity entity)
		{
			for (Staged &entry : staged)
				if (entry.entity == entity)
					return &entry;
			const gp::BlockedState *state = lookup.template Get<gp::BlockedState>(entity);
			if (state == nullptr)
				return nullptr;
			Staged entry;
			entry.entity = entity;
			entry.state = *state;
			if (const gp::MoveAway *away = lookup.template Get<gp::MoveAway>(entity))
			{
				entry.away = *away;
				entry.hadAway = true;
			}
			staged.push_back(entry);
			return &staged.back();
		}

		// The route a unit is on now (its way out, while it makes way).
		const gp::Route *RouteOf(ecs::Entity entity)
		{
			if (Staged *entry = Find(entity); entry != nullptr && entry->away)
				return &entry->away->route;
			if (const gp::MoveAway *away = lookup.template Get<gp::MoveAway>(entity))
				return &away->route;
			return lookup.template Get<gp::Route>(entity);
		}
		Staged *Find(ecs::Entity entity)
		{
			for (Staged &entry : staged)
				if (entry.entity == entity)
					return &entry;
			return nullptr;
		}
		bool Moving(ecs::Entity entity)
		{
			if (Staged *entry = Find(entity); entry != nullptr && entry->away)
				return true;
			if (lookup.template Get<gp::MoveAway>(entity) != nullptr)
				return true;
			const gp::MoveOrder *order = lookup.template Get<gp::MoveOrder>(entity);
			return order != nullptr && order->mode != gp::MoveMode::Idle && order->held == 0;
		}
		gp::RouteUnits UnitsFor(ecs::Entity entity, const gp::NavigationAgent &agent, bool throughUnits) const
		{
			gp::RouteUnits view;
			view.cells = &units;
			view.goals = &goals;
			view.relationships = &relationships;
			view.self = entity;
			const auto *ignored = lookup.template Get<gp::IgnoredObstacle>(entity);
			view.ignored = ignored != nullptr ? ignored->obstacle : ecs::Entity{};
			const auto *owner = lookup.template Get<gp::Owner>(entity);
			const auto *member = lookup.template Get<gp::TeamMember>(entity);
			const auto *collider = lookup.template Get<gp::Collider>(entity);
			const auto *disabled = lookup.template Get<gp::Disabled>(entity);
			view.player = owner != nullptr ? owner->player : 0u;
			view.team = member != nullptr ? member->team : gp::Relationships::NoTeam;
			view.crusherLevel = collider != nullptr ? collider->crusherLevel : 0u;
			view.unmanned = disabled != nullptr && (disabled->mask & gp::disabled_type::Unmanned) != 0;
			view.centered = agent.centered != 0;
			view.throughUnits = throughUnits;
			if (const auto *state = lookup.template Get<gp::BlockedState>(entity); state != nullptr && state->ignoring != ecs::Entity{})
				view.ignored = state->ignoring;
			return view;
		}

		// privateMoveAwayFromUnit: `mover` makes way for `unit`.
		void MoveAway(ecs::Entity mover, ecs::Entity unit)
		{
			const gp::Health *health = lookup.template Get<gp::Health>(mover);
			if (lookup.template Get<gp::Dying>(mover) != nullptr || (health != nullptr && gp::IsDead(*health)))
				return;
			const gp::NavigationAgent *agent = lookup.template Get<gp::NavigationAgent>(mover);
			const gp::Transform *transform = lookup.template Get<gp::Transform>(mover);
			Staged *entry = Stage(mover);
			if (agent == nullptr || transform == nullptr || entry == nullptr)
				return;
			if (entry->away && (entry->state.awayFrom == unit || entry->state.awayFromBefore == unit))
			{
				// Asked again by the one it makes way for: blocked, it cheats for two seconds.
				const gp::BlockContact *contact = lookup.template Get<gp::BlockContact>(mover);
				if (contact != nullptr && contact->blocked != 0)
				{
					entry->state.ignoreUntil = tick + 60;
					entry->changed = true;
				}
				return;
			}
			entry->state.awayFromBefore = entry->state.awayFrom;
			entry->state.awayFrom = unit;
			entry->changed = true;
			const gp::Route *unitRoute = RouteOf(unit);
			const gp::Transform *unitAt = lookup.template Get<gp::Transform>(unit);
			const gp::NavigationAgent *unitAgent = lookup.template Get<gp::NavigationAgent>(unit);
			if (unitRoute == nullptr || !unitRoute->planned || unitAt == nullptr)
				return;
			const gp::AvoidedWay avoid = gp::WayOf(unitAt->position.XY(), *unitRoute);
			std::optional<gp::AvoidedWay> avoid2;
			if (const ecs::Entity before = entry->state.awayFromBefore; before != ecs::Entity{})
				if (const gp::Route *route2 = RouteOf(before); route2 != nullptr && route2->planned)
					if (const gp::Transform *at2 = lookup.template Get<gp::Transform>(before))
						avoid2 = gp::WayOf(at2->position.XY(), *route2);
			const gp::ClearancePlane *plane = grid.ClearanceFor(agent->surfaces);
			if (plane == nullptr)
				return;
			const auto *ignored = lookup.template Get<gp::IgnoredObstacle>(mover);
			const auto *owner = lookup.template Get<gp::Owner>(mover);
			const gp::GoalSeeker seeker{mover, ignored != nullptr ? ignored->obstacle : ecs::Entity{}, FootprintOf(*agent), agent->surfaces,
				owner == nullptr || HumanMover(roster, owner->player), transform->position.XY()};
			const GoalClaimRules<Lookups> rules{lookup, relationships, mover};
			const gp::RouteUnits view = UnitsFor(mover, *agent, entry->state.throughUnits != 0);
			const gp::GoalFootprint otherFootprint = unitAgent != nullptr ? FootprintOf(*unitAgent) : gp::GoalFootprint{};
			const gp::FoundRoute found = gp::FindMoveAway(grid, *plane, gp::RouteMover{agent->radius, 5000, seeker.ignored}, transform->position.XY(), scratch,
				&view, goals, seeker, otherFootprint, avoid, avoid2 ? &*avoid2 : nullptr, rules);
			if (found.points.size() < 2)
			{
				// No way out: it moves through units (a second search, through units, would find the same nothing).
				entry->state.throughUnits = 1;
				return;
			}
			gp::MoveAway away;
			away.route.destination = found.points.back();
			away.route.planned = true;
			away.route.complete = true;
			away.route.plannedTick = tick;
			away.route.blockedByAlly = found.blockedByAlly;
			const std::size_t kept = std::min(found.points.size() - 1, gp::RoutePoints);
			for (std::size_t index = 0; index < kept; ++index)
			{
				away.route.points[index] = found.points[found.points.size() - kept + index];
				away.route.layers[index] = gp::GroundLayer;
			}
			away.route.count = static_cast<std::uint8_t>(kept);
			away.order = gp::MoveToPoint(found.points.back());
			away.until = tick + gp::MoveAwayTicks;
			entry = Find(mover);
			entry->away = away;
			const gp::BlockingUnit *kind = lookup.template Get<gp::BlockingUnit>(mover);
			if (kind == nullptr || !kind->Is(gp::blocking_kind::NoCollide))
			{
				const gp::Route route = away.route; // the staged entries may move as more units are asked
				MoveAllies(mover, *agent, transform->position.XY(), route);
			}
		}

		// Pathfinder::moveAllies: the allies standing in `mover`'s footprint along `route` make way.
		void MoveAllies(ecs::Entity mover, const gp::NavigationAgent &agent, Engine::Math::FixedVector2 from, const gp::Route &route)
		{
			const gp::BlockingUnit *kind = lookup.template Get<gp::BlockingUnit>(mover);
			if (kind == nullptr)
				return;
			const bool clearWay = kind->Is(gp::blocking_kind::Dozer) || kind->Is(gp::blocking_kind::Harvester);
			if (!clearWay && !route.blockedByAlly)
				return;
			if (depth >= 2)
				return;
			++depth;
			const auto *owner = lookup.template Get<gp::Owner>(mover);
			const auto *member = lookup.template Get<gp::TeamMember>(mover);
			const auto *ignored = lookup.template Get<gp::IgnoredObstacle>(mover);
			const std::uint32_t player = owner != nullptr ? owner->player : 0u;
			const std::uint32_t team = member != nullptr ? member->team : gp::Relationships::NoTeam;
			const ecs::Entity ignore = ignored != nullptr ? ignored->obstacle : ecs::Entity{};
			const bool infantry = kind->Is(gp::blocking_kind::Infantry);
			const std::int32_t above = agent.radius + (agent.centered != 0 ? 1 : 0);
			const auto cellOf = [](Engine::Math::Fixed value) { return static_cast<std::int32_t>((value / Engine::Math::Fixed::FromInt(gp::PathfindCellSize)).Floor()); };
			// The route's cells, from its end back (not the cell it starts in).
			std::vector<std::array<std::int32_t, 2>> cells;
			Engine::Math::FixedVector2 previous = from;
			for (std::uint32_t point = route.next; point < route.count; ++point)
			{
				const Engine::Math::FixedVector2 next = route.points[point];
				gp::route_detail::InSight(cellOf(previous.x), cellOf(previous.y), cellOf(next.x), cellOf(next.y), [&](std::int32_t x, std::int32_t y) {
					if (cells.empty() || cells.back() != std::array<std::int32_t, 2>{x, y})
						cells.push_back({x, y});
					return true;
				});
				previous = next;
			}
			if (!cells.empty())
				cells.erase(cells.begin());
			for (auto cell = cells.rbegin(); cell != cells.rend(); ++cell)
				for (std::int32_t x = (*cell)[0] - agent.radius; x < (*cell)[0] + above; ++x)
					for (std::int32_t y = (*cell)[1] - agent.radius; y < (*cell)[1] + above; ++y)
					{
						const gp::UnitOccupant *other = units.At(x, y);
						if (other == nullptr || other->entity == mover || other->entity == ignore)
							continue;
						if (!relationships.Allies(team, player, other->team, other->player))
							continue;
						const bool otherInfantry = (other->flags & gp::unit_cell_flag::Infantry) != 0;
						if (infantry && otherInfantry)
							continue;
						if (infantry && !otherInfantry && !route.blockedByAlly)
							continue;
						if (Moving(other->entity))
							continue;
						if (const auto *attack = lookup.template Get<gp::AttackTarget>(other->entity); attack != nullptr && attack->target.IsValid())
							continue;
						if (const auto *activity = lookup.template Get<gp::AiActivity>(other->entity); activity != nullptr && activity->Occupied())
							continue;
						MoveAway(other->entity, mover);
					}
			--depth;
		}
	};

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		gp::RouteScratchPool &pool = context.Write<gp::RouteScratchPool>();
		pool.Reset(1);
		Pass<decltype(lookup)> pass{lookup, context.Read<gp::NavigationGrid>(), context.Read<gp::GoalCells>(), context.Read<gp::UnitCells>(),
			context.Read<gp::Relationships>(), context.Read<gp::TeamRoster>(), pool.Slot(0), context.Tick(), {}};
		// Routes planned this tick clear the allies off them (computePath's moveAllies).
		query.ForEachChunk([&](auto chunk) {
			const auto routes = chunk.template Get<gp::Route>();
			const auto kinds = chunk.template Get<gp::BlockingUnit>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < routes.size(); ++row)
			{
				const gp::Route &route = routes[row];
				if (!route.planned || route.plannedTick != pass.tick || kinds[row].Is(gp::blocking_kind::NoCollide))
					continue;
				const gp::NavigationAgent *agent = lookup.template Get<gp::NavigationAgent>(entities[row]);
				const gp::Transform *transform = lookup.template Get<gp::Transform>(entities[row]);
				if (agent != nullptr && transform != nullptr)
					pass.MoveAllies(entities[row], *agent, transform->position.XY(), route);
			}
		});
		// Then the collisions' requests, in order.
		context.Read<gp::MoveAwayRequests>().ForEach([&](const gp::MoveAwayRequest &request) { pass.MoveAway(request.mover, request.from); });
		for (const Staged &entry : pass.staged)
		{
			if (!entry.changed)
				continue;
			context.Commands().Set<gp::BlockedState>(entry.entity, entry.state);
			if (entry.away && entry.hadAway)
				context.Commands().Set<gp::MoveAway>(entry.entity, *entry.away);
			else if (entry.away)
				context.Commands().Add<gp::MoveAway>(entry.entity, *entry.away);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::MoveAwaySystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.move_away";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	// One request after another.
	static constexpr bool Batch = true;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
