export module engine.gameplay.rts.production.systems.production_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.rts.production.components.production_queue;
export import engine.gameplay.rts.production.components.production_doors;
export import engine.gameplay.rts.production.components.production_exit_gate;
export import engine.gameplay.rts.economy.resources.player_energy;
export import engine.gameplay.common.identity.components.owner;

// Factories build, in parallel per chunk: the front of each queue advances a
// tick's work, less when its player is short of power (as the original:
// down to the settings' least speed; a player with no power at all builds
// at that least speed); when its build time is up and its factory's door
// is open (see ProductionDoors) it is done (reported, per chunk, for the
// game to bring the units out through the factory's exit) and the next one
// starts. Nothing is spent here: units are paid for when queued.
export namespace engine::gameplay
{
struct Produced
{
	ecs::Entity factory;
	std::uint32_t definition{0};
	std::uint32_t team{0};
	std::uint32_t quantity{1};
	ProductionKind kind{ProductionKind::Unit};
};

struct ProductionDone : ecs::ChunkOutputs<Produced>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::ProductionDone>
{
	static constexpr std::string_view StableName = "engine.gameplay.production_done";
};
}

export namespace engine::gameplay
{
struct ProductionSystem
{
	using Query = ecs::Query<ecs::Write<ProductionQueue>, ecs::Optional<Owner>, ecs::OptionalWrite<ProductionDoors>, ecs::OptionalWrite<ProductionExitGate>>;
	using Resources = ecs::Resources<ecs::Read<PlayerEnergy>, ecs::Read<EnergySettings>, ecs::Write<ProductionDone>>;

	// A tick of a factory's doors: open ones stay open a while, then close.
	static void UpdateDoors(ProductionDoors &doors, std::uint64_t tick) noexcept
	{
		for (std::uint32_t index = 0; index < doors.count && index < ProductionDoors::MaxDoors; ++index)
		{
			ProductionDoor &door = doors.doors[index];
			if (door.opening != 0)
			{
				if (tick - door.opening > doors.openTicks)
					door = {0, tick, 0};
			}
			else if (door.open != 0)
			{
				if (tick - door.open > doors.waitTicks)
					door = {0, 0, tick};
			}
			else if (door.closing != 0 && tick - door.closing > doors.closeTicks)
				door = {};
		}
		if (doors.completeTick != 0 && tick - doors.completeTick > doors.completeTicks)
			doors.completeTick = 0;
	}

	// Whether the unit may come out now through the (first) door, opening it
	// (or keeping it open) for it.
	static bool ThroughDoor(ProductionDoors &doors, std::uint64_t tick) noexcept
	{
		if (doors.completeTick == 0)
			doors.completeTick = tick;
		if (doors.count == 0)
			return true;
		ProductionDoor &door = doors.doors[0];
		if (door.opening == 0 && door.open == 0 && door.closing == 0)
			door.opening = tick;
		else if (door.open != 0 || door.closing != 0)
			door = {0, tick, 0}; // held (or opened back up) for this one
		return door.open != 0;
	}

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<ProductionDone>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		auto &done = context.Write<ProductionDone>().Slot(context);
		auto queues = chunk.Get<ProductionQueue>();
		const auto owners = chunk.Get<Owner>();
		auto doorSets = chunk.Get<ProductionDoors>();
		auto gates = chunk.Get<ProductionExitGate>();
		const PlayerEnergy &energy = context.Read<PlayerEnergy>();
		const EnergySettings &settings = context.Read<EnergySettings>();
		const std::uint64_t tick = context.Tick();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < queues.size(); ++row)
		{
			ProductionDoors *doors = doorSets.empty() ? nullptr : &doorSets[row];
			if (doors != nullptr)
				UpdateDoors(*doors, tick);
			ProductionQueue &queue = queues[row];
			if (queue.count == 0)
				continue;
			ProductionEntry &front = queue.entries[0];
			// Research takes its time whatever the power (UpgradeTemplate::calcTimeToBuild) and comes out of no door.
			const bool research = front.kind == ProductionKind::Upgrade;
			front.progress += owners.empty() || research ? Engine::Math::Fixed::One() : settings.ProductionSpeed(energy.SupplyRatio(owners[row].player));
			if (front.progress < Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(front.ticksTotal)))
				continue;
			if (!research && doors != nullptr && !ThroughDoor(*doors, tick))
				continue; // waiting for the door to open
			ProductionExitGate *gate = research || gates.empty() ? nullptr : &gates[row];
			if (gate == nullptr)
			{
				// Any other exit takes them all at once.
				done.push_back({entities[row], front.definition, front.team, front.quantity - front.produced, front.kind});
				queue.PopFront();
				continue;
			}
			// One at a time while the exit is free (reserveDoorForExit), the rest waiting done in the entry.
			while (front.produced < front.quantity && gate->Free(tick))
			{
				done.push_back({entities[row], front.definition, front.team, 1u, front.kind});
				++front.produced;
				gate->Exited(tick);
			}
			if (front.produced >= front.quantity)
				queue.PopFront();
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::ProductionSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.production";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
