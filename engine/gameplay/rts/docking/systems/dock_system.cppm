export module engine.gameplay.rts.docking.systems.dock_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.docking.components.dock;
export import engine.gameplay.rts.docking.components.docking;
export import engine.gameplay.rts.docking.algorithms.dock_points;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.rts.navigation.algorithms.clearance;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.random.resources.random_seed;
import Engine.Core.Math.FixedRandom;

// Walks docking movers through their phases, once a tick before routing and
// movement (a batch: movers at one dock share its approach points and its
// turn, so they go in one deterministic pass, in entity order). What the
// original kept on the dock (who holds each approach point and has reached
// it, the ready queue, the active docker) is read off the movers' own
// Docking: first the docks with no active docker give their turn to the
// first mover in their queue standing at its point (DockUpdate::update);
// then each mover runs its phase (AIDockMachine: the phase's update, or its
// start when it has just been entered, then the phase's transitions, its
// success and failure phases, each start in turn, as the original's state
// machine). A docking that ends (the dock gone or closed, the business done
// and the mover out, or no approach point to be had) leaves the mover idle.
export namespace engine::gameplay
{
struct DockSystem
{
	using Query = ecs::Query<ecs::Write<Docking>, ecs::Write<MoveOrder>, ecs::Read<Transform>, ecs::Optional<Targetable>, ecs::Optional<NavigationAgent>,
		ecs::OptionalWrite<Route>>;
	using Lookup = ecs::Lookup<ecs::Read<Dock>, ecs::Read<Transform>, ecs::Read<Targetable>>;
	using Resources = ecs::Resources<ecs::Read<NavigationGrid>, ecs::Read<RandomSeed>>;

	enum class Status : std::uint8_t
	{
		Continue,
		Success,
		Failure,
	};

	struct Mover
	{
		ecs::Entity entity;
		Docking *docking;
		MoveOrder *order;
		const Transform *transform;
		Engine::Math::Fixed radius;
		const NavigationAgent *agent;
		Route *route;
	};

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		std::vector<Mover> movers;
		query.ForEachChunk([&](auto chunk) {
			auto dockings = chunk.template Get<Docking>();
			auto orders = chunk.template Get<MoveOrder>();
			const auto transforms = chunk.template Get<Transform>();
			const auto targetables = chunk.template Get<Targetable>();
			const auto agents = chunk.template Get<NavigationAgent>();
			auto routes = chunk.template Get<Route>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < dockings.size(); ++row)
				if (IsDocking(dockings[row]))
					movers.push_back({entities[row], &dockings[row], &orders[row], &transforms[row],
						targetables.empty() ? Engine::Math::Fixed{} : targetables[row].radius, agents.empty() ? nullptr : &agents[row],
						routes.empty() ? nullptr : &routes[row]});
		});
		if (movers.empty())
			return;
		std::sort(movers.begin(), movers.end(), [](const Mover &a, const Mover &b) { return a.entity.index < b.entity.index; });
		GiveTurns(movers, lookup);
		for (Mover &mover : movers)
			Run(mover, movers, lookup, context);
	}

	// DockUpdate::update: a dock with no active docker (and not crippled) gives its turn to the first in its ready
	// queue standing at its approach point.
	template<typename LookupType>
	static void GiveTurns(std::vector<Mover> &movers, const LookupType &lookup)
	{
		for (const Mover &mover : movers)
		{
			const ecs::Entity dock = mover.docking->dock;
			const Dock *layout = lookup.IsAlive(dock) ? lookup.template Get<Dock>(dock) : nullptr;
			if (layout == nullptr || layout->crippled)
				continue;
			bool active = false;
			Mover *first = nullptr;
			for (Mover &other : movers)
			{
				if (other.docking->dock != dock)
					continue;
				active = active || other.docking->granted;
				if (other.docking->queued != 0 && other.docking->slot >= 0 && other.docking->reached &&
					(first == nullptr || other.docking->queued < first->docking->queued))
					first = &other;
			}
			if (!active && first != nullptr)
			{
				first->docking->granted = true;
				first->docking->queued = 0;
			}
		}
	}

	template<typename LookupType>
	static void Run(Mover &mover, std::vector<Mover> &movers, const LookupType &lookup, ecs::SystemContext &context)
	{
		Docking &docking = *mover.docking;
		Status status = docking.entering ? Enter(mover, docking.phase, movers, lookup, context) : Update(mover, lookup, context);
		// The original's state machine: a result moves it on (each new phase's start runs at once); a phase still
		// going takes its first transition that holds. Bounded as the original (checkfortransitionsnum).
		for (int guard = 0; guard < 20 && IsDocking(docking); ++guard)
		{
			if (status == Status::Continue)
			{
				if (docking.phase == DockPhase::WaitForClearance && AbleToAdvance(mover, movers))
				{
					status = Enter(mover, DockPhase::Advance, movers, lookup, context);
					continue;
				}
				return;
			}
			const bool success = status == Status::Success;
			const DockPhase from = docking.phase;
			Leave(mover, from, context);
			const std::optional<DockPhase> next = Next(from, success);
			if (!next)
			{
				End(mover);
				mover.docking->left = from == DockPhase::MoveToExit;
				return;
			}
			status = Enter(mover, *next, movers, lookup, context);
		}
	}

	// Each phase's success and failure phases (none: the docking is over).
	static std::optional<DockPhase> Next(DockPhase phase, bool success) noexcept
	{
		switch (phase)
		{
		case DockPhase::Approach:
		case DockPhase::Advance:
			return success ? std::optional(DockPhase::WaitForClearance) : std::nullopt;
		case DockPhase::WaitForClearance:
			return success ? std::optional(DockPhase::MoveToEntry) : std::nullopt;
		case DockPhase::MoveToEntry:
			return success ? DockPhase::MoveToDock : DockPhase::MoveToExit;
		case DockPhase::MoveToDock:
			return success ? DockPhase::Process : DockPhase::MoveToExit;
		case DockPhase::Process:
			return DockPhase::MoveToExit;
		case DockPhase::MoveToExit:
		case DockPhase::None:
			break;
		}
		// Out: AIDockMoveToRallyState succeeds at once for a dock with no rally point after docking.
		return std::nullopt;
	}

	// AIDockMachine::ableToAdvance (DockUpdate::isClearToAdvance): at its point, and the point ahead is free.
	static bool AbleToAdvance(const Mover &mover, const std::vector<Mover> &movers)
	{
		const Docking &docking = *mover.docking;
		return docking.slot > 0 && docking.reached && !Held(movers, mover, docking.slot - 1);
	}

	// Whether another mover at the same dock holds approach point `slot`.
	static bool Held(const std::vector<Mover> &movers, const Mover &mover, std::int32_t slot)
	{
		for (const Mover &other : movers)
			if (other.entity != mover.entity && other.docking->dock == mover.docking->dock && other.docking->slot == slot)
				return true;
		return false;
	}

	static bool Arrived(const Mover &mover) noexcept { return mover.order->mode == MoveMode::Idle; }

	static void MoveTo(Mover &mover, Engine::Math::FixedVector2 point, bool straight) noexcept
	{
		*mover.order = straight ? MoveStraightTo(point) : MoveToPoint(point);
		if (mover.route != nullptr)
			mover.route->planned = false;
	}

	static void End(Mover &mover) noexcept
	{
		*mover.docking = {};
		mover.order->mode = MoveMode::Idle;
	}

	template<typename LookupType>
	static const Dock *DockOf(const Mover &mover, const LookupType &lookup)
	{
		return lookup.IsAlive(mover.docking->dock) ? lookup.template Get<Dock>(mover.docking->dock) : nullptr;
	}

	template<typename LookupType>
	static Engine::Math::FixedVector2 Approach(Mover &mover, const Dock &dock, const LookupType &lookup, ecs::SystemContext &context)
	{
		const Transform *where = lookup.template Get<Transform>(mover.docking->dock);
		const auto dockPosition = where != nullptr ? where->position : Engine::Math::FixedVector3{};
		const NavigationGrid &grid = context.Read<NavigationGrid>();
		const ClearancePlane *plane = mover.agent != nullptr ? grid.ClearanceFor(mover.agent->surfaces) : nullptr;
		const std::uint8_t radius = mover.agent != nullptr ? mover.agent->radius : 0;
		const auto legal = [&](Engine::Math::FixedVector2 point) {
			if (plane == nullptr)
				return true;
			const std::int32_t x = static_cast<std::int32_t>((point.x / Engine::Math::Fixed::FromInt(PathfindCellSize)).Floor());
			const std::int32_t y = static_cast<std::int32_t>((point.y / Engine::Math::Fixed::FromInt(PathfindCellSize)).Floor());
			return Passable(grid, *plane, x, y, radius);
		};
		// FindPositionOptions: a random start angle.
		auto random = Engine::Math::Stream(context.Read<RandomSeed>().value, {context.Tick(), mover.entity.index, mover.entity.generation, 0xD0C4u});
		const Engine::Math::TurnAngle start{static_cast<std::uint32_t>(Engine::Math::UniformInt(random, 0, 0xFFFFFFFFll))};
		return ApproachPoint(dock, dockPosition, static_cast<std::uint32_t>(mover.docking->slot), mover.transform->position, start, legal);
	}

	// A phase's start (its onEnter).
	template<typename LookupType>
	static Status Enter(Mover &mover, DockPhase phase, const std::vector<Mover> &movers, const LookupType &lookup, ecs::SystemContext &context)
	{
		Docking &docking = *mover.docking;
		docking.phase = phase;
		docking.entering = false;
		const Dock *dock = DockOf(mover, lookup);
		switch (phase)
		{
		case DockPhase::Approach:
		{
			if (dock == nullptr || !dock->open)
				return Status::Failure;
			// DockUpdate::reserveApproachPosition: its own point, else the first free one (a dynamic dock always has one).
			const std::int32_t count = dock->dynamic ? static_cast<std::int32_t>(Dock::MaxDynamicApproaches) : std::min<std::int32_t>(dock->approachCount, Dock::MaxApproaches);
			std::int32_t slot = docking.slot >= 0 && docking.slot < count ? docking.slot : -1;
			for (std::int32_t index = 0; slot < 0 && index < count; ++index)
				if (!Held(movers, mover, index))
					slot = index;
			if (slot < 0)
				return Status::Failure;
			docking.slot = static_cast<std::int8_t>(slot);
			MoveTo(mover, Approach(mover, *dock, lookup, context), false);
			return Status::Continue;
		}
		case DockPhase::WaitForClearance:
			docking.since = context.Tick();
			return Status::Continue;
		case DockPhase::Advance:
		{
			if (dock == nullptr || !dock->open || docking.slot <= 0 || Held(movers, mover, docking.slot - 1))
				return Status::Failure;
			--docking.slot;
			docking.reached = false;
			MoveTo(mover, Approach(mover, *dock, lookup, context), false);
			return Status::Continue;
		}
		case DockPhase::MoveToEntry:
		{
			if (dock == nullptr || !dock->open)
				return Status::Failure;
			const Transform *where = lookup.template Get<Transform>(docking.dock);
			const Targetable *body = lookup.template Get<Targetable>(docking.dock);
			MoveTo(mover,
				EntryPoint(*dock, where != nullptr ? where->position.XY() : Engine::Math::FixedVector2{}, body != nullptr ? body->radius : Engine::Math::Fixed{},
					mover.transform->position.XY(), mover.radius),
				dock->passthrough);
			return Status::Continue;
		}
		case DockPhase::MoveToDock:
			if (dock == nullptr || !dock->open)
				return Status::Failure;
			MoveTo(mover, ActionPoint(*dock, mover.transform->position.XY()), dock->passthrough);
			return Status::Continue;
		case DockPhase::Process:
			if (dock == nullptr)
				return Status::Failure;
			docking.nextAction = context.Tick() + docking.actionDelay;
			docking.finished = false;
			return Status::Continue;
		case DockPhase::MoveToExit:
			if (dock == nullptr)
				return Status::Failure;
			MoveTo(mover, ExitPoint(*dock, mover.transform->position.XY()), dock->passthrough);
			return Status::Continue;
		case DockPhase::None:
			break;
		}
		return Status::Failure;
	}

	// A phase's tick (its update).
	template<typename LookupType>
	static Status Update(Mover &mover, const LookupType &lookup, ecs::SystemContext &context)
	{
		Docking &docking = *mover.docking;
		const Dock *dock = DockOf(mover, lookup);
		if (dock == nullptr)
			return Status::Failure;
		switch (docking.phase)
		{
		case DockPhase::Approach:
		case DockPhase::Advance:
		case DockPhase::MoveToEntry:
		case DockPhase::MoveToExit:
			return Arrived(mover) ? Status::Success : Status::Continue;
		case DockPhase::MoveToDock:
			if (!dock->open)
				return Status::Failure;
			return Arrived(mover) ? Status::Success : Status::Continue;
		case DockPhase::WaitForClearance:
			if (!dock->open)
				return Status::Failure;
			if (docking.granted)
				return Status::Success;
			return docking.since + DockClearanceTimeoutTicks < context.Tick() ? Status::Failure : Status::Continue;
		case DockPhase::Process:
			return docking.finished || !dock->open ? Status::Success : Status::Continue;
		case DockPhase::None:
			break;
		}
		return Status::Failure;
	}

	// A phase's end (its onExit): the dock's books on the mover.
	static void Leave(Mover &mover, DockPhase phase, ecs::SystemContext &context)
	{
		Docking &docking = *mover.docking;
		switch (phase)
		{
		case DockPhase::Approach:
		case DockPhase::Advance:
			// DockUpdate::onApproachReached: at its point, and in the ready queue (once) unless it has the turn.
			docking.reached = true;
			if (!docking.granted && docking.queued == 0)
				docking.queued = context.Tick() + 1;
			break;
		case DockPhase::MoveToEntry:
			// DockUpdate::onEnterReached: inside; its approach point is free again.
			docking.slot = -1;
			docking.reached = false;
			break;
		case DockPhase::MoveToExit:
			// DockUpdate::onExitReached: the dock's turn is over.
			docking.granted = false;
			break;
		default:
			break;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::DockSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.docking";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
