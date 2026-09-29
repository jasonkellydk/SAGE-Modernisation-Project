export module games.generalszh.gameplay.containment.algorithms.player_evacuation;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.teams.algorithms.reinforcements;
import games.generalszh.gameplay.scripts.algorithms.unit_script_orders;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.targetable;

// AIGroup::groupEvacuate (GameLogic::onEvacuate, a player's EVACUATE button: MSG_EVACUATE), for one of the group: one
// with an AI lets its riders out (aiEvacuate(FALSE)), an AIRCRAFT in the air first coming down where it is
// (aiMoveToAndEvacuate at its own spot: a Chinook lands to let them out); a STRUCTURE without one orders them all out
// (orderAllPassengersToExit).
export namespace generalszh::gameplay
{
inline void GroupEvacuate(GameWorld &game, ecs::Entity unit)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *ref = world.IsAlive(unit) ? world.Get<gp::DefinitionRef>(unit) : nullptr;
	if (ref == nullptr)
		return;
	const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
	if (HasAi(game, unit))
	{
		const auto *targetable = world.Get<gp::Targetable>(unit);
		const bool airborne = targetable != nullptr && (targetable->classes & gp::target_class::AirborneVehicle) != 0;
		if (kind.Is("AIRCRAFT") && airborne)
		{
			reinforcement_detail::MoveToAndEvacuate(game, unit, world.Get<gp::Transform>(unit)->position.XY(), false, reinforcement_detail::HasModule(kind, "ChinookAIUpdate"));
			return;
		}
		Evacuate(game, unit);
		return;
	}
	if (kind.Is("STRUCTURE"))
		ExitSpecificBuilding(game, unit);
}
}
