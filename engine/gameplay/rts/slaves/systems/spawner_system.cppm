export module engine.gameplay.rts.slaves.systems.spawner_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.slaves.components.spawner;
export import engine.gameplay.rts.slaves.resources.spawn_requests;
export import engine.gameplay.rts.lifecycle.resources.kill_requests;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.rts.construction.components.sale;
export import engine.gameplay.rts.construction.components.construction_progress;

// SpawnBehavior::update, onSpawnDeath and onDie, once a tick in entity order:
// a spawn that died or is gone leaves a slot to fill SpawnReplaceDelay ticks
// from now; every SPAWN_UPDATE_RATE (half a second) an active spawner asks for
// a spawn for each slot whose time has passed (its templates in turn), unless
// it is being sold, is not yet built, or is the neutral player's (the one
// without a name: shouldTryToSpawn); one
// making a single batch stops once it is made. A dying spawner whose spawns
// require it kills them. A spawner whose health is its spawns' (AggregateHealth:
// computeAggregateStates) has their health over a whole set of them at their
// average most, and goes once the last of them is lost (onSpawnDeath).
export import engine.gameplay.rts.production.components.production_exit_gate;
export import engine.gameplay.rts.slaves.components.spawn_points;
export namespace engine::gameplay
{
inline constexpr std::uint64_t SpawnUpdateTicks = 15;

struct SpawnerSystem
{
	using Query = ecs::Query<ecs::Write<Spawner>, ecs::OptionalWrite<ProductionExitGate>, ecs::OptionalWrite<SpawnPoints>>;
	using Lookup = ecs::Lookup<ecs::Read<Dying>, ecs::Read<Health>, ecs::Read<Owner>, ecs::Read<Sale>, ecs::Read<ConstructionProgress>>;
	using Resources = ecs::Resources<ecs::Write<SpawnRequests>, ecs::Write<KillRequests>, ecs::Read<TeamRoster>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		auto &requests = context.Write<SpawnRequests>().list;
		auto &kills = context.Write<KillRequests>().entities;
		auto &emptied = context.Write<SpawnRequests>().emptied;
		requests.clear();
		emptied.clear();
		auto &commands = context.Commands();
		const std::uint64_t now = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto spawners = chunk.template Get<Spawner>();
			auto gates = chunk.template Get<ProductionExitGate>();
			auto points = chunk.template Get<SpawnPoints>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < spawners.size(); ++row)
			{
				Spawner &spawner = spawners[row];
				if (lookup.Get<Dying>(entities[row]) != nullptr)
				{
					if (spawner.requireSpawner)
						for (std::size_t index = 0; index < spawner.spawnedCount; ++index)
							if (lookup.IsAlive(spawner.spawned[index]) && lookup.Get<Dying>(spawner.spawned[index]) == nullptr)
								kills.push_back(spawner.spawned[index]);
					spawner.spawnedCount = 0;
					spawner.active = false;
					continue;
				}
				// Lost spawns: replaced after the delay.
				const bool hadSpawns = spawner.spawnedCount > 0;
				for (std::size_t index = 0; index < spawner.spawnedCount;)
				{
					const ecs::Entity spawn = spawner.spawned[index];
					if (lookup.IsAlive(spawn) && lookup.Get<Dying>(spawn) == nullptr)
					{
						++index;
						continue;
					}
					spawner.spawned[index] = spawner.spawned[--spawner.spawnedCount];
					if (spawner.dueCount < Spawner::MaxSpawns)
						spawner.due[spawner.dueCount++] = now + spawner.replaceDelay;
				}
				if (spawner.aggregateHealth)
				{
					if (hadSpawns && spawner.spawnedCount == 0)
					{
						emptied.push_back(entities[row]);
						spawner.active = false;
						continue;
					}
					if (const Health *own = lookup.Get<Health>(entities[row]); own != nullptr && spawner.spawnedCount > 0)
					{
						Engine::Math::Fixed health, most;
						for (std::size_t index = 0; index < spawner.spawnedCount; ++index)
							if (const Health *body = lookup.Get<Health>(spawner.spawned[index]))
							{
								health += body->current;
								most += body->maximum;
							}
						// setInitialHealth(100 * health / (average most * SpawnNumber)).
						const Engine::Math::Fixed whole = most / Engine::Math::Fixed::FromInt(spawner.spawnedCount) * Engine::Math::Fixed::FromInt(std::max<int>(spawner.number, 1));
						if (whole > Engine::Math::Fixed{})
						{
							Health changed = *own;
							changed.current = own->maximum * health / whole;
							if (changed.current != own->current)
								commands.Set<Health>(entities[row], changed);
						}
					}
				}
				if (now < spawner.nextUpdate)
					continue;
				spawner.nextUpdate = now + SpawnUpdateTicks;
				if (!spawner.active || spawner.templateCount == 0)
					continue;
				// shouldTryToSpawn: not while sold or under construction, nor for the neutral player.
				const ConstructionProgress *built = lookup.Get<ConstructionProgress>(entities[row]);
				if (lookup.Get<Sale>(entities[row]) != nullptr || (built != nullptr && built->percent < Engine::Math::Fixed::FromInt(100)))
					continue;
				const Owner *owner = lookup.Get<Owner>(entities[row]);
				const TeamRoster &roster = context.Read<TeamRoster>();
				if (owner != nullptr && owner->player < roster.PlayerCount() && roster.PlayerAt(owner->player).name.empty())
					continue;
				// SpawnPointProductionExitUpdate: the places free (revalidateOccupiers), each spawn made taking one.
				std::uint32_t freePlaces = 0;
				bool placesChecked = false;
				for (std::size_t index = 0; index < spawner.dueCount;)
				{
					if (now <= spawner.due[index])
					{
						++index;
						continue;
					}
					// createSpawn: its exit must let one out (reserveDoorForExit: QueueProductionExitUpdate's burst and
					// delay); if not, this slot waits for a later update. Out, the exit's delay starts.
					if (!gates.empty())
					{
						if (!gates[row].Free(now))
							break;
						gates[row].Exited(now);
					}
					if (!points.empty())
					{
						if (!placesChecked)
						{
							placesChecked = true;
							SpawnPoints &places = points[row];
							for (std::uint32_t place = 0; place < places.count && place < SpawnPoints::Capacity; ++place)
								if (places.occupier[place] != ecs::Entity{} && !lookup.IsAlive(places.occupier[place]))
									places.occupier[place] = {};
							for (std::uint32_t place = 0; place < places.count && place < SpawnPoints::Capacity; ++place)
								freePlaces += places.occupier[place] == ecs::Entity{} ? 1u : 0u;
						}
						if (freePlaces == 0)
							break;
						--freePlaces;
					}
					requests.push_back({entities[row], spawner.templates[spawner.cursor]});
					spawner.cursor = static_cast<std::uint8_t>((spawner.cursor + 1) % spawner.templateCount);
					spawner.due[index] = spawner.due[--spawner.dueCount];
					if (spawner.oneShotLeft > 0)
						--spawner.oneShotLeft;
				}
				if (spawner.oneShotLeft == 0)
					spawner.active = false;
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::SpawnerSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.spawner";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
