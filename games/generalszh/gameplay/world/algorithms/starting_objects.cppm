export module games.generalszh.gameplay.world.algorithms.starting_objects;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.content.global.player_templates;
import engine.gameplay.common.spatial.algorithms.find_position;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.identity.components.definition_ref;
import Engine.Core.Math.FixedRandom;
import engine.ecs.query.query;
import engine.gameplay.rts.vision.resources.shroud_map;
import games.generalszh.gameplay.score.algorithms.scoring;

// placeNetworkBuildingsForPlayer: a skirmish or LAN player's start. Its
// faction's StartingBuilding at Player_<n>_Start (facing its placement view
// angle), then each StartingUnit around Player_<n>_Rally when the map has one,
// else half the building's bounding sphere short of the start spot (south),
// each at the first legal spot findPositionAround finds between 0.7 and 1.3
// times that radius from there (from a random start angle; one it cannot place
// is left out).
export namespace generalszh::gameplay
{
namespace starting_detail
{
inline std::optional<Engine::Math::FixedVector3> Marker(const GameWorld &game, std::string_view name)
{
	for (const auto &marker : game.level.markers)
		if (marker.name == name)
			return marker.position;
	return std::nullopt;
}

// PartitionManager::tryPosition for a point on the ground: inside the pathfind extent (the map's largest
// boundary), not a cliff cell, not under water, and no object touching a 5-unit sphere there.
inline bool LegalSpot(GameWorld &game, Engine::Math::FixedVector2 point)
{
	using Engine::Math::Fixed;
	const auto &terrain = game.level.terrain;
	const Fixed cell = terrain.cellSize;
	std::int64_t extentX = 0, extentY = 0;
	for (const auto &boundary : terrain.playableExtents)
	{
		extentX = std::max<std::int64_t>(extentX, boundary[0]);
		extentY = std::max<std::int64_t>(extentY, boundary[1]);
	}
	// The pathfind cells: 0 .. extent - 1 (Pathfinder::newMap); no cell, no position.
	const std::int64_t cellX = (point.x / cell).Floor(), cellY = (point.y / cell).Floor();
	if (point.x < Fixed{} || point.y < Fixed{} || cellX > extentX - 1 || cellY > extentY - 1)
		return false;
	// TerrainLogic::isCliffCell: the heightmap cell's cliff flag (clamped to the map as the original).
	const auto &surface = game.level.surface;
	if (!surface.cliffFlags.empty() && surface.cliffFlagBytesPerRow > 0)
	{
		const std::int64_t width = terrain.width, height = terrain.height, border = terrain.border;
		const std::int64_t x = std::clamp<std::int64_t>(cellX + border, 0, width - 2);
		const std::int64_t y = std::clamp<std::int64_t>(cellY + border, 0, height - 2);
		const std::uint8_t flags = surface.cliffFlags[static_cast<std::size_t>(y * surface.cliffFlagBytesPerRow + (x >> 3))];
		if ((flags & (1u << (x & 7))) != 0)
			return false;
	}
	const Fixed ground = game.ground.At(point);
	if (Fixed water; game.ground.Water(point, water) && ground < water)
		return false;
	// Objects: a sphere of 5 at the point on the ground against each object's geometry.
	const Fixed reach = Fixed::FromInt(5);
	bool blocked = false;
	ecs::Query<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Exclude<engine::gameplay::OffMap>> objects(game.world);
	objects.ForEachChunk([&](auto chunk) {
		if (blocked)
			return;
		const auto transforms = chunk.template Get<engine::gameplay::Transform>();
		const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
		for (std::size_t row = 0; row < transforms.size() && !blocked; ++row)
		{
			const content::Geometry &shape = game.templates.DefinitionAt(definitions[row].index).geometry;
			const auto &at = transforms[row].position;
			const Fixed top = at.z + (shape.height > Fixed{} ? shape.height : shape.majorRadius * Fixed::FromInt(2));
			if (ground + reach < at.z || ground - reach > top)
				continue;
			const Engine::Math::FixedVector2 offset = point - at.XY();
			if (shape.shape == content::GeometryShape::Box)
			{
				// Into the box's frame, then the closest point of its footprint.
				const Fixed cosine = Engine::Math::Cos(transforms[row].facing), sine = Engine::Math::Sin(transforms[row].facing);
				const Fixed along = offset.x * cosine + offset.y * sine, across = offset.y * cosine - offset.x * sine;
				const Fixed dx = along - std::clamp(along, Fixed{} - shape.majorRadius, shape.majorRadius);
				const Fixed dy = across - std::clamp(across, Fixed{} - shape.minorRadius, shape.minorRadius);
				blocked = dx * dx + dy * dy < reach * reach;
			}
			else
				blocked = Engine::Math::LengthSquared(offset) < (reach + shape.majorRadius) * (reach + shape.majorRadius);
		}
	});
	return !blocked;
}
}

// The objects made, the building first (none: no start spot or building).
inline std::vector<ecs::Entity> PlaceStartingObjects(GameWorld &game, const content::PlayerTemplateInfo &faction, int startPosition, std::uint32_t team)
{
	using Engine::Math::Fixed;
	using Engine::Math::FixedVector2;
	std::vector<ecs::Entity> made;
	const std::string number = std::to_string(startPosition + 1);
	const auto start = starting_detail::Marker(game, "Player_" + number + "_Start");
	const content::ObjectDefinition *building = game.templates.Content().objects.Find(faction.startingBuilding);
	if (!start || building == nullptr)
		return made;
	const Engine::Math::TurnAngle facing = Engine::Math::TurnFromDegrees(building->placementViewAngleDegrees);
	const ecs::Entity yard = SpawnObject(game, faction.startingBuilding, start->XY(), facing, team, "");
	CreateModulesBuildComplete(game, yard); // placeObjectAtPosition: onBuildComplete
	OnBuildComplete(game, yard); // GameLogic::startNewGame: built
	ScoreStructureComplete(game, yard, false); // onStructureConstructionComplete
	if (!game.world.IsAlive(yard))
		return made;
	made.push_back(yard);
	const Fixed radius = content::BoundingSphereRadius(building->geometry);
	FixedVector2 around = start->XY() - FixedVector2{Fixed{}, radius / Fixed::FromInt(2)};
	if (const auto rally = starting_detail::Marker(game, "Player_" + number + "_Rally"))
		around = rally->XY();
	for (const std::string &unit : faction.startingUnits)
	{
		if (unit.empty())
			continue;
		// FindPositionOptions: min 0.7, max 1.3 times the building's bounding sphere; a random start angle.
		const auto startAngle = Engine::Math::TurnAngle{static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, 0xFFFFFFFFll))};
		const auto spot = engine::gameplay::FindPositionAround(around, radius * Fixed::FromRatio(7, 10), radius * Fixed::FromRatio(13, 10), startAngle,
			[&](FixedVector2 point) { return starting_detail::LegalSpot(game, point); });
		if (!spot)
			continue; // "Could not find position"
		const ecs::Entity entity = SpawnObject(game, unit, *spot, facing, team, "");
		CreateModulesBuildComplete(game, entity); // placeObjectAtPosition: onBuildComplete
		OnBuildComplete(game, entity);
		ScoreUnitCreated(game, entity); // Player::onUnitCreated
		if (game.world.IsAlive(entity))
			made.push_back(entity);
	}
	return made;
}

// GameLogic::startNewGame for a game with seats (TheGameInfo: skirmish, LAN): each occupied seat's player sees the map at
// once: an observer's revealed for good (revealMapForPlayerPermanently), any other's looked over once, fog where nobody
// looks (revealMapForPlayer), unless MultiplayerSettings' UseShroud keeps it black until explored.
inline void RevealSeatsAtStart(GameWorld &game, std::span<const std::uint32_t> players, std::span<const std::uint8_t> observers, bool useShroud)
{
	auto *shroud = game.world.FindResource<engine::gameplay::ShroudMap>();
	if (shroud == nullptr)
		return;
	for (std::size_t index = 0; index < players.size(); ++index)
	{
		if (index < observers.size() && observers[index] != 0)
			shroud->RevealAllPermanently(players[index]);
		else if (!useShroud)
			shroud->RevealAll(players[index]);
	}
}
}
