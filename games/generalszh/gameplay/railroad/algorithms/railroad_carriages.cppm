export module games.generalszh.gameplay.railroad.algorithms.railroad_carriages;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.railroad.algorithms.railroad_motion;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.objects.resources.object_templates;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import games.generalszh.gameplay.railroad.systems.railroad_collision_system;
import engine.gameplay.common.physics.components.physics_body;
import engine.gameplay.rts.navigation.components.navigation;
import games.generalszh.gameplay.scripts.algorithms.unit_script_orders;

export namespace generalszh::gameplay
{
// createCarriages / hitchNewCarriagebyTemplate for the locomotives the railroad asked for, after the tick: its
// templates' cars made on its team one after another (stopping at a template that does not exist), each hitched behind
// the one before on its track, then placed at once along it behind the locomotive as its pull stands (their getPulled).
inline void ApplyRailroadRequests(GameWorld &game)
{
	auto &world = game.world;
	auto *requests = world.FindResource<RailroadRequests>();
	auto *waypoints = world.FindResource<RailWaypoints>();
	auto *tracks = world.FindResource<RailTracks>();
	if (requests == nullptr || waypoints == nullptr || tracks == nullptr)
		return;
	// makeAWallOutOfThisTrain (createAWallFromMyFootprint / removeWallFromMyFootprint) and disembark()
	// (orderAllPassengersToExit), for the locomotive and down its chain.
	const auto chain = [&](ecs::Entity locomotive, const auto &each) {
		std::size_t guard = 0;
		for (ecs::Entity car = locomotive; car != ecs::Entity{} && world.IsAlive(car) && guard++ < 256;)
		{
			each(car);
			const Railcar *rail = world.Get<Railcar>(car);
			car = rail != nullptr ? rail->trailer : ecs::Entity{};
		}
	};
	for (const auto &[locomotive, on] : requests->walls)
		chain(locomotive, [&](ecs::Entity car) {
			if (on && !world.Has<engine::gameplay::NavigationObstacle>(car))
			{
				const auto &kind = game.templates.DefinitionAt(world.Get<engine::gameplay::DefinitionRef>(car)->index);
				engine::gameplay::NavigationObstacle wall;
				wall.footprint.shape = kind.geometry.shape == content::GeometryShape::Box ? engine::gameplay::ObstacleShape::Box : engine::gameplay::ObstacleShape::Cylinder;
				wall.footprint.majorRadius = kind.geometry.majorRadius;
				wall.footprint.minorRadius = kind.geometry.minorRadius;
				world.Add<engine::gameplay::NavigationObstacle>(car);
				*world.Get<engine::gameplay::NavigationObstacle>(car) = wall;
			}
			else if (!on && world.Has<engine::gameplay::NavigationObstacle>(car))
				world.Remove<engine::gameplay::NavigationObstacle>(car);
		});
	requests->walls.clear();
	for (const ecs::Entity locomotive : requests->disembarks)
		chain(locomotive, [&](ecs::Entity car) { Evacuate(game, car); });
	requests->disembarks.clear();
	if (requests->carriages.empty())
		return;
	const std::vector<ecs::Entity> locomotives = std::move(requests->carriages);
	requests->carriages.clear();
	for (const ecs::Entity locomotive : locomotives)
	{
		Railcar *loco = world.IsAlive(locomotive) ? world.Get<Railcar>(locomotive) : nullptr;
		const auto *reference = loco != nullptr ? world.Get<engine::gameplay::DefinitionRef>(locomotive) : nullptr;
		const RailroadConfig *config = reference != nullptr ? game.templates.RailroadOf(reference->index) : nullptr;
		if (config == nullptr)
			continue;
		const std::uint32_t team = world.Get<engine::gameplay::TeamMember>(locomotive)->team;
		const engine::gameplay::Transform placed = *world.Get<engine::gameplay::Transform>(locomotive);
		std::vector<ecs::Entity> made;
		for (const std::string &name : config->carriages)
		{
			if (game.templates.Content().objects.Find(name) == nullptr)
				break;
			const ecs::Entity carriage = SpawnObject(game, name, placed.position.XY(), placed.facing, team, {});
			if (!world.IsAlive(carriage) || world.Get<Railcar>(carriage) == nullptr)
				break;
			made.push_back(carriage);
		}
		if (made.empty())
			continue;
		loco = world.Get<Railcar>(locomotive);
		loco->trailer = made.front();
		for (std::size_t index = 0; index < made.size(); ++index)
		{
			Railcar &car = *world.Get<Railcar>(made[index]);
			car.anchor = loco->anchor;
			car.hitched = 1;
			if (index + 1 < made.size())
				car.trailer = made[index + 1];
		}
		// Placed along the track at once, as the locomotive's update goes on to pull them.
		std::vector<RailCarRow> rows;
		const auto row = [&](ecs::Entity entity) {
			const auto *owner = world.Get<engine::gameplay::Owner>(entity);
			return RailCarRow{entity, world.Get<Railcar>(entity), world.Get<engine::gameplay::Transform>(entity),
				game.templates.RailroadOf(world.Get<engine::gameplay::DefinitionRef>(entity)->index), owner != nullptr ? owner->player : 0u};
		};
		rows.push_back(row(locomotive));
		for (const ecs::Entity entity : made)
			rows.push_back(row(entity));
		std::ranges::sort(rows, [](const RailCarRow &a, const RailCarRow &b) {
			return a.entity.index != b.entity.index ? a.entity.index < b.entity.index : a.entity.generation < b.entity.generation;
		});
		std::vector<ecs::Entity> destroyed;
		RailScene scene{rows, *waypoints, *tracks, game.ground, destroyed};
		RailCarRow *first = scene.Find(locomotive);
		if (const RailTrack *track = TrackOf(scene, loco->anchor); track != nullptr && first != nullptr)
		{
			PullChain(scene, *first, *track);
			// Hidden as their own update shows them (in the wings or past the end of a line that does not loop).
			if (!track->looping)
				for (const ecs::Entity entity : made)
				{
					Railcar &car = *world.Get<Railcar>(entity);
					car.hidden = car.wings != 0 || car.endOfLine != 0 ? 1 : 0;
				}
		}
	}
}

// The trains' pushes after the tick (the partition's collisions at the end of the frame): each victim put where it was
// shoved and lifted, its velocity added to, its spins and turn set, and let fall, bounce and slide in the air.
inline void ApplyRailroadImpulses(GameWorld &game)
{
	auto &world = game.world;
	auto *impulses = world.FindResource<RailroadImpulses>();
	if (impulses == nullptr || impulses->impulses.empty())
		return;
	namespace gp = engine::gameplay;
	for (const RailroadImpulse &push : impulses->impulses)
	{
		if (!world.IsAlive(push.victim))
			continue;
		if (push.setsPosition != 0)
			if (auto *at = world.Get<gp::Transform>(push.victim))
				at->position = push.position;
		gp::PhysicsBody *body = world.Get<gp::PhysicsBody>(push.victim);
		if (body == nullptr)
			continue;
		if (push.pushes != 0)
			body->velocity = body->velocity + push.velocity;
		if (push.spins != 0)
		{
			body->pitchRate = push.pitchRate;
			body->rollRate = push.rollRate;
		}
		if (push.turns != 0)
			body->yawRate = push.yawRate;
		if (push.falls != 0)
		{
			body->Set(gp::physics_flag::AllowToFall, true);
			body->Set(gp::physics_flag::AllowBouncing, true);
			body->Set(gp::physics_flag::AirborneFriction, true);
		}
	}
	impulses->impulses.clear();
}
}
