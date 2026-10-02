export module games.generalszh.gameplay.production.algorithms.team_building;
import engine.gameplay.common.random.resources.random_seed;
import engine.gameplay.rts.slaves.algorithms.enslave;
import engine.gameplay.rts.slaves.components.spawn_points;
import engine.gameplay.common.status.algorithms.disable_now;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import engine.gameplay.rts.navigation.components.ignored_obstacle;
import std;
import engine.gameplay.rts.production.components.rally_point;
import engine.gameplay.rts.movement.algorithms.move_paths;
import engine.gameplay.rts.navigation.components.navigation;
import games.generalszh.gameplay.movement.algorithms.goal_claim_rules;

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
import games.generalszh.gameplay.stealth.algorithms.supply_stealth;
export namespace generalszh::gameplay
{
// exitObjectViaDoor's exit path (aiFollowExitProductionPath): out to the natural rally point, then on to the rally point
// its player set (m_rallyPointExists), adjusted off others' claims (adjustDestination) for a ground mover; a rally point
// nothing near will do, or a mover not on the ground, leaves the natural rally point the path's only point. A leg with
// more after it claims no goal; a lone natural rally point is claimed as it is once its route is planned
// (AIFollowPathState: AI_FOLLOW_EXITPRODUCTION_PATH adjusts nothing on entering, then claims its path's end).
inline void FollowOnToRallyPoint(GameWorld &game, ecs::Entity unit, ecs::Entity factory, Engine::Math::FixedVector2 natural)
{
	namespace gp = engine::gameplay;
	if (!game.world.IsAlive(unit))
		return;
	auto *order = game.world.Get<gp::MoveOrder>(unit);
	if (order != nullptr)
		order->claim = gp::GoalClaim::Keep;
	const auto *rally = game.world.IsAlive(factory) ? game.world.Get<gp::RallyPoint>(factory) : nullptr;
	if (rally == nullptr || !game.world.Has<gp::NavigationAgent>(unit))
		return;
	const auto point = AdjustDestinationFor(game.world, unit, rally->at);
	if (!point)
		return;
	// The first leg is under way.
	const std::array<Engine::Math::FixedVector2, 2> route{natural, *point};
	gp::SetMovePath(game.world, unit, route, 1, gp::MovePathKind::ExitProduction);
	if (order != nullptr)
		order->claim = gp::GoalClaim::None;
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

// DefaultProductionExitUpdate::exitObjectViaDoor for `unit` out of `building`: set at its UnitCreatePoint on the ground,
// facing its way, on to its natural rally point and its rally point if it has one. False: it has no such exit.
inline bool ExitViaProductionDoor(GameWorld &game, ecs::Entity building, ecs::Entity unit)
{
	namespace gameplay = engine::gameplay;
	auto &world = game.world;
	const auto *ref = world.IsAlive(building) ? world.Get<gameplay::DefinitionRef>(building) : nullptr;
	const auto *frame = ref != nullptr ? world.Get<gameplay::Transform>(building) : nullptr;
	if (frame == nullptr || !world.IsAlive(unit))
		return false;
	const auto exit = content::ReadProductionExit(game.templates.DefinitionAt(ref->index));
	if (!exit)
		return false;
	using namespace team_building_detail;
	const Engine::Math::FixedVector2 at = InFrameOf(*frame, exit->createPoint);
	if (auto *transform = world.Get<gameplay::Transform>(unit))
		*transform = gameplay::Transform{{at.x, at.y, game.ground.At(at)}, frame->facing};
	const Engine::Math::FixedVector2 natural = InFrameOf(*frame, exit->rallyPoint);
	if (auto *order = world.Get<gameplay::MoveOrder>(unit))
		*order = gameplay::Replanned(gameplay::MoveToPoint(natural));
	FollowOnToRallyPoint(game, unit, building, natural);
	return true;
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
			CreateModulesBuildComplete(game, entity);
		}
		// A helicopter made at an airfield (exitObjectViaDoor, PRODUCED_AT_HELIPAD: no door, no space) appears on its helipad
		// (HeliPark01, facing its turn) flying (getProducerLocation: no space reserved, so air locomotion), and goes to the
		// airfield's rally point if it has one (m_heliRallyPoint), else to where it is (aiMoveToPosition).
		if (gameplay::Jet *heli = game.world.IsAlive(entity) ? game.world.Get<gameplay::Jet>(entity) : nullptr; heli != nullptr && heli->helicopter != 0)
			if (const auto *field = game.world.Get<gameplay::Airfield>(produced.factory); field != nullptr && field->hasHelipad != 0)
			{
				auto &transform = *game.world.Get<gameplay::Transform>(entity);
				transform.position = field->helipad;
				transform.facing = field->helipadFacing;
				heli->airfield = produced.factory;
				if (auto *motion = game.world.Get<gameplay::Locomotion>(entity))
					motion->locomotor = heli->flight;
				const auto *rally = game.world.Get<gameplay::RallyPoint>(produced.factory);
				if (auto *order = game.world.Get<gameplay::MoveOrder>(entity))
					*order = gameplay::Replanned(gameplay::MoveToPoint(rally != nullptr ? rally->at : field->helipad.XY()));
				continue;
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
					// TAXI_FROM_HANGAR at an airfield: to its space (then it turns to the space's turn and reloads).
					jet->path[0] = gameplay::ParkingSpot(spot, jet->parkingOffset);
					jet->pathCount = 1;
					if (field->frontRow == 0)
						jet->leg = 0;
					// Off a flight deck's hangar (FlightDeckBehavior::exitObjectViaDoor): out by its runway's creation points,
					// then to its prep point (aiFollowExitProductionPath: TAXI_FROM_HANGAR).
					if (field->frontRow != 0 && spot.runway < field->runwayCount)
					{
						const gameplay::RunwayPath &runway = field->runways[spot.runway];
						jet->route = 1;
						jet->leg = 0;
						jet->goal = runway.creationCount > 1 ? runway.creation[1].XY() : spot.prep.XY();
					}
					if (auto *motion = game.world.Get<gameplay::Locomotion>(entity))
						motion->locomotor = jet->taxi;
					continue;
				}
		// aiFollowExitProductionPath: it walks out through the factory (ignoreObstacle) to its rally point.
		if (production && production->hasExit && game.world.IsAlive(entity))
			if (auto *order = game.world.Get<gameplay::MoveOrder>(entity))
			{
				*order = gameplay::Replanned(gameplay::MoveToPoint(InFrameOf(frame, production->rallyPoint)));
				FollowOnToRallyPoint(game, entity, produced.factory, InFrameOf(frame, production->rallyPoint));
				if (!game.world.Has<gameplay::IgnoredObstacle>(entity))
					game.world.Add<gameplay::IgnoredObstacle>(entity);
				game.world.Get<gameplay::IgnoredObstacle>(entity)->obstacle = produced.factory;
			}
		// SupplyCenterProductionExitUpdate: a supply truck out of a supply centre goes harvesting (setForceWantingState).
		if (production && production->supplyExit && game.world.IsAlive(entity))
		{
			if (auto *harvester = game.world.Get<gameplay::Harvester>(entity))
				harvester->forceWanting = true;
			GrantExitStealth(game, produced.factory, entity, production->exitStealthTicks);
		}
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
	// createSpawn's m_initialBurstCountdown: while a budding spawner's initial burst lasts, a spawn leaves through the
	// door of the structure that produced the spawner (a mob out of its barracks: exitObjectViaDoor), when that is
	// still there with an exit; else it buds.
	ecs::Entity barracks{};
	if (spawner->budding && spawner->initialBurstLeft > 0)
		if (const auto *producer = world.Get<gameplay::Producer>(request.spawner); producer != nullptr && world.IsAlive(producer->entity))
			if (const auto *ref = world.Get<gameplay::DefinitionRef>(producer->entity); ref != nullptr)
				if (const content::ObjectDefinition &building = game.templates.DefinitionAt(ref->index);
					building.Is("STRUCTURE") && content::ReadProductionExit(building))
					barracks = producer->entity;
	// ExitByBudding (exitObjectByBudding): on the spawn nearest the spawner (none: the spawner), then off it.
	const bool budding = spawner->budding && !world.IsAlive(barracks);
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
	// SpawnPointProductionExitUpdate::exitObjectViaDoor: on the first free place (its bone in the spawner's frame), facing
	// its way; held there for good.
	std::optional<std::uint32_t> place;
	const std::vector<content::RestBone> *places = game.templates.SpawnPointsOf(world.Get<gameplay::DefinitionRef>(request.spawner)->index);
	if (const auto *points = world.Get<gameplay::SpawnPoints>(request.spawner); points != nullptr && places != nullptr)
	{
		place = points->Free();
		if (!place || *place >= places->size())
			return {};
		at = InFrameOf(frame, (*places)[*place].position);
		facing = frame.facing + (*places)[*place].facing;
	}
	const ecs::Entity entity = SpawnObject(game, unit.name, at, facing, team, "");
	if (!world.IsAlive(entity))
		return {};
	SetProducer(game, entity, request.spawner); // createSpawn: newSpawn->setProducer(parent)
	if (place)
	{
		world.Get<gameplay::SpawnPoints>(request.spawner)->occupier[*place] = entity;
		gameplay::DisableNow(world, entity, gameplay::disabled_type::Held, gameplay::DisabledForever);
	}
	// SlavedUpdateInterface::onEnslave: a mob member knows its nexus; a SlavedUpdate's slave (a Stinger Site's soldier)
	// takes its spawner as master (startSlavedEffects: its guard point).
	if (auto *member = world.Get<MobMember>(entity))
		member->nexus = request.spawner;
	if (auto *slave = world.Get<gameplay::Slaved>(entity))
	{
		const auto *master = world.Get<gameplay::DefinitionRef>(request.spawner);
		const Fixed masterRadius = master != nullptr ? content::BoundingCircleRadius(game.templates.DefinitionAt(master->index).geometry) : Fixed{};
		gameplay::Enslave(*slave, request.spawner, masterRadius, world.Resource<gameplay::RandomSeed>().value, game.tick, entity);
	}
	spawner = world.Get<gameplay::Spawner>(request.spawner);
	spawner->spawned[spawner->spawnedCount++] = entity;
	if (world.IsAlive(barracks))
	{
		// DefaultProductionExitUpdate::exitObjectViaDoor: at its create point facing its way, out along its exit path
		// (aiFollowExitProductionPath: through the barracks); the spawn still thinks the spawner made it.
		ExitViaProductionDoor(game, barracks, entity);
		if (!world.Has<gameplay::IgnoredObstacle>(entity))
			world.Add<gameplay::IgnoredObstacle>(entity);
		world.Get<gameplay::IgnoredObstacle>(entity)->obstacle = barracks;
		--world.Get<gameplay::Spawner>(request.spawner)->initialBurstLeft;
	}
	else if (budding)
	{
		if (auto *order = world.Get<gameplay::MoveOrder>(entity))
			*order = gameplay::Replanned(gameplay::MoveToPoint(at)); // aiMoveToPosition: it cannot stay where another is
	}
	else if (production && production->hasExit && !place)
		if (auto *order = world.Get<gameplay::MoveOrder>(entity))
		{
			*order = gameplay::Replanned(gameplay::MoveToPoint(InFrameOf(frame, production->rallyPoint)));
			FollowOnToRallyPoint(game, entity, request.spawner, InFrameOf(frame, production->rallyPoint));
			world.Add<gameplay::IgnoredObstacle>(entity);
			world.Get<gameplay::IgnoredObstacle>(entity)->obstacle = request.spawner; // it walks out through its spawner
		}
	if (production && production->supplyExit)
	{
		if (auto *harvester = world.Get<gameplay::Harvester>(entity))
			harvester->forceWanting = true;
		GrantExitStealth(game, request.spawner, entity, production->exitStealthTicks);
	}
	return entity;
}
}
