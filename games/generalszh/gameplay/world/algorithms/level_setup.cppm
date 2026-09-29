export module games.generalszh.gameplay.world.algorithms.level_setup;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.objects.algorithms.object_factory;
export import games.generalszh.gameplay.veterancy.algorithms.veterancy_placement;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.rts.combat.components.aggression;
import games.generalszh.gameplay.scripts.algorithms.object_edits;
export import engine.gameplay.common.areas.resources.trigger_areas;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.spatial.resources.deck_surfaces;
import engine.gameplay.common.spatial.components.surface_layer;

// Reading a Zero Hour level into the game: its waypoints (with their path
// labels), its water areas, and its placed objects.
export namespace generalszh::gameplay
{
engine::gameplay::WaypointGraph BuildWaypoints(const engine::level::Level &level)
{
	std::vector<engine::gameplay::WaypointDescription> waypoints;
	for (const auto &marker : level.markers)
	{
		engine::gameplay::WaypointDescription waypoint{marker.id, marker.name, marker.position, marker.links, {}};
		for (const char *key : {"waypointPathLabel1", "waypointPathLabel2", "waypointPathLabel3"})
			if (const auto label = marker.properties.Get<std::string>(key))
				waypoint.labels.push_back(*label);
		waypoints.push_back(std::move(waypoint));
	}
	return engine::gameplay::WaypointGraph(waypoints);
}

void AddWater(const engine::level::Level &level, engine::gameplay::GroundHeight &ground)
{
	for (const auto &region : level.regions)
		if (region.water && !region.points.empty())
		{
			std::vector<Engine::Math::FixedVector2> outline;
			for (const auto &point : region.points)
				outline.push_back(point.XY());
			ground.AddWater(std::move(outline), region.points.front().z);
		}
}

// The level's polygon areas (PolygonTrigger: every one, water and rivers too), in whole map units (toward zero).
engine::gameplay::TriggerAreas ReadTriggerAreas(const engine::level::Level &level)
{
	engine::gameplay::TriggerAreas areas;
	const auto whole = [](Engine::Math::Fixed value) {
		const std::int64_t raw = value.Raw();
		return static_cast<std::int32_t>(raw >= 0 ? raw >> 16 : -((-raw) >> 16));
	};
	for (const auto &region : level.regions)
	{
		std::vector<std::array<std::int32_t, 2>> points;
		for (const auto &point : region.points)
			points.push_back({whole(point.x), whole(point.y)});
		areas.Add(region.name, std::move(points));
	}
	return areas;
}

// Every placed object (road and bridge points, scorch decals and waypoint markers are not objects), after the terrain's
// bridges and the bridge-like things (their own passes come first: PlaceBridges, PlaceBridgeLikeObjects).
void PlaceObjects(GameWorld &game)
{
	for (const auto &placement : game.level.placements)
	{
		if ((placement.flags & 0x36u) != 0 || placement.properties.Contains("scorchType") || placement.properties.Contains("waypointID"))
			continue;
		if (const auto *kind = game.templates.Content().objects.Find(placement.type); kind != nullptr && (kind->isBridge || kind->Is("WALK_ON_TOP_OF_WALL")))
			continue;
		const std::uint32_t team = TeamIndex(game, placement.properties.Get<std::string>("originalOwner").value_or(""));
		const ecs::Entity entity = SpawnObject(game, placement.type, placement.position.XY(), placement.orientation, team,
			placement.properties.Get<std::string>("objectName").value_or(""));
		if (auto *transform = game.world.IsAlive(entity) ? game.world.Get<engine::gameplay::Transform>(entity) : nullptr)
		{
			transform->position.z += placement.position.z;
			// Its layer: the ground's or the deck's nearest its height (getLayerForDestination; Object::setLayer).
			const std::uint8_t layer = engine::gameplay::LayerForDestination(game.world.Resource<engine::gameplay::DeckSurfaces>(), game.ground, transform->position);
			if (layer != engine::gameplay::GroundLayer)
			{
				game.world.Add<engine::gameplay::SurfaceLayer>(entity);
				*game.world.Get<engine::gameplay::SurfaceLayer>(entity) = engine::gameplay::SurfaceLayer{layer};
			}
		}
		// Since the team now has members, activate it. This magically created thing counts as built (onBuildComplete).
		if (game.world.IsAlive(entity))
		{
			game.roster.SetActive(team);
			OnBuildComplete(game, entity);
		}
		if (const auto level = placement.properties.Get<std::int64_t>("objectVeterancy"); level && game.world.IsAlive(entity))
			PlaceAtVeterancy(game, entity, *level);
		// objectRecruitableAI / objectSelectable / objectEnabled / objectPowered / objectIndestructible / objectUnsellable /
		// objectTargetable (updateObjValuesFromMapProperties, in its order).
		for (const auto &[key, flag] : {std::pair{"objectRecruitableAI", "AI Recruitable"}, std::pair{"objectSelectable", "Selectable"},
				 std::pair{"objectEnabled", "Enabled"}, std::pair{"objectPowered", "Powered"}, std::pair{"objectIndestructible", "Indestructible"},
				 std::pair{"objectUnsellable", "Unsellable"}, std::pair{"objectTargetable", "Player Targetable"}})
			if (const auto value = placement.properties.Get<bool>(key); value && game.world.IsAlive(entity))
				SetObjectPanelFlag(game, entity, flag, *value);
		// objectAggressiveness: its AI's mood (setAttitude).
		if (const auto mood = placement.properties.Get<std::int64_t>("objectAggressiveness"); mood && game.world.IsAlive(entity))
			if (auto *aggression = game.world.Get<engine::gameplay::Aggression>(entity))
				aggression->attitude = static_cast<std::int8_t>(*mood);
	}
}
}
