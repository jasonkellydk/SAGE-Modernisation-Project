export module games.generalszh.gameplay.spawning.algorithms.spawn_completion;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.ecs.query.query;
import engine.ecs.core.world;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.lifetime.components.lifetime;
import engine.gameplay.rts.slaves.components.spawner;
import engine.gameplay.rts.slaves.resources.spawn_requests;
import engine.gameplay.rts.veterancy.components.experience;
import games.generalszh.gameplay.ai.algorithms.ai_team_building;
import games.generalszh.gameplay.ai.resources.ai_players;
import games.generalszh.gameplay.production.algorithms.team_building;
import games.generalszh.gameplay.score.algorithms.scoring;
import games.generalszh.gameplay.veterancy.algorithms.veterancy_placement;

// SpawnBehavior after the tick's systems: the spawns asked for made in order (score keepers and computer players told),
// aggregate spawners sharing veterancy with their spawns, and a spawner that was only its spawns going with the last.
export namespace generalszh::gameplay
{
namespace gp = engine::gameplay;

// Spawns asked for this tick, made in the order asked.
inline void CompleteSpawns(GameWorld &game)
{
	const std::vector<gp::SpawnRequest> requests = game.world.Resource<gp::SpawnRequests>().list;
	for (const gp::SpawnRequest &request : requests)
	{
		const ecs::Entity spawn = SpawnFrom(game, request);
		// SpawnBehavior::createSpawn: Player::onUnitCreated(parent, spawn).
		ScoreUnitCreated(game, spawn);
		if (const auto *owner = game.world.IsAlive(spawn) ? game.world.Get<gp::Owner>(spawn) : nullptr)
			if (AiPlayer *ai = game.world.Resource<AiPlayers>().Of(owner->player))
				OnAiUnitProduced(game, *ai, request.spawner, spawn);
	}
	// computeAggregateStates: a spawner that is its spawns and they share the higher veterancy (a spawn above it
	// raises it; one below it is raised), in the spawns' order.
	{
		std::vector<std::pair<ecs::Entity, gp::Spawner>> aggregates;
		ecs::Query<ecs::Read<gp::Spawner>> spawners(game.world);
		spawners.ForEachChunk([&](auto chunk) {
			const auto rows = chunk.template Get<gp::Spawner>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < rows.size(); ++row)
				if (rows[row].aggregateHealth)
					aggregates.emplace_back(entities[row], rows[row]);
		});
		for (const auto &[entity, spawner] : aggregates)
		{
			const auto *own = game.world.Get<gp::Experience>(entity);
			if (own == nullptr)
				continue;
			for (std::size_t index = 0; index < spawner.spawnedCount; ++index)
			{
				const ecs::Entity spawn = spawner.spawned[index];
				const auto *theirs = game.world.IsAlive(spawn) ? game.world.Get<gp::Experience>(spawn) : nullptr;
				if (theirs == nullptr)
					continue;
				const std::uint8_t level = game.world.Get<gp::Experience>(entity)->level;
				if (theirs->level > level)
					PlaceAtVeterancy(game, entity, theirs->level, true);
				else if (theirs->level < level)
					PlaceAtVeterancy(game, spawn, level, true);
			}
		}
	}
	// A spawner that was only its spawns goes with the last of them (destroyObject: gone, not killed).
	for (const ecs::Entity emptied : game.world.Resource<gp::SpawnRequests>().emptied)
		if (game.world.IsAlive(emptied))
		{
			if (!game.world.Has<gp::Lifetime>(emptied))
				game.world.Add<gp::Lifetime>(emptied);
			*game.world.Get<gp::Lifetime>(emptied) = gp::Lifetime{game.tick, 1, 0};
		}
}
}
