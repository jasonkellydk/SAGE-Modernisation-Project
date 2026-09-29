export module games.generalszh.gameplay.ai.algorithms.attack_squads;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.ai.components.attack_squad;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.scripts.algorithms.command_button_targets;
import engine.gameplay.common.weapons.components.armament;
import engine.ecs.query.query;

// Attacking teams outside AttackSquadSystem:
// - aiAttackTeam (NAMED_ATTACK_TEAM: the unit; TEAM_ATTACK_TEAM: the attacking team as an AIGroup, each member): the
//   unit's AI state starts over attacking a squad made of the victim team's members now (squadFromTeam), CMD_FROM_SCRIPT.
// - After the step, a unit whose squad has nobody left idles (the state succeeds into AI_IDLE); squads nobody attacks
//   any more are let go.
export namespace generalszh::gameplay
{
inline void AttackTeam(GameWorld &game, std::span<const ecs::Entity> attackers, std::uint32_t victims, bool fromPlayer)
{
	auto *squads = game.world.FindResource<AttackSquads>();
	if (squads == nullptr || victims >= game.roster.TeamCount())
		return;
	std::optional<std::uint32_t> squad;
	for (const ecs::Entity unit : attackers)
	{
		if (!game.world.IsAlive(unit) || !HasAi(game, unit) || !game.world.Has<engine::gameplay::AttackTarget>(unit))
			continue;
		AiIdle(game, unit);
		Commanded(game, unit);
		if (!squad)
			squad = squads->Add(game.roster.TeamAt(victims).members);
		if (!game.world.Has<AttackSquad>(unit))
			game.world.Add<AttackSquad>(unit);
		*game.world.Get<AttackSquad>(unit) = AttackSquad{*squad, static_cast<std::uint8_t>(fromPlayer ? 1 : 0)};
	}
}

inline void NamedAttackTeam(GameWorld &game, ecs::Entity unit, const std::string &team)
{
	if (const auto victims = ResolveTeam(game, team))
		AttackTeam(game, std::span(&unit, 1), *victims, false);
}

inline void TeamAttackTeam(GameWorld &game, const std::string &attackers, const std::string &team)
{
	const auto index = ResolveTeam(game, attackers);
	const auto victims = ResolveTeam(game, team);
	if (!index || !victims)
		return;
	const std::vector<ecs::Entity> group = button_target_detail::GroupOf(game, *index);
	AttackTeam(game, group, *victims, false);
}

inline void ApplySquadsDone(GameWorld &game)
{
	auto *squads = game.world.FindResource<AttackSquads>();
	auto *done = game.world.FindResource<SquadsDone>();
	if (squads == nullptr || done == nullptr)
		return;
	std::vector<ecs::Entity> finished;
	done->AppendTo(finished);
	done->Reset(0);
	for (const ecs::Entity unit : finished)
		if (game.world.IsAlive(unit) && game.world.Has<AttackSquad>(unit))
		{
			game.world.Remove<AttackSquad>(unit);
			AiIdle(game, unit);
		}
	if (finished.empty() || squads->Size() == 0)
		return;
	std::vector<bool> used(squads->Size(), false);
	ecs::Query<ecs::Read<AttackSquad>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		for (const AttackSquad &order : chunk.template Get<AttackSquad>())
			if (order.squad < used.size())
				used[order.squad] = true;
	});
	squads->Release(used);
}
}
