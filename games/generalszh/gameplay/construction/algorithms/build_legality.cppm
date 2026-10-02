export module games.generalszh.gameplay.construction.algorithms.build_legality;
import std;
import games.generalszh.gameplay.powers.algorithms.special_power_state;

export import games.generalszh.gameplay.world.resources.game_world;
export import engine.gameplay.common.spatial.algorithms.footprint;
import engine.ecs.query.query;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.navigation.resources.navigation_grid;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.rts.navigation.algorithms.route_search;
import engine.gameplay.rts.navigation.algorithms.clearance;
import games.generalszh.gameplay.ai.resources.ai_players;

// BuildAssistant::isLocationLegalToBuild, for a builder placing a structure
// (DozerAIUpdate::construct: TERRAIN_RESTRICTIONS | NO_OBJECT_OVERLAP):
//   off the map's pathfinding extent: restricted terrain;
//   isLocationClearOfObjects: over whatever its footprint overlaps, those that
//   may be cleared (shrubbery, CLEARED_BY_BUILD, the dead), mines and inert
//   things are no matter; an ally that is busy (moving) is in the way; an
//   immobile or disabled thing is in the way; so is an enemy;
//   CANNOT_BUILD_NEAR_SUPPLIES: not within SupplyBuildBorder of a supply source;
//   the ground under it, sampled every 30 then every 10 units: no water, cliff
//   or impassable cells, no more than AllowedHeightVariationForBuilding between
//   its lowest and highest point, not within MinDistFromEdgeOfMapForBuild of
//   the map's edge.
export namespace generalszh::gameplay
{
enum class LegalBuild : std::uint8_t
{
	Ok,
	RestrictedTerrain,
	NotFlatEnough,
	ObjectsInTheWay,
	TooCloseToSupplies,
	NoClearPath,
};

// isLocationLegalToBuild's options (BuildAssistant::LocalLegalToBuildOptions).
namespace build_check
{
inline constexpr std::uint32_t TerrainRestrictions = 1u << 0;
inline constexpr std::uint32_t ClearPath = 1u << 1;
inline constexpr std::uint32_t NoObjectOverlap = 1u << 2;
inline constexpr std::uint32_t NoEnemyObjectOverlap = 1u << 3;
inline constexpr std::uint32_t Placement = TerrainRestrictions | NoObjectOverlap; // DozerAIUpdate::construct for a human
}

// Its footprint as geometry gives it (a box, else a circle of its radius).
inline engine::gameplay::Footprint FootprintOf(const content::ObjectDefinition &object)
{
	namespace gp = engine::gameplay;
	if (object.geometry.shape == content::GeometryShape::Box)
		return {gp::FootprintShape::Box, object.geometry.majorRadius, object.geometry.minorRadius};
	return {gp::FootprintShape::Circle, object.geometry.majorRadius, object.geometry.majorRadius};
}

// BuildAssistant::isRemovableForConstruction.
inline bool RemovableForConstruction(GameWorld &game, ecs::Entity entity, const content::ObjectDefinition &definition)
{
	namespace gp = engine::gameplay;
	if (definition.Is("INERT"))
		return false;
	if (definition.Is("SHRUBBERY") || definition.Is("CLEARED_BY_BUILD"))
		return true;
	const auto *health = game.world.Get<gp::Health>(entity);
	return game.world.Has<gp::Dying>(entity) || (health != nullptr && gp::IsDead(*health));
}

// The things on the map (not aboard anything) a run of build checks looks at, bucketed by where they stand, so each check
// looks only near its spot (the partition manager's part in isLocationLegalToBuild): gathered once for a search.
struct BuildScene
{
	struct Thing
	{
		ecs::Entity entity;
		std::uint32_t definition{0};
		Engine::Math::FixedVector2 position;
		Engine::Math::TurnAngle facing;
		Engine::Math::Fixed radius; // its footprint's bounding circle
		bool supply{false};         // SUPPLY_SOURCE
	};
	static constexpr std::int64_t BucketSize = 100;
	std::vector<Thing> things;
	std::map<std::pair<std::int64_t, std::int64_t>, std::vector<std::uint32_t>> buckets;
	Engine::Math::Fixed largest;

	static std::int64_t BucketOf(Engine::Math::Fixed value) { return (value / Engine::Math::Fixed::FromInt(BucketSize)).Floor(); }

	// Each thing that may reach within `reach` of `at` (its bounding circle), in entity order.
	template<typename Visit>
	void ForEachNear(Engine::Math::FixedVector2 at, Engine::Math::Fixed reach, Visit &&visit) const
	{
		const Engine::Math::Fixed grown = reach + largest;
		std::vector<std::uint32_t> near;
		for (std::int64_t by = BucketOf(at.y - grown); by <= BucketOf(at.y + grown); ++by)
			for (std::int64_t bx = BucketOf(at.x - grown); bx <= BucketOf(at.x + grown); ++bx)
				if (const auto found = buckets.find({bx, by}); found != buckets.end())
					for (const std::uint32_t index : found->second)
					{
						const Thing &thing = things[index];
						const Engine::Math::Fixed limit = reach + thing.radius;
						if (Engine::Math::DistanceSquared(thing.position, at) <= limit * limit)
							near.push_back(index);
					}
		std::sort(near.begin(), near.end(), [&](std::uint32_t a, std::uint32_t b) { return things[a].entity.index < things[b].entity.index; });
		for (const std::uint32_t index : near)
			visit(things[index]);
	}
};

inline Engine::Math::Fixed FootprintRadius(const engine::gameplay::Footprint &footprint)
{
	return footprint.shape == engine::gameplay::FootprintShape::Box ? Engine::Math::Length(Engine::Math::FixedVector2{footprint.major, footprint.minor})
																	: footprint.major;
}

// Everything on the map (not aboard anything) within `reach` of `center`.
inline BuildScene GatherBuildScene(GameWorld &game, Engine::Math::FixedVector2 center, Engine::Math::Fixed reach)
{
	namespace gp = engine::gameplay;
	BuildScene scene;
	ecs::Query<ecs::Read<gp::Transform>, ecs::Read<gp::DefinitionRef>, ecs::Exclude<gp::OffMap>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto transforms = chunk.template Get<gp::Transform>();
		const auto definitions = chunk.template Get<gp::DefinitionRef>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < entities.size(); ++row)
		{
			const content::ObjectDefinition &other = game.templates.DefinitionAt(definitions[row].index);
			const Engine::Math::Fixed radius = FootprintRadius(FootprintOf(other));
			const auto at = transforms[row].position.XY();
			const Engine::Math::Fixed limit = reach + radius;
			if (Engine::Math::DistanceSquared(at, center) > limit * limit)
				continue;
			scene.things.push_back({entities[row], definitions[row].index, at, transforms[row].facing, radius, other.Is("SUPPLY_SOURCE")});
			scene.largest = std::max(scene.largest, radius);
		}
	});
	for (std::uint32_t index = 0; index < scene.things.size(); ++index)
		scene.buckets[{BuildScene::BucketOf(scene.things[index].position.x), BuildScene::BucketOf(scene.things[index].position.y)}].push_back(index);
	return scene;
}

// Everything on the map whose footprint overlaps `footprint` there (in entity order): from `scene` when given (it must
// cover the spot), else the whole map.
template<typename Visit>
void ForEachOverlapping(GameWorld &game, const engine::gameplay::Footprint &footprint, Engine::Math::FixedVector2 at, Engine::Math::TurnAngle facing,
	Visit &&visit, const BuildScene *scene = nullptr)
{
	namespace gp = engine::gameplay;
	std::optional<BuildScene> local;
	if (scene == nullptr)
		scene = &local.emplace(GatherBuildScene(game, at, FootprintRadius(footprint)));
	std::vector<std::pair<ecs::Entity, std::uint32_t>> found;
	scene->ForEachNear(at, FootprintRadius(footprint), [&](const BuildScene::Thing &thing) {
		if (gp::FootprintsOverlap(footprint, at, facing, FootprintOf(game.templates.DefinitionAt(thing.definition)), thing.position, thing.facing))
			found.emplace_back(thing.entity, thing.definition);
	});
	for (const auto &[entity, definition] : found)
		visit(entity, game.templates.DefinitionAt(definition));
}

// isLocationClearOfObjects' second part ("Check for overlapping exit areas"): every structure whose bounding circle is
// within 2 x (major + minor radius) of the spot, but what construction clears. Each side's bounds are its geometry grown
// by its FactoryExtraBibWidth (a non-box one a box of its major radius; a box his minor grown by MY extra width, as the
// original), its exit a rectangle FactoryExitWidth deep in front of it (half that as its major radius, its geometry's
// minor; a non-box one its major radius as minor), centred its major radius plus half the exit ahead along its facing.
// The two bounds overlapping is in the way; for an IMMOBILE structure, so is my exit over its bounds, its exit over mine,
// or the two exits. A skirmish AI builder keeps at least 3 cells of extra width (taken from its exit width).
inline LegalBuild CheckFactoryExits(GameWorld &game, const content::ObjectDefinition &build, Engine::Math::FixedVector2 at,
	Engine::Math::TurnAngle facing, ecs::Entity builder)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	auto &world = game.world;
	const Fixed range = Fixed::FromInt(2) * (build.geometry.majorRadius + build.geometry.minorRadius);
	Fixed myExitWidth = build.factoryExitWidth;
	Fixed myExtraWidth = build.factoryExtraBibWidth;
	if (const auto *owner = world.IsAlive(builder) ? world.Get<gp::Owner>(builder) : nullptr)
		if (const auto *ais = world.FindResource<AiPlayers>())
			if (const AiPlayer *ai = ais->Of(owner->player); ai != nullptr && ai->skirmish)
			{
				const Fixed cells = Fixed::FromInt(30); // 3 * PATHFIND_CELL_SIZE_F
				if (myExtraWidth < cells)
				{
					myExtraWidth = cells;
					myExitWidth -= myExtraWidth;
					if (myExitWidth < Fixed{})
						myExitWidth = Fixed{};
				}
			}
	const bool box = build.geometry.shape == content::GeometryShape::Box;
	const gp::Footprint myBounds = box ? gp::Footprint{gp::FootprintShape::Box, build.geometry.majorRadius + myExtraWidth, build.geometry.minorRadius + myExtraWidth}
		: gp::Footprint{gp::FootprintShape::Box, build.geometry.majorRadius + myExtraWidth, build.geometry.majorRadius + myExtraWidth};
	const gp::Footprint myGeom{box ? gp::FootprintShape::Box : gp::FootprintShape::Circle, myExitWidth / Fixed::FromInt(2),
		box ? build.geometry.minorRadius : build.geometry.majorRadius};
	const bool checkMyExit = myExitWidth > Fixed{};
	const Fixed myOffset = build.geometry.majorRadius + myExitWidth / Fixed::FromInt(2);
	const Engine::Math::FixedVector2 myExitPos{at.x + Engine::Math::Cos(facing) * myOffset, at.y + Engine::Math::Sin(facing) * myOffset};
	LegalBuild result = LegalBuild::Ok;
	const BuildScene scene = GatherBuildScene(game, at, range);
	scene.ForEachNear(at, range, [&](const BuildScene::Thing &thing) {
		if (result != LegalBuild::Ok)
			return;
		const content::ObjectDefinition &them = game.templates.DefinitionAt(thing.definition);
		if (!them.Is("STRUCTURE") || RemovableForConstruction(game, thing.entity, them))
			return;
		const Fixed hisExitWidth = them.factoryExitWidth;
		const Fixed hisExtraWidth = them.factoryExtraBibWidth;
		const bool hisBox = them.geometry.shape == content::GeometryShape::Box;
		const gp::Footprint hisBounds = hisBox
			? gp::Footprint{gp::FootprintShape::Box, them.geometry.majorRadius + hisExtraWidth, them.geometry.minorRadius + myExtraWidth}
			: gp::Footprint{gp::FootprintShape::Box, them.geometry.majorRadius + hisExtraWidth, them.geometry.majorRadius + hisExtraWidth};
		const gp::Footprint hisGeom{hisBox ? gp::FootprintShape::Box : gp::FootprintShape::Circle, hisExitWidth / Fixed::FromInt(2),
			hisBox ? them.geometry.minorRadius : them.geometry.majorRadius};
		const bool checkHisExit = hisExitWidth > Fixed{};
		const Fixed hisOffset = them.geometry.majorRadius + hisExitWidth / Fixed::FromInt(2);
		const Engine::Math::FixedVector2 hisExitPos{thing.position.x + Engine::Math::Cos(thing.facing) * hisOffset,
			thing.position.y + Engine::Math::Sin(thing.facing) * hisOffset};
		if (gp::FootprintsOverlap(hisBounds, thing.position, thing.facing, myBounds, at, facing))
		{
			result = LegalBuild::ObjectsInTheWay;
			return;
		}
		if (!checkMyExit && !checkHisExit && hisExtraWidth == Fixed{} && myExtraWidth == Fixed{})
			return; // neither has extra exit space
		if (!them.Is("IMMOBILE"))
			return;
		if ((checkMyExit && gp::FootprintsOverlap(hisBounds, thing.position, thing.facing, myGeom, myExitPos, facing)) ||
			(checkHisExit && gp::FootprintsOverlap(hisGeom, hisExitPos, thing.facing, myBounds, at, facing)) ||
			(checkMyExit && checkHisExit && gp::FootprintsOverlap(hisGeom, hisExitPos, thing.facing, myGeom, myExitPos, facing)))
			result = LegalBuild::ObjectsInTheWay;
	});
	return result;
}

// With only NoEnemyObjectOverlap (isLocationClearOfObjects' onlyCheckEnemies), an immobile thing that is not an enemy is
// no matter. Near supplies is always checked; ClearPath: the builder (not immobile) can reach it.
inline LegalBuild CheckBuildLocation(GameWorld &game, const content::ObjectDefinition &build, Engine::Math::FixedVector2 at, Engine::Math::TurnAngle facing,
	ecs::Entity builder, std::uint32_t options = build_check::Placement, const BuildScene *scene = nullptr)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	auto &world = game.world;
	const gp::NavigationGrid &grid = world.Resource<gp::NavigationGrid>();
	const Fixed cell = Fixed::FromInt(10);
	const Fixed extentX = Fixed::FromInt(grid.Width()) * cell, extentY = Fixed::FromInt(grid.Height()) * cell;
	if (at.x < Fixed{} || at.y < Fixed{} || at.x > extentX || at.y > extentY)
		return LegalBuild::RestrictedTerrain;
	const gp::Footprint footprint = FootprintOf(build);
	const auto *builderOwner = world.IsAlive(builder) ? world.Get<gp::Owner>(builder) : nullptr;
	const auto &relationships = world.Resource<gp::Relationships>();
	// isLocationClearOfObjects (for either overlap option).
	LegalBuild result = LegalBuild::Ok;
	const bool onlyEnemies = options == build_check::NoEnemyObjectOverlap;
	if ((options & (build_check::NoObjectOverlap | build_check::NoEnemyObjectOverlap)) != 0)
	ForEachOverlapping(game, footprint, at, facing, [&](ecs::Entity them, const content::ObjectDefinition &definition) {
		if (result != LegalBuild::Ok || RemovableForConstruction(game, them, definition) || definition.Is("MINE") || definition.Is("INERT"))
			return;
		const auto *theirs = world.Get<gp::Owner>(them);
		// Object::getRelationship (the teams' overrides, then the players').
		const gp::Relationship relation = builderOwner != nullptr && theirs != nullptr ? RelationOf(game, builder, them) : gp::Relationship::Neutral;
		const bool allied = relation == gp::Relationship::Allies;
		const bool enemy = relation == gp::Relationship::Enemies;
		const auto *order = world.Get<gp::MoveOrder>(them);
		const bool busy = order != nullptr && order->mode != gp::MoveMode::Idle && them != builder;
		const auto *off = world.Get<gp::Disabled>(them);
		if (definition.Is("IMMOBILE") && onlyEnemies && builderOwner != nullptr && !enemy)
			return;
		if ((allied && busy) || definition.Is("IMMOBILE") || (off != nullptr && off->mask != 0) || enemy)
			result = LegalBuild::ObjectsInTheWay;
	}, scene);
	if (result != LegalBuild::Ok)
		return result;
	if (!onlyEnemies && (options & (build_check::NoObjectOverlap | build_check::NoEnemyObjectOverlap)) != 0)
		if (const LegalBuild exits = CheckFactoryExits(game, build, at, facing, builder); exits != LegalBuild::Ok)
			return exits;
	const Fixed border = game.templates.Content().gameData.supplyBuildBorder;
	if (build.Is("CANNOT_BUILD_NEAR_SUPPLIES") && border > Fixed{})
	{
		// Its footprint against each supply source's grown by the border (expandFootprint).
		std::optional<BuildScene> local;
		const Fixed reach = FootprintRadius(footprint) + border * Fixed::FromInt(2);
		const BuildScene &near = scene != nullptr ? *scene : local.emplace(GatherBuildScene(game, at, reach));
		near.ForEachNear(at, reach, [&](const BuildScene::Thing &thing) {
			if (!thing.supply)
				return;
			gp::Footprint grown = FootprintOf(game.templates.DefinitionAt(thing.definition));
			grown.major += border;
			grown.minor += border;
			if (gp::FootprintsOverlap(footprint, at, facing, grown, thing.position, thing.facing))
				result = LegalBuild::TooCloseToSupplies;
		});
		if (result != LegalBuild::Ok)
			return result;
	}
	// CLEAR_PATH: a route for the builder reaches it (isQuickPathAvailable).
	if ((options & build_check::ClearPath) != 0 && world.IsAlive(builder))
	{
		const auto *builderRef = world.Get<gp::DefinitionRef>(builder);
		if (builderRef == nullptr || !game.templates.DefinitionAt(builderRef->index).Is("IMMOBILE"))
		{
			const auto *agent = world.Get<gp::NavigationAgent>(builder);
			const auto *from = world.Get<gp::Transform>(builder);
			if (agent == nullptr || from == nullptr)
				return LegalBuild::NoClearPath;
			// clientSafeQuickDoesPathExist: connected zones, not a route search.
			if (!gp::QuickPathExists(world.Resource<gp::NavigationGrid>(), agent->surfaces, from->position.XY(), at))
				return LegalBuild::NoClearPath;
		}
	}
	if ((options & build_check::TerrainRestrictions) == 0)
		return LegalBuild::Ok;
	// The ground under it: coarse, then careful.
	const Fixed edge = game.templates.Content().gameData.minDistFromEdgeOfMapForBuild;
	const Fixed allowed = game.templates.Content().gameData.allowedHeightVariationForBuilding;
	bool restricted = false;
	Fixed low, high;
	bool sampled = false;
	const auto sample = [&](Engine::Math::FixedVector2 point) {
		const auto x = static_cast<std::int32_t>((point.x / cell).Floor()), y = static_cast<std::int32_t>((point.y / cell).Floor());
		if (!grid.Contains(x, y))
			restricted = true;
		else if (const gp::PathfindCellType type = grid.Type(x, y);
				 type == gp::PathfindCellType::Water || type == gp::PathfindCellType::Cliff || type == gp::PathfindCellType::Impassable)
			restricted = true;
		const Fixed z = game.ground.At(point);
		low = sampled ? std::min(low, z) : z;
		high = sampled ? std::max(high, z) : z;
		sampled = true;
		if (edge > Fixed{} && (point.x < edge || point.x > extentX - edge || point.y < edge || point.y > extentY - edge))
			restricted = true;
	};
	for (const Fixed step : {Fixed::FromInt(30), cell})
	{
		gp::ForEachFootprintSample(footprint, at, facing, step, sample);
		if (restricted)
			return LegalBuild::RestrictedTerrain;
		if (high - low > allowed)
			return LegalBuild::NotFlatEnough;
	}
	return LegalBuild::Ok;
}

// BuildAssistant::clearRemovableForConstruction: what may be cleared under it goes (shrubbery, CLEARED_BY_BUILD, the
// dead), as the structure is placed.
inline std::vector<ecs::Entity> RemovableUnder(GameWorld &game, const content::ObjectDefinition &build, Engine::Math::FixedVector2 at,
	Engine::Math::TurnAngle facing)
{
	std::vector<ecs::Entity> removable;
	ForEachOverlapping(game, FootprintOf(build), at, facing, [&](ecs::Entity them, const content::ObjectDefinition &definition) {
		if (RemovableForConstruction(game, them, definition))
			removable.push_back(them);
	});
	return removable;
}

// BuildAssistant::moveObjectsForConstruction: what stands where it goes (within a box as wide as its major radius each
// way, or its own box), neither a MINE nor INERT nor ALWAYS_SELECTABLE nor removable, is the builder's player's own, an
// ally's or a neutral's with an AI: told to move out (aiMoveToPositionEvenIfSleeping, CMD_FROM_AI) to a random spot 0.5
// to 1.5 times its reach (the box's corner distance * 1.4) away in a random direction; anything else there (an enemy's,
// or one with no AI) counts as unmovable: `clear` false. The moves are the caller's to order.
struct ConstructionMoves
{
	bool clear{true};
	std::vector<std::pair<ecs::Entity, Engine::Math::FixedVector2>> moves;
};

inline ConstructionMoves MoveObjectsForConstruction(GameWorld &game, const content::ObjectDefinition &build, Engine::Math::FixedVector2 at,
	Engine::Math::TurnAngle facing, std::uint32_t player)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	gp::Footprint box{gp::FootprintShape::Box, build.geometry.majorRadius, build.geometry.majorRadius};
	if (build.geometry.shape == content::GeometryShape::Box)
		box = FootprintOf(build);
	const Fixed reach = Engine::Math::Sqrt(box.major * box.major + box.minor * box.minor) * Fixed::FromRatio(14, 10);
	const auto *relationships = game.world.FindResource<gp::Relationships>();
	bool unmovables = false;
	std::vector<ecs::Entity> movers;
	ForEachOverlapping(game, box, at, facing, [&](ecs::Entity them, const content::ObjectDefinition &definition) {
		if (definition.Is("MINE") || definition.Is("INERT") || definition.Is("ALWAYS_SELECTABLE") || RemovableForConstruction(game, them, definition))
			return;
		const auto *owner = game.world.Get<gp::Owner>(them);
		const std::uint32_t theirs = owner != nullptr ? owner->player : player;
		const gp::Relationship relation = theirs == player ? gp::Relationship::Allies
			: relationships != nullptr ? relationships->Between(player, theirs) : gp::Relationship::Neutral;
		if (relation != gp::Relationship::Neutral && relation != gp::Relationship::Allies)
		{
			unmovables = true;
			return;
		}
		if (game.world.Get<gp::MoveOrder>(them) == nullptr)
		{
			unmovables = true;
			return;
		}
		movers.push_back(them);
	});
	ConstructionMoves result;
	result.clear = !unmovables;
	for (const ecs::Entity them : movers)
	{
		const Fixed distance = Engine::Math::UniformFixed(game.random, Fixed::FromRatio(1, 2), Fixed::FromRatio(3, 2)) * reach;
		const Engine::Math::TurnAngle direction{static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, 0xFFFFFFFFll))};
		result.moves.emplace_back(them, Engine::Math::FixedVector2{at.x + distance * Engine::Math::Cos(direction), at.y + distance * Engine::Math::Sin(direction)});
	}
	return result;
}
}
