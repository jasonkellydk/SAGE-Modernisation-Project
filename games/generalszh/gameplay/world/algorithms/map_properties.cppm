export module games.generalszh.gameplay.world.algorithms.map_properties;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import engine.level.model.level;
import games.generalszh.gameplay.veterancy.algorithms.veterancy_placement;
import games.generalszh.gameplay.scripts.algorithms.object_edits;
import engine.gameplay.rts.combat.components.aggression;

// Object::updateObjValuesFromMapProperties, past its name (given as the object is made): its veterancy, its panel flags
// (objectRecruitableAI / objectSelectable / objectEnabled / objectPowered / objectIndestructible / objectUnsellable /
// objectTargetable, in its order) and its AI's mood (objectAggressiveness: setAttitude).
export namespace generalszh::gameplay
{
inline void ApplyMapProperties(GameWorld &game, ecs::Entity entity, const engine::level::Properties &properties)
{
	if (!game.world.IsAlive(entity))
		return;
	if (const auto level = properties.Get<std::int64_t>("objectVeterancy"))
		PlaceAtVeterancy(game, entity, *level);
	for (const auto &[key, flag] : {std::pair{"objectRecruitableAI", "AI Recruitable"}, std::pair{"objectSelectable", "Selectable"},
			 std::pair{"objectEnabled", "Enabled"}, std::pair{"objectPowered", "Powered"}, std::pair{"objectIndestructible", "Indestructible"},
			 std::pair{"objectUnsellable", "Unsellable"}, std::pair{"objectTargetable", "Player Targetable"}})
		if (const auto value = properties.Get<bool>(key))
			SetObjectPanelFlag(game, entity, flag, *value);
	if (const auto mood = properties.Get<std::int64_t>("objectAggressiveness"))
		if (auto *aggression = game.world.Get<engine::gameplay::Aggression>(entity))
			aggression->attitude = static_cast<std::int8_t>(*mood);
}
}
