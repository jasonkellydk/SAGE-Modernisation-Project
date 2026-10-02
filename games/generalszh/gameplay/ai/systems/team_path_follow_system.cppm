export module games.generalszh.gameplay.ai.systems.team_path_follow_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.ai.components.team_path_follow;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.rts.combat.components.attack_move;

// AIFollowWaypointPathState::update for every unit following a path as a team, chunk-parallel, after the tick's movement:
// its team moved on to another waypoint (MovedOn); another order took its movement over (Replaced); or its move to its
// goal ended, reached or given up (Arrived; a skirmish computer player's member also once its team's centre, the members
// with an AI not held, is within SkirmishGroupFudgeDistance times the team's group count of its goal). What follows is
// applied in order after the systems (ApplyTeamPathFollows: the team's waypoint and the random next one are shared).
// Attack-following while it fights (AIAttackFollowWaypointPathState: its attack machine not idle), none of that is looked
// at: the fight over, it goes back to its leg and goes on from there.
export namespace generalszh::gameplay
{
struct TeamPathFollowSystem
{
	using Query = ecs::Query<ecs::Read<TeamPathFollow>, ecs::Read<engine::gameplay::MoveOrder>, ecs::Read<engine::gameplay::TeamMember>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::MoveOrder>, ecs::Read<engine::gameplay::Locomotion>,
		ecs::Read<engine::gameplay::Disabled>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::AttackMove>>;
	using Resources = ecs::Resources<ecs::Read<TeamWaypoints>, ecs::Read<engine::gameplay::TeamRoster>, ecs::Read<ObjectTemplates>, ecs::Write<TeamPathEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<TeamPathEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using Engine::Math::Fixed;
		const TeamWaypoints &waypoints = context.Read<TeamWaypoints>();
		const gp::TeamRoster &roster = context.Read<gp::TeamRoster>();
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const Fixed fudge = templates.Content().aiData.skirmishGroupFudgeDistance;
		const auto lookup = context.Lookup<Lookup>();
		auto &events = context.Write<TeamPathEvents>().Slot(context);
		const auto follows = chunk.Get<TeamPathFollow>();
		const auto orders = chunk.Get<gp::MoveOrder>();
		const auto members = chunk.Get<gp::TeamMember>();
		const auto entities = chunk.Entities();
		const auto hasAi = [&](ecs::Entity unit) { return lookup.Get<gp::MoveOrder>(unit) != nullptr || lookup.Get<gp::Locomotion>(unit) != nullptr; };
		const auto held = [&](ecs::Entity unit) {
			const gp::Disabled *disabled = lookup.Get<gp::Disabled>(unit);
			return disabled != nullptr && (disabled->mask & gp::disabled_type::Held) != 0;
		};
		for (std::size_t row = 0; row < follows.size(); ++row)
		{
			const TeamPathFollow &follow = follows[row];
			const gp::MoveOrder &order = orders[row];
			const std::uint32_t team = members[row].team;
			if (const gp::AttackMove *attacking = lookup.Get<gp::AttackMove>(entities[row]); attacking != nullptr && attacking->engaged != 0)
				continue;
			if (waypoints.Of(team) != follow.waypoint)
			{
				events.push_back({entities[row], TeamPathEventKind::MovedOn});
				continue;
			}
			if (order.mode != gp::MoveMode::Idle && (order.mode != gp::MoveMode::Point || order.destination != follow.goal))
			{
				events.push_back({entities[row], TeamPathEventKind::Replaced});
				continue;
			}
			if (order.mode == gp::MoveMode::Idle)
			{
				events.push_back({entities[row], TeamPathEventKind::Arrived});
				continue;
			}
			if (follow.skirmish == 0 || team >= roster.TeamCount())
				continue;
			// The team as an AI group (getTeamAsAIGroup): its count, and the centre of its members with an AI not held
			// (none such: of the others).
			std::int64_t count = 0;
			std::array<Engine::Math::FixedVector2, 2> totals{};
			std::array<std::int64_t, 2> counts{};
			for (const ecs::Entity unit : roster.TeamAt(team).members)
			{
				if (!lookup.IsAlive(unit))
					continue;
				const bool ai = hasAi(unit);
				if (!ai)
				{
					const gp::DefinitionRef *ref = lookup.Get<gp::DefinitionRef>(unit);
					if (ref == nullptr)
						continue;
					const content::ObjectDefinition &kind = templates.DefinitionAt(ref->index);
					if (!kind.Is("STRUCTURE") && !kind.Is("ALWAYS_SELECTABLE"))
						continue;
				}
				++count;
				const gp::Transform *at = lookup.Get<gp::Transform>(unit);
				if (at == nullptr || held(unit))
					continue;
				const std::size_t which = ai ? 0 : 1;
				totals[which] = totals[which] + at->position.XY();
				++counts[which];
			}
			const std::size_t use = counts[0] > 0 ? 0 : 1;
			if (counts[use] == 0)
				continue;
			const Engine::Math::FixedVector2 center{totals[use].x / Fixed::FromInt(counts[use]), totals[use].y / Fixed::FromInt(counts[use])};
			const Fixed reach = Fixed::FromInt(count) * fudge;
			if (Engine::Math::DistanceSquared(center, follow.goal) <= reach * reach)
				events.push_back({entities[row], TeamPathEventKind::Arrived});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::TeamPathFollowSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.team_path_follow";
	// After the tick's simulation: its movement done (the original's AIInternalMoveToState::update within the state).
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
