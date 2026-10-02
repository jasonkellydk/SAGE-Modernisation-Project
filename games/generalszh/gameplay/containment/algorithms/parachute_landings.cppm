export module games.generalszh.gameplay.containment.algorithms.parachute_landings;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.parachute.resources.parachute_landings;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.producer;
import engine.gameplay.common.identity.components.definition_ref;
import games.generalszh.gameplay.ai.resources.ai_players;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.production.algorithms.team_building;
import games.generalszh.content.production.production_content;

// ParachuteContain::onRemoving for the tick's landed riders, in turn: a skirmish computer player's hunts (aiHunt, as per
// its designer); else, its delivery's building (the rider's producer's producer: a Tech Reinforcement Pad) asking for it
// (DefaultProductionExitUpdate UseSpawnRallyPoint), it goes out by that building's exit and on to its rally point
// (exitObjectViaDoor); else it idles where it landed.
export namespace generalszh::gameplay
{
inline void ApplyParachuteLandings(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	auto *landings = world.FindResource<gp::ParachuteLandings>();
	if (landings == nullptr || landings->riders.empty())
		return;
	const std::vector<ecs::Entity> riders = std::move(landings->riders);
	landings->riders.clear();
	const auto *ais = world.FindResource<AiPlayers>();
	for (const ecs::Entity rider : riders)
	{
		if (!world.IsAlive(rider))
			continue;
		const auto *owner = world.Get<gp::Owner>(rider);
		const bool skirmishAi = owner != nullptr && ais != nullptr &&
			std::ranges::any_of(ais->players, [&](const AiPlayer &ai) { return ai.player == owner->player && ai.skirmish; });
		if (skirmishAi)
		{
			UnitHunt(game, rider);
			continue;
		}
		const auto producerOf = [&](ecs::Entity entity) -> ecs::Entity {
			const auto *producer = world.IsAlive(entity) ? world.Get<gp::Producer>(entity) : nullptr;
			return producer != nullptr ? producer->entity : ecs::Entity{};
		};
		const ecs::Entity building = producerOf(producerOf(rider));
		if (!world.IsAlive(building))
			continue;
		const auto *ref = world.Get<gp::DefinitionRef>(building);
		const auto exit = ref != nullptr ? content::ReadProductionExit(game.templates.DefinitionAt(ref->index)) : std::nullopt;
		if (exit && exit->useSpawnRallyPoint)
			ExitViaProductionDoor(game, building, rider);
	}
}
}
