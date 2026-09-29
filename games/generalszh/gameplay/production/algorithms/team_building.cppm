export module games.generalszh.gameplay.production.algorithms.team_building;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import engine.gameplay.rts.navigation.components.ignored_obstacle;
import std;
import engine.gameplay.rts.production.components.rally_point;
import engine.gameplay.rts.movement.components.move_path;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.content.production.production_content;
import engine.gameplay.rts.production.systems.production_system;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.rts.movement.components.move_order;
import engine.ecs.query.query;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.rts.aircraft.components.airfield;
import games.generalszh.gameplay.aircraft.algorithms.airfields;
import engine.gameplay.rts.aircraft.components.jet;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.rts.harvesting.components.harvester;
import Engine.Core.Math.FixedAngle;

// Units out of their factories (ProductionUpdate's exit), prerequisites, and spawners' spawns.
import engine.gameplay.rts.slaves.components.spawner;
import engine.gameplay.common.identity.components.team_member;
import games.generalszh.gameplay.ai.components.mob_member;
export import engine.gameplay.rts.slaves.resources.spawn_requests;
export namespace generalszh::gameplay
{
// exitObjectViaDoor's exit path (aiFollowExitProductionPath): out to the natural rally point, then on to the rally point
// its player set, if one (m_rallyPointExists).
inline void FollowOnToRallyPoint(GameWorld &game, ecs::Entity unit, ecs::Entity factory, Engine::Math::FixedVector2 natural)
{
	namespace gp = engine::gameplay;
	const auto *rally = game.world.IsAlive(factory) ? game.world.Get<gp::RallyPoint>(factory) : nullptr;
	if (rally == nullptr || !game.world.IsAlive(unit))
		return;
	gp::MovePath path;
	path.count = 2;
	path.next = 1; // the first leg is under way
	path.points[0] = natural;
	path.points[1] = rally->at;
	if (!game.world.Has<gp::MovePath>(unit))
		game.world.Add<gp::MovePath>(unit);
	*game.world.Get<gp::MovePath>(unit) = path;
}

namespace team_building_detail
{
namespace gameplay = engine::gameplay;
using Engine::Math::Fixed;

Engine::Math::FixedVector2 InFrameOf(const gameplay::Transform &frame, const Engine::Math::FixedVector3 &local)
{
	const Fixed c = Engine::Math::Cos(frame.facing), s = Engine::Math::Sin(frame.facing);
	return {frame.position.x + local.x * c - local.y * s, frame.position.y + local.x * s + local.y * c};
}
}

// What each player owns (alive, not dying), by definition name: for prerequisites.
std::set<std::pair<std::uint32_t, std::string_view>> OwnedObjects(GameWorld &game)
{
	namespace gameplay = engine::gameplay;
	std::set<std::pair<std::uint32_t, std::string_view>> owned;
	ecs::Query<ecs::Read<gameplay::Owner>, ecs::Read<gameplay::DefinitionRef>, ecs::Exclude<gameplay::Dying>> things(game.world);
	things.ForEachChunk([&](auto chunk) {
		const auto owners = chunk.template Get<gameplay::Owner>();
		const auto definitions = chunk.template Get<gameplay::DefinitionRef>();
		for (std::size_t row = 0; row < owners.size(); ++row)
			owned.emplace(owners[row].player, std::string_view(game.templates.DefinitionAt(definitions[row].index).name));
	});
	return owned;
}

// Whether `player` has what `unit` requires (the original's
// ProductionPrerequisite): one of each object group. Sciences are not
// ported yet (no player has any), so they are not checked.
bool PrerequisitesMet(const std::set<std::pair<std::uint32_t, std::string_view>> &owned, std::uint32_t player, const content::ObjectDefinition &unit)
{
	return std::all_of(unit.prerequisiteObjects.begin(), unit.prerequisiteObjects.end(), [&](const std::vector<std::string> &group) {
		return std::any_of(group.begin(), group.end(), [&](const std::string &name) { return owned.contains({player, std::string_view(name)}); });
	});
}

// Brings produced units out of their factory; returns them (for Player::onUnitCreated: a computer player's orders).
std::vector<ecs::Entity> OnProduced(GameWorld &game, const engine::gameplay::Produced &produced)
{
	namespace gameplay = engine::gameplay;
	using namespace team_building_detail;
	std::vector<ecs::Entity> out;
	if (!game.world.IsAlive(produced.factory))
		return out;
	const gameplay::Transform frame = *game.world.Get<gameplay::Transform>(produced.factory);
	const content::ObjectDefinition &factory = game.templates.DefinitionAt(game.world.Get<gameplay::DefinitionRef>(produced.factory)->index);
	const auto production = content::ReadObjectProduction(factory, game.step);
	const content::ObjectDefinition &unit = game.templates.DefinitionAt(produced.definition);
	for (std::uint32_t index = 0; index < produced.quantity; ++index)
	{
		// Out of the factory's exit (or its middle, until parking places are ported), on to its rally point.
		const auto at = production && production->hasExit ? InFrameOf(frame, production->createPoint) : frame.position.XY();
		const ecs::Entity entity = SpawnObject(game, unit.name, at, frame.facing, produced.team, "");
		if (game.world.IsAlive(entity))
		{
			out.push_back(entity);
			SetProducer(game, entity, produced.factory); // ProductionUpdate::update: setProducer(creationBuilding)
			OnBuildComplete(game, entity); // ProductionUpdate: the game side of its create modules
		}
		// A jet made at an airfield starts in the hangar of a free space and taxies to it.
		if (gameplay::Jet *jet = game.world.IsAlive(entity) ? game.world.Get<gameplay::Jet>(entity) : nullptr)
			if (const auto *field = game.world.Get<gameplay::Airfield>(produced.factory))
				if (const auto space = FreeSpace(game, produced.factory))
				{
					const gameplay::ParkingSpace &spot = field->spaces[*space];
					auto &transform = *game.world.Get<gameplay::Transform>(entity);
					transform.position = spot.hangar;
					transform.facing = spot.hangarFacing;
					jet->airfield = produced.factory;
					jet->space = *space;
					jet->state = gameplay::JetState::TaxiToParking;
					jet->since = game.tick;
					jet->leg = 1;
					jet->goal = spot.parking.XY();
					if (auto *motion = game.world.Get<gameplay::Locomotion>(entity))
						motion->locomotor = jet->taxi;
					continue;
				}
		// aiFollowExitProductionPath: it walks out through the factory (ignoreObstacle) to its rally point.
		if (production && production->hasExit && game.world.IsAlive(entity))
			if (auto *order = game.world.Get<gameplay::MoveOrder>(entity))
			{
				*order = gameplay::MoveToPoint(InFrameOf(frame, production->rallyPoint));
				FollowOnToRallyPoint(game, entity, produced.factory, InFrameOf(frame, production->rallyPoint));
				if (!game.world.Has<gameplay::IgnoredObstacle>(entity))
					game.world.Add<gameplay::IgnoredObstacle>(entity);
				game.world.Get<gameplay::IgnoredObstacle>(entity)->obstacle = produced.factory;
			}
		// SupplyCenterProductionExitUpdate: a supply truck out of a supply centre goes harvesting (setForceWantingState).
		if (production && production->supplyExit && game.world.IsAlive(entity))
			if (auto *harvester = game.world.Get<gameplay::Harvester>(entity))
				harvester->forceWanting = true;
	}
	return out;
}

// SpawnBehavior::createSpawn: a spawner's spawn made on its team and brought out of its exit (exitObjectViaDoor:
// the create point, then on to the rally point; a supply centre's truck goes harvesting), then counted as its.
ecs::Entity SpawnFrom(GameWorld &game, const engine::gameplay::SpawnRequest &request)
{
	namespace gameplay = engine::gameplay;
	using namespace team_building_detail;
	auto &world = game.world;
	if (!world.IsAlive(request.spawner))
		return {};
	gameplay::Spawner *spawner = world.Get<gameplay::Spawner>(request.spawner);
	if (spawner == nullptr || spawner->spawnedCount >= gameplay::Spawner::MaxSpawns)
		return {};
	const gameplay::Transform frame = *world.Get<gameplay::Transform>(request.spawner);
	const content::ObjectDefinition &source = game.templates.DefinitionAt(world.Get<gameplay::DefinitionRef>(request.spawner)->index);
	const auto production = content::ReadObjectProduction(source, game.step);
	const content::ObjectDefinition &unit = game.templates.DefinitionAt(request.definition);
	const std::uint32_t team = world.Get<gameplay::TeamMember>(request.spawner)->team;
	auto at = production && production->hasExit ? InFrameOf(frame, production->createPoint) : frame.position.XY();
	auto facing = frame.facing;
	// ExitByBudding (exitObjectByBudding): on the spawn nearest the spawner (none: the spawner), then off it.
	const bool budding = spawner->budding;
	if (budding)
	{
		at = frame.position.XY();
		Engine::Math::Fixed nearest;
		bool found = false;
		for (std::size_t index = 0; index < spawner->spawnedCount; ++index)
			if (const auto *host = world.IsAlive(spawner->spawned[index]) ? world.Get<gameplay::Transform>(spawner->spawned[index]) : nullptr)
			{
				const Engine::Math::Fixed distance = Engine::Math::DistanceSquared(host->position.XY(), frame.position.XY());
				if (!found || distance < nearest)
				{
					nearest = distance;
					at = host->position.XY();
					facing = host->facing;
					found = true;
				}
			}
	}
	const ecs::Entity entity = SpawnObject(game, unit.name, at, facing, team, "");
	if (!world.IsAlive(entity))
		return {};
	SetProducer(game, entity, request.spawner); // createSpawn: newSpawn->setProducer(parent)
	// SlavedUpdateInterface::onEnslave: a mob member knows its nexus.
	if (auto *member = world.Get<MobMember>(entity))
		member->nexus = request.spawner;
	spawner = world.Get<gameplay::Spawner>(request.spawner);
	spawner->spawned[spawner->spawnedCount++] = entity;
	if (budding)
	{
		if (auto *order = world.Get<gameplay::MoveOrder>(entity))
			*order = gameplay::MoveToPoint(at); // aiMoveToPosition: it cannot stay where another is
	}
	else if (production && production->hasExit)
		if (auto *order = world.Get<gameplay::MoveOrder>(entity))
		{
			*order = gameplay::MoveToPoint(InFrameOf(frame, production->rallyPoint));
			FollowOnToRallyPoint(game, entity, request.spawner, InFrameOf(frame, production->rallyPoint));
			world.Add<gameplay::IgnoredObstacle>(entity);
			world.Get<gameplay::IgnoredObstacle>(entity)->obstacle = request.spawner; // it walks out through its spawner
		}
	if (production && production->supplyExit)
		if (auto *harvester = world.Get<gameplay::Harvester>(entity))
			harvester->forceWanting = true;
	return entity;
}
}
