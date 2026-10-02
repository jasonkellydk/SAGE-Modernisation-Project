export module games.generalszh.gameplay.bridges.algorithms.bridges;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.bridges.components.bridge;
export import games.generalszh.gameplay.bridges.systems.bridge_damage_system;
export import games.generalszh.gameplay.bridges.resources.bridge_cues;
import games.generalszh.gameplay.walls.algorithms.wall_pieces;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.world.algorithms.map_properties;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.creation.algorithms.creation_list_runner;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.content.terrain.bridge_content;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.health.components.health;
import engine.gameplay.rts.lifecycle.resources.casualties;
import engine.gameplay.rts.navigation.algorithms.decks;
import engine.gameplay.common.spatial.resources.deck_surfaces;
import engine.gameplay.common.spatial.components.surface_layer;
import engine.gameplay.rts.navigation.algorithms.clearance;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.common.health.components.pending_damage;
import games.generalszh.content.combat.combat_catalog;
import engine.ecs.query.query;
import Engine.Core.Math.FixedRandom;

// The terrain's bridges (the original's TerrainLogic bridges, W3DBridgeBuffer::loadBridges' logic half and
// BridgeBehavior's reach beyond its object): map-drawn bridges made from their map points, landmark bridges and their
// towers, where a bridge's deck is, its damage state as TerrainLogic records it (updateBridgeDamageStates), the effects
// of its transitions and its death, and the scripts' questions (isBridgeBroken / isBridgeRepaired).
export namespace generalszh::gameplay
{
namespace bridge_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;
using Engine::Math::FixedVector3;

// BRIDGE_FLOAT_AMT: a map-drawn bridge's ends float this far above the height map.
inline Fixed FloatAmount() { return Fixed::FromRatio(1, 4); }

// MapObject flags.
inline constexpr std::uint32_t BridgePoint1 = 0x10;
inline constexpr std::uint32_t BridgePoint2 = 0x20;

inline const content::ObjectDefinition *DefinitionOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gp::DefinitionRef>(entity) : nullptr;
	return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
}

// The neutral player's default team (ThePlayerList->getNeutralPlayer()->getDefaultTeam()).
inline std::uint32_t NeutralTeam(const GameWorld &game) { return TeamIndex(game, "team"); }

// A 2D cross product's sign test: `p` on the inner side of (or on) every edge of the triangle.
inline bool InTriangle(FixedVector2 a, FixedVector2 b, FixedVector2 c, FixedVector2 p)
{
	const auto side = [](FixedVector2 from, FixedVector2 to, FixedVector2 at) { return (to.x - from.x) * (at.y - from.y) - (to.y - from.y) * (at.x - from.x); };
	const Fixed d1 = side(a, b, p), d2 = side(b, c, p), d3 = side(c, a, p);
	const bool negative = d1 < Fixed{} || d2 < Fixed{} || d3 < Fixed{};
	const bool positive = d1 > Fixed{} || d2 > Fixed{} || d3 > Fixed{};
	return !(negative && positive);
}
}

// The bridge's Roads.ini template (TerrainRoadCollection::findBridge), by its index in the catalog.
inline const content::BridgeContent *BridgeTemplateOf(const GameWorld &game, const Bridge &bridge)
{
	const auto &catalog = game.templates.Content().bridges;
	if (bridge.bridgeTemplate >= catalog.size())
		return nullptr;
	return &std::next(catalog.begin(), static_cast<std::ptrdiff_t>(bridge.bridgeTemplate))->second;
}

inline std::uint32_t BridgeTemplateIndex(const GameWorld &game, std::string_view name)
{
	const auto &catalog = game.templates.Content().bridges;
	const auto found = catalog.find(name);
	return found == catalog.end() ? 0xFFFFFFFFu : static_cast<std::uint32_t>(std::distance(catalog.begin(), found));
}

// Bridge::isPointOnBridge: within its bounds, in one of the two triangles its corners make (2D).
inline bool PointOnBridge(const Bridge &bridge, Engine::Math::FixedVector2 at)
{
	using namespace bridge_detail;
	const FixedVector2 left1 = bridge.fromLeft.XY(), right1 = bridge.fromRight.XY(), left2 = bridge.toLeft.XY(), right2 = bridge.toRight.XY();
	const Fixed loX = std::min({left1.x, right1.x, left2.x, right2.x}), hiX = std::max({left1.x, right1.x, left2.x, right2.x});
	const Fixed loY = std::min({left1.y, right1.y, left2.y, right2.y}), hiY = std::max({left1.y, right1.y, left2.y, right2.y});
	if (at.x < loX || at.x > hiX || at.y < loY || at.y > hiY)
		return false;
	return InTriangle(left1, right1, left2, at) || InTriangle(right1, left2, right2, at);
}

// Bridge::getBridgeHeight: the deck's plane (through fromLeft, fromRight and toLeft) straight above or below `at`.
inline Engine::Math::Fixed BridgeHeight(const Bridge &bridge, Engine::Math::FixedVector2 at)
{
	using namespace bridge_detail;
	const FixedVector3 u = bridge.fromRight - bridge.fromLeft, v = bridge.toLeft - bridge.fromLeft;
	const FixedVector3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
	if (n.z == Fixed{})
		return bridge.fromLeft.z;
	return bridge.fromLeft.z - (n.x * (at.x - bridge.fromLeft.x) + n.y * (at.y - bridge.fromLeft.y)) / n.z;
}

// BridgeBehavior::getRandomSurfacePosition: along the deck and across it at random, up to TransitionEffectsHeight above.
inline Engine::Math::FixedVector3 RandomSurfacePosition(GameWorld &game, const Bridge &bridge, const content::BridgeContent &bridgeTemplate)
{
	using namespace bridge_detail;
	const FixedVector3 along = bridge.toLeft - bridge.fromLeft;
	const Fixed a = Engine::Math::UniformFixed(game.random, Fixed{}, Fixed::One());
	const FixedVector3 across = bridge.fromRight - bridge.fromLeft;
	const Fixed b = Engine::Math::UniformFixed(game.random, Fixed{}, Fixed::One());
	FixedVector3 at{along.x * a + across.x * b + bridge.fromLeft.x, along.y * a + across.y * b + bridge.fromLeft.y,
		along.z * a + across.z * b + bridge.fromLeft.z};
	at.z += Engine::Math::UniformFixed(game.random, Fixed{}, bridgeTemplate.transitionEffectsHeight);
	return at;
}

// The bridge's deck (its BridgeInfo corners) for the pathfinding.
inline engine::gameplay::DeckGeometry DeckOf(const Bridge &bridge)
{
	return {bridge.from, bridge.to, bridge.fromLeft, bridge.fromRight, bridge.toLeft, bridge.toRight};
}

// TerrainLogic::addBridgeToLogic's Pathfinder::addBridge: the bridge's deck a pathfinding layer of its own (none left: it
// stays without), broken already if it is rubble.
inline void RegisterDeck(GameWorld &game, ecs::Entity object, Bridge &bridge)
{
	auto &grid = game.world.Resource<engine::gameplay::NavigationGrid>();
	bridge.layer = engine::gameplay::AddDeck(grid, game.ground, object, DeckOf(bridge), bridge.curDamageState == body_state::Rubble).value_or(0);
	// Its surface, at the same layer, for what stands on it.
	if (bridge.layer != 0)
		game.world.Resource<engine::gameplay::DeckSurfaces>().decks.push_back(DeckOf(bridge));
}

// A checkpoint's bridges given their decks again, in the order they were given them (their layers), on a grid of the
// terrain alone (before the obstacles are stamped back, as they were when the game began).
inline void RegisterBridgeDecks(GameWorld &game)
{
	std::vector<std::pair<std::uint8_t, ecs::Entity>> decks;
	ecs::Query<ecs::Read<Bridge>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto bridges = chunk.template Get<Bridge>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < bridges.size(); ++row)
			if (bridges[row].layer != 0)
				decks.emplace_back(bridges[row].layer, entities[row]);
	});
	std::sort(decks.begin(), decks.end(), [](const auto &left, const auto &right) { return left.first < right.first; });
	game.world.Resource<engine::gameplay::DeckSurfaces>().decks.clear();
	for (const auto &[layer, object] : decks)
		RegisterDeck(game, object, *game.world.Get<Bridge>(object));
}

// Every clearance plane afresh (the decks' and the ground's), after decks were made or changed.
inline void RebuildClearance(GameWorld &game)
{
	auto &grid = game.world.Resource<engine::gameplay::NavigationGrid>();
	for (engine::gameplay::ClearancePlane &plane : grid.Clearance())
		engine::gameplay::BuildClearance(grid, plane);
}

// TerrainLogic::objectInteractsWithBridgeLayer (the bridge's health aside): on one of its deck's clear cells (its ends
// and entries too) or over its surface, within a cell's height (10) of the deck.
inline bool InteractsWithDeck(const GameWorld &game, const Bridge &bridge, const Engine::Math::FixedVector3 &at)
{
	using Engine::Math::Fixed;
	const auto deck = DeckOf(bridge);
	bool on = engine::gameplay::PointOnDeck(deck, at.XY());
	const auto &grid = game.world.Resource<engine::gameplay::NavigationGrid>();
	if (!on && bridge.layer != 0 && bridge.layer <= grid.Decks().size())
	{
		const auto &cells = grid.Decks()[bridge.layer - 1];
		const std::int32_t x = static_cast<std::int32_t>((at.x / Fixed::FromInt(engine::gameplay::PathfindCellSize)).Floor());
		const std::int32_t y = static_cast<std::int32_t>((at.y / Fixed::FromInt(engine::gameplay::PathfindCellSize)).Floor());
		on = cells.Contains(x, y) && (cells.type[cells.Index(x, y)] == engine::gameplay::PathfindCellType::Clear || cells.toGround[cells.Index(x, y)] != 0);
	}
	return on && Engine::Math::Abs(at.z - engine::gameplay::DeckHeight(deck, at.XY())) <= Fixed::FromInt(10);
}

namespace bridge_detail
{
// Bridge::updateDamageState going to rubble: everything on its deck's layer on it takes HUGE_DAMAGE_AMOUNT of
// DAMAGE_FALLING, DEATH_SPLATTED, from itself (dealt with the next tick's damage).
inline void DropFromDeck(GameWorld &game, const Bridge &bridge)
{
	namespace gp = engine::gameplay;
	if (bridge.layer == 0)
		return;
	std::vector<ecs::Entity> falling;
	ecs::Query<ecs::Read<gp::SurfaceLayer>, ecs::Read<gp::Transform>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto layers = chunk.template Get<gp::SurfaceLayer>();
		const auto transforms = chunk.template Get<gp::Transform>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < layers.size(); ++row)
			if (layers[row].layer == bridge.layer && InteractsWithDeck(game, bridge, transforms[row].position))
				falling.push_back(entities[row]);
	});
	const std::uint32_t type = content::DamageTypeIndex("FALLING").value_or(0);
	const std::uint32_t death = content::DeathTypeIndex("SPLATTED").value_or(0);
	for (const ecs::Entity entity : falling)
	{
		if (game.world.Get<gp::Health>(entity) == nullptr)
			continue;
		if (!game.world.Has<gp::PendingDamage>(entity))
			game.world.Add<gp::PendingDamage>(entity);
		*game.world.Get<gp::PendingDamage>(entity) = gp::PendingDamage{entity, gp::HugeDamage(), type, death};
	}
}
}

// TerrainLogic::updateBridgeDamageStates (Bridge::updateDamageState for each): each bridge records its object's body
// state; a change into or out of rubble is its damageStateChanged (other changes clear it); every bridge marks the
// tick, the next tick's scripts seeing it (anyBridgesDamageStatesChanged). (A bridge's pathfinding layer and the
// things on it are the layers' work: none are in the port yet.)
inline void UpdateBridgeDamageStates(GameWorld &game)
{
	// Into rubble: its deck broken (Pathfinder::changeBridgeState) and what is on it dropped; out of it: mended.
	std::vector<std::pair<Bridge, bool>> changes;
	ecs::Query<ecs::Write<Bridge>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		for (Bridge &bridge : chunk.template Get<Bridge>())
		{
			bridge.changed = 0;
			if (bridge.bodyState != bridge.curDamageState)
			{
				if (bridge.bodyState == body_state::Rubble || bridge.curDamageState == body_state::Rubble)
				{
					bridge.changed = 1;
					changes.emplace_back(bridge, bridge.bodyState == body_state::Rubble);
				}
				bridge.curDamageState = bridge.bodyState;
			}
			bridge.statesUpdatedTick = game.tick;
		}
	});
	if (changes.empty())
		return;
	auto &grid = game.world.Resource<engine::gameplay::NavigationGrid>();
	for (const auto &[bridge, broken] : changes)
	{
		if (bridge.layer == 0)
			continue;
		engine::gameplay::SetDeckDestroyed(grid, game.ground, bridge.layer, broken);
		for (engine::gameplay::ClearancePlane &plane : grid.Clearance())
			engine::gameplay::BuildDeckClearance(grid, plane, bridge.layer);
		if (broken)
			bridge_detail::DropFromDeck(game, bridge);
	}
}

// Bridge::Bridge(BridgeInfo) with the map's properties (updateObjValuesFromMapProperties): a GenericBridge in the
// middle of the deck, facing from `from` to `to`.
inline ecs::Entity MakeBridgeObject(GameWorld &game, const Bridge &bridge, const engine::level::Properties &properties)
{
	namespace gp = engine::gameplay;
	const Engine::Math::FixedVector3 center{(bridge.fromLeft.x + bridge.toRight.x) / Engine::Math::Fixed::FromInt(2),
		(bridge.fromLeft.y + bridge.toRight.y) / Engine::Math::Fixed::FromInt(2), (bridge.fromLeft.z + bridge.toRight.z) / Engine::Math::Fixed::FromInt(2)};
	const Engine::Math::TurnAngle facing = Engine::Math::Atan2(bridge.toLeft.y - bridge.fromLeft.y, bridge.toLeft.x - bridge.fromLeft.x);
	const ecs::Entity object = SpawnObject(game, "GenericBridge", center.XY(), facing, bridge_detail::NeutralTeam(game),
		properties.Get<std::string>("objectName").value_or(""));
	if (!game.world.IsAlive(object))
		return {};
	game.world.Get<gp::Transform>(object)->position = center;
	game.world.Add<Bridge>(object);
	*game.world.Get<Bridge>(object) = bridge;
	RegisterDeck(game, object, *game.world.Get<Bridge>(object));
	ApplyMapProperties(game, object, properties);
	return object;
}

// W3DBridgeBuffer::MAX_BRIDGES: the bridge buffer's room; addBridge makes no more once that many were made.
inline constexpr std::size_t MaxMapBridges = 200;

// W3DBridgeBuffer::loadBridges: each BRIDGE_POINT1 map object followed by a BRIDGE_POINT2 makes a bridge of the Roads.ini
// Bridge it names (none found, or its pristine model missing: no bridge; MaxMapBridges made already: no more), its ends
// BRIDGE_FLOAT_AMT above the height map, as wide as its model's BRIDGE_LEFT times its BridgeScale
// (W3DBridge::getBridgeInfo); then the damage states.
inline void PlaceBridges(GameWorld &game)
{
	using namespace bridge_detail;
	const auto &placements = game.level.placements;
	std::size_t made = 0;
	for (std::size_t index = 0; index < placements.size(); ++index)
	{
		const auto &first = placements[index];
		if ((first.flags & BridgePoint1) == 0)
			continue;
		if (index + 1 >= placements.size())
			break;
		const auto &second = placements[index + 1];
		if ((second.flags & BridgePoint2) == 0)
			continue;
		++index;
		const auto &catalog = game.templates.Content().bridges;
		const auto found = catalog.find(first.type);
		if (found == catalog.end() || !found->second.extentY)
			continue;
		if (made >= MaxMapBridges)
			continue;
		++made;
		const content::BridgeContent &kind = found->second;
		Bridge bridge;
		bridge.from = {first.position.x, first.position.y, game.ground.At(first.position.XY()) + FloatAmount()};
		bridge.to = {second.position.x, second.position.y, game.ground.At(second.position.XY()) + FloatAmount()};
		const auto [minY, maxY] = *kind.extentY;
		bridge.width = (maxY - minY) * kind.scale;
		FixedVector3 normal{bridge.from.y - bridge.to.y, bridge.to.x - bridge.from.x, {}};
		if (const Fixed length = Engine::Math::Length(normal); length > Fixed{})
			normal = {normal.x / length, normal.y / length, {}};
		const auto offset = [&](const FixedVector3 &end, Fixed y) {
			return FixedVector3{end.x + normal.x * y * kind.scale, end.y + normal.y * y * kind.scale, end.z};
		};
		bridge.fromLeft = offset(bridge.from, maxY);
		bridge.fromRight = offset(bridge.from, minY);
		bridge.toLeft = offset(bridge.to, maxY);
		bridge.toRight = offset(bridge.to, minY);
		bridge.bridgeTemplate = static_cast<std::uint32_t>(std::distance(catalog.begin(), found));
		MakeBridgeObject(game, bridge, first.properties);
	}
	UpdateBridgeDamageStates(game);
}

// Bridge::Bridge(Object *) and createTower: a landmark bridge (IsBridge, a BOX) covers its box at its height; its
// Roads.ini Bridge of the same name gives it four towers at its corners, each pushed out across the deck by its own
// major radius (a missing one: the last one's), on the bridge's team, facing along it (the `from` two turned about),
// indestructible when it is.
inline void AddLandmarkBridge(GameWorld &game, ecs::Entity object)
{
	using namespace bridge_detail;
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const content::ObjectDefinition *kind = DefinitionOf(game, object);
	if (kind == nullptr)
		return;
	const gp::Transform at = *world.Get<gp::Transform>(object);
	const Fixed halfX = kind->geometry.majorRadius, halfY = kind->geometry.minorRadius;
	const Fixed c = Engine::Math::Cos(at.facing), s = Engine::Math::Sin(at.facing);
	const auto &p = at.position;
	Bridge bridge;
	bridge.landmark = 1;
	bridge.width = halfY * Fixed::FromInt(2);
	bridge.fromLeft = {p.x - halfX * c - halfY * s, p.y + halfY * c - halfX * s, p.z};
	bridge.toLeft = {p.x + halfX * c - halfY * s, p.y + halfY * c + halfX * s, p.z};
	bridge.fromRight = {p.x - halfX * c + halfY * s, p.y - halfY * c - halfX * s, p.z};
	bridge.toRight = {p.x + halfX * c + halfY * s, p.y - halfY * c + halfX * s, p.z};
	const Fixed two = Fixed::FromInt(2);
	bridge.from = {(bridge.fromLeft.x + bridge.fromRight.x) / two, (bridge.fromLeft.y + bridge.fromRight.y) / two, (bridge.fromLeft.z + bridge.fromRight.z) / two};
	bridge.to = {(bridge.toLeft.x + bridge.toRight.x) / two, (bridge.toLeft.y + bridge.toRight.y) / two, (bridge.toLeft.z + bridge.toRight.z) / two};
	bridge.bridgeTemplate = BridgeTemplateIndex(game, kind->name);
	if (const content::BridgeContent *roads = BridgeTemplateOf(game, bridge))
	{
		FixedVector2 across{bridge.toLeft.x - bridge.toRight.x, bridge.toLeft.y - bridge.toRight.y};
		if (const Fixed length = Engine::Math::Length(across); length > Fixed{})
			across = {across.x / length, across.y / length};
		const std::array<FixedVector3, 4> corners{bridge.fromLeft, bridge.fromRight, bridge.toLeft, bridge.toRight};
		Fixed offset = Fixed::FromInt(5); // PATHFIND_CELL_SIZE_F / 2
		const auto *member = world.Get<gp::TeamMember>(object);
		const auto *body = world.Get<gp::Health>(object);
		const bool indestructible = body != nullptr && body->indestructible;
		for (std::size_t type = 0; type < corners.size(); ++type)
		{
			const content::ObjectDefinition *towerKind = game.templates.Content().objects.Find(roads->towers[type]);
			if (towerKind == nullptr)
				continue; // createTower: no template, no tower
			offset = towerKind->geometry.majorRadius;
			FixedVector3 position = corners[type];
			const bool left = type == 0 || type == 2;
			position.x += left ? across.x * offset : -across.x * offset;
			position.y += left ? across.y * offset : -across.y * offset;
			const Engine::Math::TurnAngle facing = type < 2 ? at.facing + Engine::Math::TurnAngle{0x80000000u} : at.facing;
			const ecs::Entity tower = SpawnObject(game, roads->towers[type], position.XY(), facing, member != nullptr ? member->team : 0u, "");
			if (!world.IsAlive(tower))
				continue;
			world.Get<gp::Transform>(tower)->position = position;
			world.Add<BridgeTower>(tower);
			*world.Get<BridgeTower>(tower) = BridgeTower{object, static_cast<std::uint8_t>(type)};
			if (auto *health = world.Get<gp::Health>(tower); health != nullptr && indestructible)
				health->indestructible = true;
			bridge.towers[type] = tower;
		}
	}
	world.Add<Bridge>(object);
	*world.Get<Bridge>(object) = bridge;
	RegisterDeck(game, object, *world.Get<Bridge>(object));
}

// GameLogic::startNewGame's bridge pass, before every other map object: bridge-like things (IsBridge, or
// WALK_ON_TOP_OF_WALL) on the neutral player's default team, raised by the ground they stand on, a landmark bridge
// registered (addLandmarkBridgeToLogic) before the map's properties are applied. Whether the placement was one.
inline bool PlaceBridgeLike(GameWorld &game, const engine::level::Placement &placement)
{
	using namespace bridge_detail;
	namespace gp = engine::gameplay;
	const content::ObjectDefinition *kind = game.templates.Content().objects.Find(placement.type);
	if (kind == nullptr || (!kind->isBridge && !kind->Is("WALK_ON_TOP_OF_WALL")))
		return false;
	const ecs::Entity object = SpawnObject(game, placement.type, placement.position.XY(), placement.orientation, NeutralTeam(game),
		placement.properties.Get<std::string>("objectName").value_or(""));
	if (!game.world.IsAlive(object))
		return true;
	game.world.Get<gp::Transform>(object)->position.z = placement.position.z + game.ground.At(placement.position.XY());
	if (kind->isBridge)
		AddLandmarkBridge(game, object);
	// Pathfinder::addWallPiece.
	if (kind->Is("WALK_ON_TOP_OF_WALL"))
		AddWallPiece(game, object, *kind);
	ApplyMapProperties(game, object, placement.properties);
	return true;
}

// The bridge-like things go before everything else (their pass comes first; the main loop passes them over).
inline void PlaceBridgeLikeObjects(GameWorld &game)
{
	for (const auto &placement : game.level.placements)
		if ((placement.flags & (bridge_detail::BridgePoint1 | bridge_detail::BridgePoint2 | 0x6u)) == 0)
			PlaceBridgeLike(game, placement);
}

inline bool IsBridgeLike(const GameWorld &game, std::string_view type)
{
	const content::ObjectDefinition *kind = game.templates.Content().objects.Find(type);
	return kind != nullptr && (kind->isBridge || kind->Is("WALK_ON_TOP_OF_WALL"));
}

namespace bridge_detail
{
// BridgeBehavior::doAreaEffects: NumFXPerType times, the FX at a random point of the deck, then the OCL at another.
inline void AreaEffects(GameWorld &game, ecs::Entity object, const Bridge &bridge, const content::BridgeContent &kind, const std::string &ocl,
	const std::string &fx)
{
	namespace gp = engine::gameplay;
	if (ocl.empty() && fx.empty())
		return;
	auto *cues = game.world.FindResource<BridgeCues>();
	const auto *member = game.world.Get<gp::TeamMember>(object);
	const auto facing = game.world.Get<gp::Transform>(object)->facing;
	for (std::uint32_t index = 0; index < kind.fxPerType; ++index)
	{
		if (!fx.empty())
		{
			const FixedVector3 at = RandomSurfacePosition(game, bridge, kind);
			if (cues != nullptr)
				cues->list.push_back({fx, at, false});
		}
		if (!ocl.empty())
		{
			const FixedVector3 at = RandomSurfacePosition(game, bridge, kind);
			RunCreationList(game, ocl, {at, facing, member != nullptr ? member->team : 0xFFFFFFFFu, object});
		}
	}
}
}

// The tick's BridgeEvents, in order, then the bridges that died this tick:
// - onBodyDamageStateChange: its transition's sound (to DAMAGED only: DamagedToSound or RepairedToSound) and area
//   effects (Roads.ini's TransitionToOCL / TransitionToFX for the state it went to, damage or repair: better than it was
//   is a repair), then updateBridgeDamageStates (none of it without its Roads.ini template: resolveFX failed);
// - a BridgeDieFX / BridgeDieOCL due: at its bone (the model at rest, turned and moved as the bridge is), from the
//   bridge itself (ParentObject), or at a random point of the deck;
// - onDie: its towers killed, its death noted, and its BridgeDie effects of no delay at once. (The things on it fall:
//   handleObjectsOnBridgeOnDie is the layers' work, none are in the port yet.)
inline void ApplyBridgeEvents(GameWorld &game, std::span<const engine::gameplay::Casualty> casualties)
{
	namespace gp = engine::gameplay;
	using namespace bridge_detail;
	auto &world = game.world;
	std::vector<BridgeEvent> events;
	if (auto *resource = world.FindResource<BridgeEvents>())
	{
		resource->AppendTo(events);
		resource->Reset(0);
	}
	const auto dieEffect = [&](ecs::Entity object, const Bridge &bridge, const content::BridgeDieEffect &effect) {
		const gp::Transform &at = *world.Get<gp::Transform>(object);
		const content::BridgeContent *kind = BridgeTemplateOf(game, bridge);
		FixedVector3 where = at.position;
		if (effect.where == content::BridgeDieEffect::Where::Bone)
		{
			const Fixed c = Engine::Math::Cos(at.facing), s = Engine::Math::Sin(at.facing);
			where = {at.position.x + effect.bone.x * c - effect.bone.y * s, at.position.y + effect.bone.x * s + effect.bone.y * c, at.position.z + effect.bone.z};
		}
		else if (effect.where == content::BridgeDieEffect::Where::Surface && kind != nullptr)
			where = RandomSurfacePosition(game, bridge, *kind);
		if (!effect.creationList)
		{
			if (auto *cues = world.FindResource<BridgeCues>())
				cues->list.push_back({effect.name, where, false});
			return;
		}
		const auto *member = world.Get<gp::TeamMember>(object);
		RunCreationList(game, effect.name, {where, at.facing, member != nullptr ? member->team : 0xFFFFFFFFu, object});
	};
	const auto effectsOf = [&](ecs::Entity object) -> const std::vector<content::BridgeDieEffect> * {
		const content::ObjectDefinition *kind = DefinitionOf(game, object);
		if (kind == nullptr)
			return nullptr;
		const auto found = game.templates.Content().bridgeDieEffects.find(kind->name);
		return found != game.templates.Content().bridgeDieEffects.end() ? &found->second : nullptr;
	};
	// onBodyDamageStateChange.
	const auto stateChange = [&](ecs::Entity object, std::uint8_t from, std::uint8_t to) {
		const Bridge bridge = *world.Get<Bridge>(object);
		const content::BridgeContent *kind = BridgeTemplateOf(game, bridge);
		if (kind == nullptr)
			return;
		const bool repaired = from > to;
		if (to == body_state::Damaged)
		{
			const std::string &sound = repaired ? kind->repairedSound : kind->damagedSound;
			if (!sound.empty())
				if (auto *cues = world.FindResource<BridgeCues>())
					cues->list.push_back({sound, world.Get<gp::Transform>(object)->position, true});
		}
		for (std::size_t slot = 0; slot < content::BridgeBodyEffects; ++slot)
			AreaEffects(game, object, bridge, *kind, (repaired ? kind->repairOcl : kind->damageOcl)[to][slot], (repaired ? kind->repairFx : kind->damageFx)[to][slot]);
		UpdateBridgeDamageStates(game);
	};
	for (const BridgeEvent &event : events)
	{
		if (!world.IsAlive(event.bridge) || world.Get<Bridge>(event.bridge) == nullptr)
			continue;
		if (event.kind == BridgeEvent::Kind::DieEffect)
		{
			if (const auto *effects = effectsOf(event.bridge); effects != nullptr && event.effect < effects->size())
				dieEffect(event.bridge, *world.Get<Bridge>(event.bridge), (*effects)[event.effect]);
			continue;
		}
		stateChange(event.bridge, event.from, event.to);
	}
	for (const gp::Casualty &casualty : casualties)
	{
		if (casualty.departure != gp::Departure::Killed || !world.IsAlive(casualty.entity))
			continue;
		auto *bridge = world.Get<Bridge>(casualty.entity);
		if (bridge == nullptr)
			continue;
		// Dying is rubble, and the original's body turns to rubble as the killing blow lands, before onDie: its state
		// changes now (the system sees it dying from the next tick, the state already heard).
		if (bridge->bodyState != body_state::Rubble)
		{
			const std::uint8_t from = bridge->bodyState;
			bridge->bodyState = body_state::Rubble;
			stateChange(casualty.entity, from, body_state::Rubble);
			bridge = world.Get<Bridge>(casualty.entity);
		}
		for (const ecs::Entity tower : bridge->towers)
			KillNow(game, tower);
		bridge->deathTick = game.tick;
		const Bridge copy = *bridge;
		if (const auto *effects = effectsOf(casualty.entity))
			for (const content::BridgeDieEffect &effect : *effects)
				if (effect.delayTicks == 0)
					dieEffect(casualty.entity, copy, effect);
	}
}

// TerrainLogic::isBridgeBroken / isBridgeRepaired for the scripts (BRIDGE_BROKEN / BRIDGE_REPAIRED): only while the
// damage states were last recorded in the tick before this one (anyBridgesDamageStatesChanged, cleared once the
// scripts have run), that record's change into rubble, or out of it.
inline bool BridgeBroken(const GameWorld &game, ecs::Entity object, bool broken)
{
	const Bridge *bridge = game.world.IsAlive(object) ? game.world.Get<Bridge>(object) : nullptr;
	if (bridge == nullptr || bridge->statesUpdatedTick == Bridge::Never || bridge->statesUpdatedTick + 1 != game.tick)
		return false;
	return bridge->changed != 0 && (bridge->curDamageState == body_state::Rubble) == broken;
}
}
