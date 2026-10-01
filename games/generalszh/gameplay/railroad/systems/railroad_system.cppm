export module games.generalszh.gameplay.railroad.systems.railroad_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.railroad.algorithms.railroad_motion;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.lifetime.components.lifetime;
import games.generalszh.content.objects.model_conditions;

// RailroadBehavior::update for every train car, once a tick, in entity order (a train is one chain: its locomotive's
// update moves and places every car it pulls, one after another, as the original's does):
//   a locomotive's first update lays its track from the waypoint nearest it (loadTrackData) and hitches its cars
//   (createCarriages: when its first carriage template exists, cars the map put just behind it (the closest of its
//   allies' never hitched cars within twice its radius of the point twice its radius behind it; then likewise behind
//   each), else its templates' cars, made after the tick);
//   then the car's step (StepRailcar); a car on a track that does not loop is hidden in the wings and at the end of the
//   line; a car more than 3 below the ground there (a tunnel) takes on OVER_WATER. A car that took itself away goes
//   with the tick (destroyObject).
export namespace generalszh::gameplay
{
struct RailroadSystem
{
	using Query = ecs::Query<ecs::Write<Railcar>, ecs::Write<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Read<engine::gameplay::Owner>, ecs::OptionalWrite<engine::gameplay::Appearance>>;
	using Resources = ecs::Resources<ecs::Read<RailWaypoints>, ecs::Write<RailTracks>, ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::GroundHeight>,
		ecs::Read<engine::gameplay::Relationships>, ecs::Write<RailroadRequests>, ecs::Write<RailroadCues>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const auto &templates = context.Read<ObjectTemplates>();
		std::vector<RailCarRow> rows;
		std::vector<engine::gameplay::Appearance *> appearances;
		query.ForEachChunk([&](auto chunk) {
			auto cars = chunk.template Get<Railcar>();
			auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto owners = chunk.template Get<engine::gameplay::Owner>();
			auto looks = chunk.template Get<engine::gameplay::Appearance>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < cars.size(); ++row)
				if (const RailroadConfig *config = templates.RailroadOf(definitions[row].index))
					rows.push_back({entities[row], &cars[row], &transforms[row], config, owners[row].player}), appearances.push_back(looks.empty() ? nullptr : &looks[row]);
		});
		if (rows.empty())
			return;
		std::vector<std::size_t> order(rows.size());
		std::iota(order.begin(), order.end(), std::size_t{0});
		const auto before = [](ecs::Entity a, ecs::Entity b) { return a.index != b.index ? a.index < b.index : a.generation < b.generation; };
		std::ranges::sort(order, [&](std::size_t a, std::size_t b) { return before(rows[a].entity, rows[b].entity); });
		std::vector<RailCarRow> sorted;
		std::vector<engine::gameplay::Appearance *> sortedLooks;
		for (const std::size_t index : order)
			sorted.push_back(rows[index]), sortedLooks.push_back(appearances[index]);
		std::vector<ecs::Entity> destroyed;
		const auto &ground = context.Read<engine::gameplay::GroundHeight>();
		auto &requests = context.Write<RailroadRequests>();
		RailScene scene{sorted, context.Read<RailWaypoints>(), context.Write<RailTracks>(), ground, destroyed, &requests, &context.Write<RailroadCues>()};
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		for (std::size_t index = 0; index < sorted.size(); ++index)
		{
			RailCarRow &row = sorted[index];
			Railcar &car = *row.car;
			if (car.gone != 0)
				continue;
			if (car.trackLoaded == 0 && car.locomotive != 0)
			{
				const std::uint32_t anchor = RailAnchor(scene.waypoints, row.transform->position);
				if (anchor != RailWaypoints::None)
				{
					car.anchor = scene.waypoints.ids[anchor];
					if (TrackOf(scene, car.anchor) != nullptr)
						HitchCarriages(scene, row, relationships, requests);
				}
				car.trackLoaded = 1;
			}
			const RailTrack *track = TrackOf(scene, car.anchor);
			if (track == nullptr)
				continue;
			StepRailcar(scene, row);
			if (!track->looping)
				car.hidden = car.wings != 0 || car.endOfLine != 0 ? 1 : 0;
			if (engine::gameplay::Appearance *look = sortedLooks[index])
				look->Set(content::model_condition::OverWater,
					row.transform->position.z < ground.At(row.transform->position.XY()) - Engine::Math::Fixed::FromInt(3));
		}
		auto &commands = context.Commands();
		for (const ecs::Entity entity : destroyed)
			commands.Set<engine::gameplay::Lifetime>(entity, engine::gameplay::Lifetime{context.Tick(), 1, 0});
	}

	// createCarriages / hitchNewCarriagebyProximity: the chain of cars the map put down behind the locomotive, each the
	// closest (centre to centre on the ground, strictly within) of the never hitched cars allied to the one before, within
	// twice that one's radius of the point twice its radius behind it; none behind it: its templates' cars (made after the
	// tick).
	static void HitchCarriages(RailScene &scene, RailCarRow &locomotive, const engine::gameplay::Relationships &relationships, RailroadRequests &requests)
	{
		const RailroadConfig &config = *locomotive.config;
		if (config.carriages.empty() || !config.firstCarriageKnown)
			return;
		RailCarRow *behind = Behind(scene, locomotive, relationships);
		if (behind == nullptr)
		{
			requests.carriages.push_back(locomotive.entity);
			return;
		}
		RailCarRow *puller = &locomotive;
		while (behind != nullptr)
		{
			puller->car->trailer = behind->entity;
			if (behind->car->locomotive != 0)
				return; // "You can not hitch a locomotive in mid train"
			behind->car->anchor = locomotive.car->anchor;
			behind->car->hitched = 1;
			puller = behind;
			behind = Behind(scene, *puller, relationships);
		}
	}

	static RailCarRow *Behind(RailScene &scene, const RailCarRow &from, const engine::gameplay::Relationships &relationships)
	{
		const Engine::Math::Fixed reach = from.config->radius * Engine::Math::Fixed::FromInt(2);
		const Engine::Math::FixedVector2 hitch = from.transform->position.XY() - Engine::Math::Direction(from.transform->facing) * reach;
		RailCarRow *closest = nullptr;
		Engine::Math::Fixed closestSquared = reach * reach;
		for (RailCarRow &other : scene.cars)
		{
			if (other.entity == from.entity || other.car->hitched != 0 || !relationships.Allies(from.player, other.player))
				continue;
			const Engine::Math::Fixed squared = Engine::Math::DistanceSquared(other.transform->position.XY(), hitch);
			if (squared < closestSquared)
			{
				closest = &other;
				closestSquared = squared;
			}
		}
		return closest;
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::RailroadSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.railroad";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
