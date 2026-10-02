export module games.generalszh.gameplay.walls.algorithms.walls;
import std;

export import games.generalszh.gameplay.walls.algorithms.wall_pieces;
export import games.generalszh.gameplay.walls.systems.wall_piece_system;
import games.generalszh.gameplay.bridges.algorithms.bridges;
import games.generalszh.content.combat.combat_catalog;
import engine.gameplay.rts.navigation.algorithms.decks;
import engine.gameplay.rts.navigation.algorithms.clearance;
import engine.gameplay.common.spatial.components.surface_layer;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.health.components.pending_damage;
import engine.ecs.query.query;
export import engine.gameplay.rts.lifecycle.resources.casualties;
import engine.gameplay.common.identity.components.definition_ref;
import games.generalszh.content.objects.object_definition;

// The wall units walk on top of (the original's LAYER_WALL, Pathfinder's wall pieces):
//   BuildWallLayer (Pathfinder::newMap, after the bridge pass): with any pieces, the wall layer (AddWallLayer) at AIData's
//     WallHeight; every piece told its layer and region.
//   ApplyWallEvents (each tick, after the systems): pieces whose objects are gone leave the wall (removeWallPiece);
//     pieces fallen to rubble (WallPieceSystem) stop holding it up (classifyObjectFootprint's INVALID_ID), and everything
//     on the wall's layer over such a piece takes HUGE_DAMAGE_AMOUNT of DAMAGE_FALLING, DEATH_SPLATTED, from itself
//     (dealt with the next tick's damage); the wall's cells are classified afresh and its room rebuilt.
//   RegisterLayers (a checkpoint): the bridges' decks and the wall's layer again, in the order of their layers.
export namespace generalszh::gameplay
{
namespace walls_detail
{
inline void Reclassify(GameWorld &game)
{
	auto &surfaces = game.world.Resource<engine::gameplay::DeckSurfaces>();
	auto &grid = game.world.Resource<engine::gameplay::NavigationGrid>();
	const std::uint8_t layer = surfaces.wall.layer;
	if (layer == 0 || layer > grid.Decks().size())
		return;
	engine::gameplay::ClassifyWall(grid, layer, surfaces.wall);
	for (engine::gameplay::ClearancePlane &plane : grid.Clearance())
		engine::gameplay::BuildDeckClearance(grid, plane, layer);
}

// The pieces' footprints as boxes or not, in the wall's order.
inline std::vector<std::uint8_t> Boxes(GameWorld &game, const engine::gameplay::WallSurface &wall)
{
	std::vector<std::uint8_t> boxes;
	boxes.reserve(wall.Pieces());
	for (const ecs::Entity piece : wall.owner)
	{
		const auto *info = game.world.IsAlive(piece) ? game.world.Get<WallPiece>(piece) : nullptr;
		boxes.push_back(info != nullptr ? info->box : 1u);
	}
	return boxes;
}
}

inline void BuildWallLayer(GameWorld &game)
{
	auto &surfaces = game.world.Resource<engine::gameplay::DeckSurfaces>();
	auto &wall = surfaces.wall;
	wall.height = game.templates.Content().aiData.wallHeight;
	if (wall.Pieces() == 0)
		return;
	auto &grid = game.world.Resource<engine::gameplay::NavigationGrid>();
	const std::vector<std::uint8_t> boxes = walls_detail::Boxes(game, wall);
	const auto layer = engine::gameplay::AddWallLayer(grid, wall, boxes);
	if (!layer)
		return;
	// Its place among the decks' surfaces (the wall has none of its own shape: a placeholder).
	surfaces.decks.resize(*layer);
	for (const ecs::Entity piece : wall.owner)
		if (auto *info = game.world.IsAlive(piece) ? game.world.Get<WallPiece>(piece) : nullptr)
			*info = WallPiece{wall.x0, wall.y0, wall.columns, wall.rows, *layer, info->valid, info->box};
}

inline void ApplyWallEvents(GameWorld &game, std::span<const engine::gameplay::Casualty> casualties)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	std::vector<ecs::Entity> fallen;
	if (auto *events = world.FindResource<WallEvents>())
	{
		events->AppendTo(fallen);
		events->Reset(0);
	}
	auto &surfaces = world.Resource<gp::DeckSurfaces>();
	gp::WallSurface &wall = surfaces.wall;
	if (wall.layer == 0)
		return;
	// Pieces killed this tick fell to rubble as they died (ActiveBody's BODY_RUBBLE before anything removed them).
	for (const gp::Casualty &casualty : casualties)
		if (casualty.departure == gp::Departure::Killed)
			fallen.push_back(casualty.entity);
	std::sort(fallen.begin(), fallen.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
	fallen.erase(std::unique(fallen.begin(), fallen.end()), fallen.end());
	bool changed = false;
	const std::uint32_t type = content::DamageTypeIndex("FALLING").value_or(0);
	const std::uint32_t death = content::DeathTypeIndex("SPLATTED").value_or(0);
	for (const ecs::Entity object : fallen)
	{
		for (std::size_t piece = 0; piece < wall.Pieces(); ++piece)
		{
			if (wall.owner[piece] != object || wall.valid[piece] == 0)
				continue;
			if (auto *info = world.IsAlive(object) ? world.Get<WallPiece>(object) : nullptr)
				info->valid = 0;
			wall.valid[piece] = 0;
			changed = true;
			// Kill anybody on the wall over the piece.
			std::vector<ecs::Entity> falling;
			ecs::Query<ecs::Read<gp::SurfaceLayer>, ecs::Read<gp::Transform>> query(world);
			query.ForEachChunk([&](auto chunk) {
				const auto layers = chunk.template Get<gp::SurfaceLayer>();
				const auto transforms = chunk.template Get<gp::Transform>();
				const auto entities = chunk.Entities();
				for (std::size_t row = 0; row < layers.size(); ++row)
					if (layers[row].layer == wall.layer && gp::PointOnWallPiece(wall, piece, transforms[row].position.XY()))
						falling.push_back(entities[row]);
			});
			for (const ecs::Entity entity : falling)
			{
				if (world.Get<gp::Health>(entity) == nullptr)
					continue;
				if (!world.Has<gp::PendingDamage>(entity))
					world.Add<gp::PendingDamage>(entity);
				*world.Get<gp::PendingDamage>(entity) = gp::PendingDamage{entity, gp::HugeDamage(), type, death};
			}
		}
	}
	// removeWallPiece: the last piece takes the place of one whose object is gone.
	for (std::size_t piece = 0; piece < wall.Pieces();)
	{
		if (world.IsAlive(wall.owner[piece]) && world.Has<WallPiece>(wall.owner[piece]))
		{
			++piece;
			continue;
		}
		const std::size_t last = wall.Pieces() - 1;
		wall.owner[piece] = wall.owner[last];
		wall.position[piece] = wall.position[last];
		wall.facing[piece] = wall.facing[last];
		wall.major[piece] = wall.major[last];
		wall.minor[piece] = wall.minor[last];
		wall.valid[piece] = wall.valid[last];
		wall.owner.pop_back();
		wall.position.pop_back();
		wall.facing.pop_back();
		wall.major.pop_back();
		wall.minor.pop_back();
		wall.valid.pop_back();
		changed = true;
	}
	if (changed)
		walls_detail::Reclassify(game);
}

// A checkpoint's decks and wall made again in the order of their layers (the bridges' as RegisterBridgeDecks does), on a
// grid of the terrain alone; the wall from the pieces still standing or fallen (those whose objects are gone are out of
// it), in its first region.
inline void RegisterLayers(GameWorld &game)
{
	auto &world = game.world;
	auto &surfaces = world.Resource<engine::gameplay::DeckSurfaces>();
	std::vector<std::pair<std::uint8_t, ecs::Entity>> bridges;
	ecs::Query<ecs::Read<Bridge>> bridgeQuery(world);
	bridgeQuery.ForEachChunk([&](auto chunk) {
		const auto list = chunk.template Get<Bridge>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < list.size(); ++row)
			if (list[row].layer != 0)
				bridges.emplace_back(list[row].layer, entities[row]);
	});
	std::stable_sort(bridges.begin(), bridges.end(), [](const auto &left, const auto &right) { return left.first < right.first; });
	surfaces.decks.clear();
	surfaces.wall = {};
	surfaces.wall.height = game.templates.Content().aiData.wallHeight;
	WallPiece region{};
	std::vector<ecs::Entity> pieces;
	ecs::Query<ecs::Read<WallPiece>, ecs::Read<engine::gameplay::Transform>> wallQuery(world);
	wallQuery.ForEachChunk([&](auto chunk) {
		const auto list = chunk.template Get<WallPiece>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < list.size(); ++row)
			if (list[row].layer != 0)
			{
				region = list[row];
				pieces.push_back(entities[row]);
			}
	});
	std::sort(pieces.begin(), pieces.end(), [](ecs::Entity left, ecs::Entity right) { return left.index < right.index; });
	for (const ecs::Entity piece : pieces)
	{
		const auto &transform = *world.Get<engine::gameplay::Transform>(piece);
		const auto *ref = world.Get<engine::gameplay::DefinitionRef>(piece);
		if (ref == nullptr)
			continue;
		const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
		const bool sphere = kind.geometry.shape == content::GeometryShape::Sphere;
		surfaces.wall.AddPiece(piece, transform.position.XY(), transform.facing, kind.geometry.majorRadius,
			sphere ? kind.geometry.majorRadius : kind.geometry.minorRadius, world.Get<WallPiece>(piece)->valid != 0);
	}
	auto &grid = world.Resource<engine::gameplay::NavigationGrid>();
	std::size_t next = 0;
	const std::uint8_t wallLayer = pieces.empty() ? 0 : region.layer;
	const std::size_t layers = std::max<std::size_t>(bridges.size() + (wallLayer != 0 ? 1 : 0), wallLayer);
	for (std::size_t layer = 1; layer <= layers; ++layer)
	{
		if (layer == wallLayer)
		{
			surfaces.decks.resize(layer);
			engine::gameplay::PushWallLayer(grid, surfaces.wall, region.x0, region.y0, region.width, region.height);
			continue;
		}
		if (next < bridges.size())
		{
			const auto &[bridgeLayer, object] = bridges[next++];
			RegisterDeck(game, object, *world.Get<Bridge>(object));
		}
	}
}
}
