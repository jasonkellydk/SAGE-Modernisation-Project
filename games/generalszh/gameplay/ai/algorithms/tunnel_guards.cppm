export module games.generalszh.gameplay.ai.algorithms.tunnel_guards;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.ai.components.tunnel_guard;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.gameplay.combat.algorithms.common_targets;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.status.components.ai_activity;

// Guarding a tunnel network, outside its system (TunnelGuardSystem steps the guards):
// - doTeamGuardInTunnelNetwork (TEAM_GUARD_IN_TUNNEL_NETWORK): each member with an AI, mobile, guards (aiGuardTunnelNetwork,
//   GUARDMODE_NORMAL, CMD_FROM_SCRIPT): its AI state starts over at the machine's Return; not idle, it picks no targets
//   of its own (the guard's attacks are its only ones).
// - TunnelTracker::updateNemesis(target): with no current nemesis (none, not seen for 4 seconds, or gone) a VEHICLE,
//   STRUCTURE, INFANTRY or AIRCRAFT becomes it, seen now; the current one seen again is seen now.
// - TunnelContain::update, after the step: each tunnel of a player hit within the last second by an enemy makes its
//   attacker the network's nemesis.
// - The tick's guard events, in order: boarding a tunnel (AIEnterState), leaving the network at a tunnel at once
//   (exitObjectInAHurry: out where it stands), the nemesis, the team's victim set or cleared.
export namespace generalszh::gameplay
{
namespace tunnel_guard_detail
{
namespace gp = engine::gameplay;

inline bool Kind(const GameWorld &game, ecs::Entity entity, std::initializer_list<std::string_view> kinds)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gp::DefinitionRef>(entity) : nullptr;
	if (ref == nullptr)
		return false;
	const content::ObjectDefinition &definition = game.templates.DefinitionAt(ref->index);
	return std::ranges::any_of(kinds, [&](std::string_view kind) { return definition.Is(std::string(kind)); });
}

// TunnelTracker::getCurNemesis: seen within 4 seconds and still there (not dead, not hidden); else none (and cleared).
inline ecs::Entity CurrentNemesis(GameWorld &game, std::uint32_t player)
{
	if (player >= game.roster.PlayerCount())
		return {};
	auto &owner = game.roster.PlayerAt(player);
	if (owner.tunnelNemesis == ecs::Entity{})
		return {};
	if (owner.nemesisTick + 4 * game.step.TicksPerSecond() < game.tick || !CommonTargetValid(game, owner.tunnelNemesis))
		owner.tunnelNemesis = {};
	return owner.tunnelNemesis;
}
}

inline void UpdateNemesis(GameWorld &game, std::uint32_t player, ecs::Entity target)
{
	using namespace tunnel_guard_detail;
	if (player >= game.roster.PlayerCount())
		return;
	auto &owner = game.roster.PlayerAt(player);
	const ecs::Entity current = CurrentNemesis(game, player);
	if (current == ecs::Entity{})
	{
		if (target != ecs::Entity{} && Kind(game, target, {"VEHICLE", "STRUCTURE", "INFANTRY", "AIRCRAFT"}))
		{
			owner.tunnelNemesis = target;
			owner.nemesisTick = game.tick;
		}
	}
	else if (current == target)
		owner.nemesisTick = game.tick;
}

inline void TeamGuardInTunnelNetwork(GameWorld &game, const std::string &team)
{
	namespace gp = engine::gameplay;
	ForTeam(game, team, [&](ecs::Entity unit) {
		if (!game.world.IsAlive(unit) || !HasAi(game, unit) || !game.world.Has<gp::MoveOrder>(unit) || tunnel_guard_detail::Kind(game, unit, {"PROJECTILE"}))
			return;
		AiIdle(game, unit);
		Commanded(game, unit);
		game.world.Add<TunnelGuard>(unit);
		// In its guard state its AI is not idle: it picks no targets of its own (no AIIdleState mood look).
		if (auto *activity = game.world.Get<gp::AiActivity>(unit))
			activity->busy = 1;
	});
}

inline void ApplyTunnelGuards(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using namespace tunnel_guard_detail;
	auto &world = game.world;
	const auto *relationships = world.FindResource<gp::Relationships>();
	// TunnelContain::update: a tunnel hit within the last second by an enemy.
	for (const std::uint32_t network : game.manifest.Networks())
		for (const ecs::Entity tunnel : game.manifest.Network(network))
		{
			const auto *body = world.IsAlive(tunnel) ? world.Get<gp::Health>(tunnel) : nullptr;
			const auto *owner = world.IsAlive(tunnel) ? world.Get<gp::Owner>(tunnel) : nullptr;
			if (body == nullptr || owner == nullptr || body->lastDamageTick == 0 || body->lastDamageTick + game.step.TicksPerSecond() <= game.tick)
				continue;
			const auto *attacker = world.IsAlive(body->lastAttacker) ? world.Get<gp::Owner>(body->lastAttacker) : nullptr;
			if (attacker != nullptr && relationships != nullptr && relationships->Enemies(owner->player, attacker->player))
				UpdateNemesis(game, owner->player, body->lastAttacker);
		}
	auto *resource = world.FindResource<TunnelGuardEvents>();
	if (resource == nullptr)
		return;
	std::vector<TunnelGuardEvent> events;
	resource->AppendTo(events);
	resource->Reset(0);
	for (const TunnelGuardEvent &event : events)
	{
		if (!world.IsAlive(event.unit))
			continue;
		const auto *member = world.Get<gp::TeamMember>(event.unit);
		const auto *owner = world.Get<gp::Owner>(event.unit);
		switch (event.kind)
		{
		case TunnelGuardEvent::Kind::Board:
			if (world.IsAlive(event.target) && world.Has<gp::Transport>(event.target) && !world.Has<gp::Passenger>(event.unit))
			{
				AiCommanded(game, event.unit);
				if (!world.Has<gp::Boarding>(event.unit))
					world.Add<gp::Boarding>(event.unit);
				*world.Get<gp::Boarding>(event.unit) = {event.target};
			}
			break;
		case TunnelGuardEvent::Kind::Exit:
		{
			auto *seat = world.Get<gp::Passenger>(event.unit);
			const auto *door = world.IsAlive(event.target) ? world.Get<gp::Transform>(event.target) : nullptr;
			if (seat == nullptr || door == nullptr)
				break;
			game.manifest.Take(seat->transport, event.unit);
			world.Remove<gp::Passenger>(event.unit);
			world.Remove<gp::OffMap>(event.unit);
			if (auto *where = world.Get<gp::Transform>(event.unit))
				where->position = door->position;
			break;
		}
		case TunnelGuardEvent::Kind::Nemesis:
			if (owner != nullptr)
				UpdateNemesis(game, owner->player, event.target);
			break;
		case TunnelGuardEvent::Kind::TeamTarget:
			if (member != nullptr)
				SetTeamTarget(game, member->team, event.target);
			break;
		case TunnelGuardEvent::Kind::ClearTeamTarget:
			if (member != nullptr)
				SetTeamTarget(game, member->team, {});
			break;
		}
	}
}
}
