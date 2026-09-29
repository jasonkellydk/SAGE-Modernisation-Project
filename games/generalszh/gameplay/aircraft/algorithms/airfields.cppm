export module games.generalszh.gameplay.aircraft.algorithms.airfields;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.aircraft.components.airfield;
import engine.gameplay.rts.aircraft.components.jet;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.weapons.components.armament;
import engine.ecs.query.query;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.sale;

// Jets and the airfields that take them in, between ticks: which parking
// spaces are taken (by the jets that hold them), and a home for jets that
// have none (placed on the map, or whose airfield is gone): out of ammo,
// such a jet looks for the nearest allied airfield with a free space (not
// under construction nor being sold), as
// the original's circling jets do, and flies there to land.
export namespace generalszh::gameplay
{
// An airfield's first parking space no jet holds, after skipping `skip` free ones (none: full).
std::optional<std::uint32_t> FreeSpace(GameWorld &game, ecs::Entity airfield, std::uint32_t skip = 0)
{
	namespace gameplay = engine::gameplay;
	const gameplay::Airfield *field = game.world.IsAlive(airfield) ? game.world.Get<gameplay::Airfield>(airfield) : nullptr;
	if (field == nullptr)
		return std::nullopt;
	std::vector<bool> taken(field->spaceCount, false);
	ecs::Query<ecs::Read<gameplay::Jet>> jets(game.world);
	jets.ForEachChunk([&](auto chunk) {
		for (const gameplay::Jet &jet : chunk.template Get<gameplay::Jet>())
			if (jet.airfield == airfield && jet.space < taken.size())
				taken[jet.space] = true;
	});
	for (std::uint32_t space = 0; space < taken.size(); ++space)
		if (!taken[space] && skip-- == 0)
			return space;
	return std::nullopt;
}

void AssignAirfields(GameWorld &game)
{
	namespace gameplay = engine::gameplay;
	struct Homeless
	{
		ecs::Entity jet;
		std::uint32_t player{0};
		Engine::Math::FixedVector2 at;
	};
	std::vector<Homeless> homeless;
	ecs::Query<ecs::Read<gameplay::Jet>, ecs::Read<gameplay::Transform>, ecs::Read<gameplay::Owner>, ecs::Optional<gameplay::Armament>> jets(game.world);
	jets.ForEachChunk([&](auto chunk) {
		const auto states = chunk.template Get<gameplay::Jet>();
		const auto transforms = chunk.template Get<gameplay::Transform>();
		const auto owners = chunk.template Get<gameplay::Owner>();
		const auto armaments = chunk.template Get<gameplay::Armament>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < states.size(); ++row)
		{
			const bool outOfAmmo = !armaments.empty() && armaments[row].readyTick == gameplay::OutOfAmmo;
			if (outOfAmmo && !game.world.IsAlive(states[row].airfield))
				homeless.push_back({entities[row], owners[row].player, transforms[row].position.XY()});
		}
	});
	if (homeless.empty())
		return;
	struct Field
	{
		ecs::Entity entity;
		std::uint32_t player{0};
		Engine::Math::FixedVector2 at;
	};
	std::vector<Field> fields;
	ecs::Query<ecs::Read<gameplay::Airfield>, ecs::Read<gameplay::Transform>, ecs::Read<gameplay::Owner>> airfields(game.world);
	airfields.ForEachChunk([&](auto chunk) {
		const auto transforms = chunk.template Get<gameplay::Transform>();
		const auto owners = chunk.template Get<gameplay::Owner>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < transforms.size(); ++row)
			// findSuitableAirfield: not one still being built, nor one being sold.
			if (!game.world.Has<gameplay::UnderConstruction>(entities[row]) && !game.world.Has<gameplay::Sale>(entities[row]))
				fields.push_back({entities[row], owners[row].player, transforms[row].position.XY()});
	});
	for (const Homeless &jet : homeless)
	{
		const Field *best = nullptr;
		Engine::Math::Fixed bestDistance;
		for (const Field &field : fields)
		{
			if (!game.world.Resource<gameplay::Relationships>().Allies(jet.player, field.player))
				continue;
			const Engine::Math::Fixed distance = Engine::Math::DistanceSquared(jet.at, field.at);
			if ((best == nullptr || distance < bestDistance) && FreeSpace(game, field.entity))
			{
				best = &field;
				bestDistance = distance;
			}
		}
		if (best == nullptr)
			continue;
		gameplay::Jet &state = *game.world.Get<gameplay::Jet>(jet.jet);
		state.airfield = best->entity;
		state.space = *FreeSpace(game, best->entity);
	}
}
}
