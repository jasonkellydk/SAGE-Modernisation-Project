export module engine.gameplay.rts.harvesting.systems.harvest_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.harvesting.components.harvester;
export import engine.gameplay.rts.harvesting.components.resource_store;
export import engine.gameplay.rts.harvesting.resources.harvest_catalog;
export import engine.gameplay.rts.harvesting.resources.harvest_roster;
export import engine.gameplay.rts.docking.components.docking;
export import engine.gameplay.rts.docking.algorithms.dock_points;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.rts.navigation.algorithms.clearance;
export import engine.gameplay.rts.economy.resources.player_money;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.common.lifetime.components.lifetime;
import Engine.Core.Math.FixedRandom;

// Harvesters' rounds, once a tick before docking (a batch, in entity order:
// they share the stores' boxes and approach points). Per harvester, as the
// original's SupplyTruckAIUpdate::update: first its round (the state's
// update, or its start when just entered, then the state's transitions and
// its success and failure states, as the original's state machine), then
// its business at a dock (AIDockProcessDockState: every actionDelay ticks,
// a store hands it one box while it has room; a depot takes all it carries
// at once and pays the depot's player; no more: it leaves).
//   Finding a dock (ResourceGatheringManager): the dock its player sent it
//   to while that still takes it; else, carrying, the nearest of its
//   player's depots; empty, the nearest store not an enemy's with boxes
//   left, within its scan distance (doubled for a computer player). A dock
//   must have an approach point free for it (a dynamic one always does).
//   Nearest: centre to centre in three dimensions; ties to the first found.
//   Home (Player::findClosestByKindOf): the nearest of its player's homes of
//   the most preferred rank there is, centre to centre on the ground.
// A store's boxes change through the tick's commands (only its active
// docker takes from it); the roster copy is kept current for the harvesters
// after it this tick.
export import engine.gameplay.rts.upgrades.resources.player_upgrades;

export import engine.gameplay.rts.containment.components.transport;
export namespace engine::gameplay
{
struct HarvestSystem
{
	using Query = ecs::Query<ecs::Write<Harvester>, ecs::Write<Docking>, ecs::Write<MoveOrder>, ecs::Write<Transform>, ecs::Read<Owner>,
		ecs::Optional<Targetable>, ecs::Optional<NavigationAgent>, ecs::OptionalWrite<Route>, ecs::Optional<Transport>>;
	using Resources = ecs::Resources<ecs::Read<HarvestRoster>, ecs::Read<HarvestCatalog>, ecs::Read<Relationships>, ecs::Read<NavigationGrid>,
		ecs::Read<RandomSeed>, ecs::Read<PlayerUpgrades>, ecs::Write<PlayerMoney>, ecs::Write<HarvestEvents>>;

	enum class Status : std::uint8_t
	{
		Continue,
		Success,
		Failure,
	};

	// REGROUP_SUCCESS_DISTANCE_SQUARED: close enough to home not to move.
	static constexpr std::int64_t RegroupDistanceSquared = 225;
	static constexpr std::int64_t RegroupSearchRadius = 100;

	struct Truck
	{
		ecs::Entity entity;
		Harvester *harvester;
		Docking *docking;
		MoveOrder *order;
		Transform *transform;
		std::uint32_t player;
		Engine::Math::Fixed radius;
		const NavigationAgent *agent;
		Route *route;
	};

	struct Round
	{
		std::vector<HarvestSite> &sites;
		std::vector<Truck> &trucks;
		ecs::SystemContext &context;
	};

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		context.Write<HarvestEvents>().Reset(1);
		std::vector<Truck> trucks;
		query.ForEachChunk([&](auto chunk) {
			auto harvesters = chunk.template Get<Harvester>();
			auto dockings = chunk.template Get<Docking>();
			auto orders = chunk.template Get<MoveOrder>();
			auto transforms = chunk.template Get<Transform>();
			const auto owners = chunk.template Get<Owner>();
			const auto bodies = chunk.template Get<Targetable>();
			const auto agents = chunk.template Get<NavigationAgent>();
			auto routes = chunk.template Get<Route>();
			const auto transports = chunk.template Get<Transport>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < harvesters.size(); ++row)
			{
				// A harvester that lands on transport duty loses its supplies (ChinookTakeoffOrLandingState::onEnter).
				if (!transports.empty() && transports[row].landing)
					harvesters[row].boxes = 0;
				trucks.push_back({entities[row], &harvesters[row], &dockings[row], &orders[row], &transforms[row], owners[row].player,
					bodies.empty() ? Engine::Math::Fixed{} : bodies[row].radius, agents.empty() ? nullptr : &agents[row], routes.empty() ? nullptr : &routes[row]});
			}
		});
		if (trucks.empty())
			return;
		std::sort(trucks.begin(), trucks.end(), [](const Truck &a, const Truck &b) { return a.entity.index < b.entity.index; });
		std::vector<HarvestSite> sites;
		context.Read<HarvestRoster>().AppendTo(sites);
		std::sort(sites.begin(), sites.end(), [](const HarvestSite &a, const HarvestSite &b) { return a.entity.index < b.entity.index; });
		Round round{sites, trucks, context};
		for (Truck &truck : trucks)
		{
			RunRound(truck, round);
			Business(truck, round);
		}
	}

	// ---- the round (SupplyTruckStateMachine) ----

	static bool Idle(const Truck &truck) noexcept { return !IsDocking(*truck.docking) && truck.order->mode == MoveMode::Idle; }

	static void RunRound(Truck &truck, Round &round)
	{
		Harvester &harvester = *truck.harvester;
		Status status = harvester.entering ? Enter(truck, harvester.state, round) : Update(truck, round);
		for (int guard = 0; guard < 20; ++guard)
		{
			if (status == Status::Continue)
			{
				const std::optional<HarvesterState> to = Transition(truck);
				if (!to)
					return;
				status = Enter(truck, *to, round);
				continue;
			}
			status = Enter(truck, Next(harvester.state, status == Status::Success), round);
		}
	}

	static HarvesterState Next(HarvesterState state, bool success) noexcept
	{
		switch (state)
		{
		case HarvesterState::Wanting:
			return success ? HarvesterState::Busy : HarvesterState::Regrouping;
		case HarvesterState::Regrouping:
			return success ? HarvesterState::Wanting : HarvesterState::Busy;
		default:
			return HarvesterState::Busy;
		}
	}

	// Each state's transitions, the first that holds.
	static std::optional<HarvesterState> Transition(const Truck &truck) noexcept
	{
		const Harvester &harvester = *truck.harvester;
		const bool docking = IsDocking(*truck.docking);
		const bool idle = Idle(truck);
		switch (harvester.state)
		{
		case HarvesterState::Busy:
			if (idle)
				return HarvesterState::Idle;
			if (docking)
				return HarvesterState::Docking;
			break;
		case HarvesterState::Idle:
			if (harvester.forceBusy)
				return HarvesterState::Busy;
			if (harvester.forceWanting)
				return HarvesterState::Wanting;
			if (docking)
				return HarvesterState::Docking;
			if (!idle)
				return HarvesterState::Busy;
			break;
		case HarvesterState::Wanting:
			if (docking)
				return HarvesterState::Docking;
			if (!idle)
				return HarvesterState::Busy;
			break;
		case HarvesterState::Regrouping:
			if (harvester.directed)
				return HarvesterState::Busy;
			break;
		case HarvesterState::Docking:
			if (harvester.forceBusy)
				return HarvesterState::Busy;
			if (idle)
				return HarvesterState::Wanting;
			if (!docking && !idle)
				return HarvesterState::Busy;
			break;
		}
		return std::nullopt;
	}

	static Status Enter(Truck &truck, HarvesterState state, Round &round)
	{
		Harvester &harvester = *truck.harvester;
		harvester.state = state;
		harvester.entering = false;
		switch (state)
		{
		case HarvesterState::Busy:
			harvester.forceBusy = false;
			return Status::Continue;
		case HarvesterState::Wanting:
		case HarvesterState::Docking:
			harvester.forceWanting = false;
			return Status::Continue;
		case HarvesterState::Regrouping:
			return Regroup(truck, round);
		case HarvesterState::Idle:
			break;
		}
		return Status::Continue;
	}

	static Status Update(Truck &truck, Round &round)
	{
		switch (truck.harvester->state)
		{
		case HarvesterState::Wanting:
		{
			const HarvestSite *dock = truck.harvester->boxes > 0 ? BestDepot(truck, round) : BestStore(truck, round);
			if (dock == nullptr)
				return Status::Failure;
			StartDocking(*truck.docking, dock->entity, dock->store ? truck.harvester->storeDelay : truck.harvester->depotDelay);
			truck.harvester->directed = false;
			return Status::Success;
		}
		case HarvesterState::Regrouping:
			return Idle(truck) ? Status::Success : Status::Continue;
		default:
			return Status::Continue;
		}
	}

	// RegroupingState::onEnter: to home, unless already there.
	static Status Regroup(Truck &truck, Round &round)
	{
		using Engine::Math::Fixed;
		const HarvestSite *home = nullptr;
		Fixed nearest;
		for (const HarvestSite &site : round.sites)
		{
			if (site.player != truck.player || site.homeRank == HarvestCatalog::NoHome || site.entity == truck.entity)
				continue;
			const Fixed distance = Engine::Math::DistanceSquared(site.position.XY(), truck.transform->position.XY());
			if (home == nullptr || site.homeRank < home->homeRank || (site.homeRank == home->homeRank && distance < nearest))
			{
				home = &site;
				nearest = distance;
			}
		}
		if (home == nullptr)
			return Status::Failure;
		if (CircleGapSquared(truck.transform->position.XY(), truck.radius, home->position.XY(), home->radius) < Fixed::FromInt(RegroupDistanceSquared))
			return Status::Continue;
		const NavigationGrid &grid = round.context.Read<NavigationGrid>();
		const ClearancePlane *plane = truck.agent != nullptr ? grid.ClearanceFor(truck.agent->surfaces) : nullptr;
		const std::uint8_t cells = truck.agent != nullptr ? truck.agent->radius : 0;
		const auto legal = [&](Engine::Math::FixedVector2 point) {
			if (plane == nullptr)
				return true;
			const std::int32_t x = static_cast<std::int32_t>((point.x / Fixed::FromInt(PathfindCellSize)).Floor());
			const std::int32_t y = static_cast<std::int32_t>((point.y / Fixed::FromInt(PathfindCellSize)).Floor());
			return Passable(grid, *plane, x, y, cells);
		};
		auto random = Engine::Math::Stream(round.context.Read<RandomSeed>().value, {round.context.Tick(), truck.entity.index, truck.entity.generation, 0x4E6Fu});
		const Engine::Math::TurnAngle start{static_cast<std::uint32_t>(Engine::Math::UniformInt(random, 0, 0xFFFFFFFFll))};
		const auto spot = FindPositionAround(home->position.XY(), Fixed{}, Fixed::FromInt(RegroupSearchRadius), start, legal);
		if (!spot)
			return Status::Failure;
		*truck.order = MoveToPoint(*spot);
		if (truck.route != nullptr)
			truck.route->planned = false;
		truck.harvester->directed = false;
		return Status::Continue;
	}

	// ---- finding docks (ResourceGatheringManager) ----

	// ActionManager::canTransferSuppliesAt: a store with boxes, not an enemy's; a depot of its own player's, and
	// something to deliver.
	static bool CanTransfer(const Truck &truck, const HarvestSite &site, const Round &round)
	{
		if (site.store)
			return site.boxes > 0 && !round.context.Read<Relationships>().Enemies(site.player, truck.player);
		if (site.depot)
			return truck.harvester->boxes > 0 && site.player == truck.player;
		return false;
	}

	// DockUpdate::isClearToApproach: a dynamic dock, one it already holds a point at, or one with a point free.
	static bool ClearToApproach(const Truck &truck, const HarvestSite &site, const Round &round)
	{
		if (site.dynamic)
			return true;
		std::uint32_t held = 0;
		for (const Truck &other : round.trucks)
		{
			if (other.docking->dock != site.entity)
				continue;
			const bool holds = other.docking->slot >= 0 || (other.docking->phase == DockPhase::Approach && other.docking->entering);
			if (!holds)
				continue;
			if (other.entity == truck.entity)
				return true;
			++held;
		}
		return held < site.approaches;
	}

	static bool Takes(const Truck &truck, const HarvestSite &site, const Round &round)
	{
		return site.open && CanTransfer(truck, site, round) && ClearToApproach(truck, site, round);
	}

	static const HarvestSite *Preferred(const Truck &truck, const Round &round, bool store)
	{
		for (const HarvestSite &site : round.sites)
			if (site.entity == truck.harvester->preferredDock)
				return (store ? site.store : site.depot) && Takes(truck, site, round) ? &site : nullptr;
		return nullptr;
	}

	static Engine::Math::Fixed ScanDistance(const Truck &truck, const Round &round)
	{
		const HarvestCatalog &catalog = round.context.Read<HarvestCatalog>();
		return catalog.Computer(truck.player) ? truck.harvester->scanDistance * Engine::Math::Fixed::FromInt(catalog.computerScanMultiplier)
											  : truck.harvester->scanDistance;
	}

	static const HarvestSite *BestStore(const Truck &truck, const Round &round, ecs::Entity except = {})
	{
		if (const HarvestSite *preferred = Preferred(truck, round, true))
			return preferred;
		const Engine::Math::Fixed scan = ScanDistance(truck, round);
		const Engine::Math::Fixed limit = scan * scan;
		const HarvestSite *best = nullptr;
		Engine::Math::Fixed bestCost;
		for (const HarvestSite &site : round.sites)
		{
			// findBestSupplyWarehouse: of the listed warehouses (the preferred dock need not be).
			if (!site.store || !site.listed || site.entity == except || !Takes(truck, site, round))
				continue;
			const Engine::Math::Fixed cost = Engine::Math::DistanceSquared(site.position, truck.transform->position);
			if (cost < limit && (best == nullptr || cost < bestCost))
			{
				best = &site;
				bestCost = cost;
			}
		}
		return best;
	}

	static const HarvestSite *BestDepot(const Truck &truck, const Round &round)
	{
		if (const HarvestSite *preferred = Preferred(truck, round, false))
			return preferred;
		const HarvestSite *best = nullptr;
		Engine::Math::Fixed bestCost;
		for (const HarvestSite &site : round.sites)
		{
			if (!site.depot || !Takes(truck, site, round))
				continue;
			const Engine::Math::Fixed cost = Engine::Math::DistanceSquared(site.position, truck.transform->position);
			if (best == nullptr || cost < bestCost)
			{
				best = &site;
				bestCost = cost;
			}
		}
		return best;
	}

	// ---- the business at a dock (AIDockProcessDockState::update, DockUpdate::action) ----

	static void Business(Truck &truck, Round &round)
	{
		Docking &docking = *truck.docking;
		const std::uint64_t tick = round.context.Tick();
		if (!IsDocking(docking) || docking.phase != DockPhase::Process || docking.entering || docking.finished || tick < docking.nextAction)
			return;
		HarvestSite *site = nullptr;
		for (HarvestSite &candidate : round.sites)
			if (candidate.entity == docking.dock)
				site = &candidate;
		// Not a warehouse or supply centre (a repair dock): that dock's business is another system's.
		if (site == nullptr || !(site->store || site->depot))
			return;
		docking.nextAction = tick + docking.actionDelay;
		if (!site->open || !(site->store ? TakeBox(truck, *site, round) : Deliver(truck, *site, round)))
			docking.finished = true;
	}

	// SupplyWarehouseDockUpdate::action: one box while the harvester has room (it must touch the store: one that
	// does not is nudged a little, and fails).
	static bool TakeBox(Truck &truck, HarvestSite &store, Round &round)
	{
		using Engine::Math::Fixed;
		if (store.boxes == 0)
			return false;
		const Fixed reach = truck.radius * 2;
		if (CircleGapSquared(truck.transform->position.XY(), truck.radius, store.position.XY(), store.radius) > reach * reach)
		{
			const Fixed range = Fixed::FromInt(PathfindCellSize) * Fixed::FromRatio(4, 10);
			auto random = Engine::Math::Stream(round.context.Read<RandomSeed>().value, {round.context.Tick(), truck.entity.index, truck.entity.generation, 0xB0C5u});
			truck.transform->position.x += Engine::Math::UniformFixed(random, -range, range);
			truck.transform->position.y += Engine::Math::UniformFixed(random, -range, range);
			return false;
		}
		Harvester &harvester = *truck.harvester;
		if (harvester.boxes >= harvester.maxBoxes)
			return false;
		--store.boxes;
		++harvester.boxes;
		auto &commands = round.context.Commands();
		commands.Set<ResourceStore>(store.entity, ResourceStore{store.boxes, store.startingBoxes, store.deleteWhenEmpty, store.listed});
		if (store.boxes == 0)
		{
			// SupplyTruckAIUpdate::gainOneBox: the supplies-depleted voice, unless another store is near.
			const HarvestSite *next = BestStore(truck, round, store.entity);
			const Fixed quarter = ScanDistance(truck, round) / Fixed::FromInt(4);
			if (next == nullptr || Engine::Math::Distance(truck.transform->position, next->position) > quarter)
				round.context.Write<HarvestEvents>().SlotAt(0).push_back(
					{truck.entity, HarvestEvent::Kind::Depleted, 0, truck.player, truck.transform->position});
			if (store.deleteWhenEmpty)
			{
				store.open = false;
				commands.Add<Lifetime>(store.entity, Lifetime{round.context.Tick(), 1, 0});
				return false;
			}
		}
		return true;
	}

	// SupplyCenterDockUpdate::action: everything it carries, paid to the depot's player, and its upgraded supply boost
	// (getUpgradedSupplyBoostValue: the boost times the boxes it brings over its most); then it leaves.
	static bool Deliver(Truck &truck, HarvestSite &depot, Round &round)
	{
		std::int64_t value = static_cast<std::int64_t>(truck.harvester->boxes) * round.context.Read<HarvestCatalog>().valuePerBox;
		const Harvester &harvester = *truck.harvester;
		if (harvester.boost != 0 && harvester.maxBoxes > 0 && harvester.boostUpgrade != Harvester::NoUpgrade &&
			round.context.Read<PlayerUpgrades>().Completed(truck.player).Has(harvester.boostUpgrade))
			value += harvester.boost * static_cast<std::int64_t>(harvester.boxes) / static_cast<std::int64_t>(harvester.maxBoxes);
		truck.harvester->boxes = 0;
		if (value > 0)
		{
			round.context.Write<PlayerMoney>().Earn(depot.player, value);
			round.context.Write<HarvestEvents>().SlotAt(0).push_back({truck.entity, HarvestEvent::Kind::Delivered, value, depot.player, truck.transform->position, depot.entity});
		}
		return false;
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::HarvestSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.harvesting";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
