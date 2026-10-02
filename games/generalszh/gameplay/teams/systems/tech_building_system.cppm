export module games.generalszh.gameplay.teams.systems.tech_building_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.teams.components.tech_building;
export import engine.gameplay.rts.lifecycle.resources.casualties;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.components.owner;

// TechBuildingBehavior::onDie, among the tick's deaths: a tech building killed goes to the neutral player's default team
// (setTeam), so no player keeps any bonus from it, and its remains wear no one's colours. (Its CAPTURED look clears with
// it: the appearance shows CAPTURED only while a playable side holds it.)
export namespace generalszh::gameplay
{
struct TechBuildingSystem
{
	using Query = ecs::Query<ecs::Read<TechBuilding>>;
	using Lookup = ecs::Lookup<ecs::Read<TechBuilding>, ecs::Read<engine::gameplay::TeamMember>, ecs::Read<engine::gameplay::Owner>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::Casualties>, ecs::Read<engine::gameplay::TeamRoster>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const gp::TeamRoster &roster = context.Read<gp::TeamRoster>();
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		const std::optional<std::uint32_t> neutral = roster.FindTeam("team");
		if (!neutral)
			return;
		for (const gp::Casualty &death : context.Read<gp::Casualties>().list)
		{
			if (death.departure != gp::Departure::Killed || lookup.template Get<TechBuilding>(death.entity) == nullptr)
				continue;
			if (lookup.template Get<gp::TeamMember>(death.entity) != nullptr)
				commands.Set<gp::TeamMember>(death.entity, gp::TeamMember{*neutral});
			if (lookup.template Get<gp::Owner>(death.entity) != nullptr)
				commands.Set<gp::Owner>(death.entity, gp::Owner{roster.TeamAt(*neutral).owner});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::TechBuildingSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.tech_building";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	// The composition orders it after the tick's deaths.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
