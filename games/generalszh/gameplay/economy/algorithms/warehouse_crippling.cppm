export module games.generalszh.gameplay.economy.algorithms.warehouse_crippling;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.economy.systems.warehouse_crippling_system;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.rts.docking.components.docking;
import engine.gameplay.rts.harvesting.components.harvester;
import engine.gameplay.rts.movement.components.locomotion;
import engine.ecs.query.query;

// SupplyWarehouseDockUpdate::setDockCrippled(TRUE), once the systems have run: the docker it had let in (its active
// docker) is killed when inside (between entering and leaving) unless it moves on an airborne locomotor; one still on
// its way in is lucky: its AI idles, and a supply truck is told it wants to gather again (setForceWantingState).
export namespace generalszh::gameplay
{
inline void ApplyWarehouseCrippling(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto *events = game.world.FindResource<WarehouseCripplingEvents>();
	if (events == nullptr || events->list.empty())
		return;
	const std::vector<WarehouseCripplingEvent> crippled = std::move(events->list);
	events->list.clear();
	for (const WarehouseCripplingEvent &event : crippled)
	{
		std::vector<std::pair<ecs::Entity, bool>> active; // (docker, inside)
		ecs::Query<ecs::Read<gp::Docking>> dockers(game.world);
		dockers.ForEachChunk([&](auto chunk) {
			const auto rows = chunk.template Get<gp::Docking>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < rows.size(); ++row)
				if (rows[row].dock == event.warehouse && rows[row].granted)
				{
					const gp::DockPhase phase = rows[row].phase;
					active.emplace_back(entities[row], phase == gp::DockPhase::MoveToDock || phase == gp::DockPhase::Process || phase == gp::DockPhase::MoveToExit);
				}
		});
		for (const auto &[docker, inside] : active)
		{
			if (!game.world.IsAlive(docker))
				continue;
			if (inside)
			{
				const auto *motion = game.world.Get<gp::Locomotion>(docker);
				if (motion == nullptr || (motion->locomotor.surfaces & 8u) == 0)
					KillNow(game, docker);
				continue;
			}
			AiIdle(game, docker);
			if (auto *harvester = game.world.Get<gp::Harvester>(docker))
				harvester->forceWanting = true;
		}
	}
}
}
