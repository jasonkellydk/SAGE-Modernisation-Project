export module games.generalszh.gameplay.mines.algorithms.minefields;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.mines.components.minefield_generator;
import games.generalszh.content.mines.mine_content;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.rts.mines.components.minefield;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.targetable;
import engine.gameplay.common.spatial.resources.spatial_index;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.rts.upgrades.components.upgradable;
import engine.gameplay.rts.navigation.resources.navigation_grid;
import engine.gameplay.rts.navigation.definitions.pathfind_cell;
import engine.ecs.query.query;
import Engine.Core.Math.FixedAngle;
import Engine.Core.Math.FixedRandom;

// GenerateMinefieldBehavior (GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/GenerateMinefieldBehavior.cpp):
// laying a thing's minefield (placeMines, once) of its MineName (UpgradedMineName once upgraded) around it:
// - SmartBorder: rings (placeMinesAroundCircle) from its footprint grown by a mine's radius out a mine's width at a time
//   while inside DistanceAroundObject (at least one), a mine in its middle first unless SmartBorderSkipInterior (the
//   rings then from the mine's own footprint);
// - BorderOnly: one ring at its footprint grown by DistanceAroundObject;
// - else: MinesPerSquareFoot of that grown footprint's area at random points in it, kept a mine's width apart, none in
//   its own footprint.
// A ring has as many mines as its circumference holds mine widths (rounded up), evenly spaced from angle 0, each
// jittered RandomJitter of a mine's radius. A mine (placeMineAt) is not laid on water or cliffs, nor where a structure
// covers SkipIfThisMuchUnderStructure of it; it is its player's default team's, faces anywhere, and knows who made it
// (its producer) and scoots out from it (MinefieldBehavior::setScootParms). Its GenerationFX plays on it.
// Laid when an upgrade triggers it (upgradeImplementation), or as it dies (GenerateOnlyOnDeath); an Upgradable one
// swaps its mines for the upgraded ones once it has Upgrade_ChinaEMPMines (update: its own mines removed, laid again).
// Boxes that are not AlwaysCircular are ringed as their bounding circles (the original walks their rectangles; no
// shipped minefield does).
export namespace generalszh::gameplay
{
struct MinefieldSource
{
	ecs::Entity generator;
	std::uint32_t definition{0};
	engine::gameplay::Transform transform;
	std::uint32_t team{0xFFFFFFFFu};
};

namespace minefield_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector3;

inline Fixed TwoPi() noexcept { return Fixed::FromRatio(6283185, 1000000); }

// MinefieldBehavior::setScootParms: from `start` (its maker) to `end` over ScootFromStartingPointTime, or the time to
// fall that far, at a steady speed, falling under gravity, down on the ground at the last.
inline void Scoot(engine::gameplay::Minefield &mine, engine::gameplay::Transform &transform, FixedVector3 start, FixedVector3 end, std::uint64_t ticks,
	Fixed gravity)
{
	if (start.z > end.z)
	{
		const Fixed fall = Engine::Math::Sqrt(Fixed::FromInt(2) * (start.z - end.z) / Engine::Math::Abs(gravity));
		ticks = std::max<std::uint64_t>(ticks, static_cast<std::uint64_t>(std::max<std::int64_t>(fall.Ceil(), 0)));
	}
	if (ticks == 0)
	{
		transform.position = end;
		return;
	}
	const Fixed dx = end.x - start.x, dy = end.y - start.y, dz = end.z - start.z;
	const Fixed distance = Engine::Math::Length(Engine::Math::FixedVector2{dx, dy});
	const Fixed tiny = Fixed::FromRatio(1, 10);
	if (distance <= tiny && Engine::Math::Abs(dz) <= tiny)
	{
		transform.position = end;
		return;
	}
	const Fixed speed = distance / Fixed::FromInt(static_cast<std::int64_t>(ticks));
	mine.scootVelocity = distance <= tiny ? FixedVector3{} : FixedVector3{dx / distance * speed, dy / distance * speed, Fixed{}};
	mine.scootAcceleration = FixedVector3{Fixed{}, Fixed{}, gravity};
	mine.scootLeft = ticks;
	transform.position = start;
}
}

// placeMines, from `source` (upgraded: its UpgradedMineName). The mines made.
inline std::vector<ecs::Entity> PlaceMines(GameWorld &game, const MinefieldSource &source, bool upgraded)
{
	using namespace minefield_detail;
	namespace gp = engine::gameplay;
	auto &world = game.world;
	std::vector<ecs::Entity> made;
	const content::ObjectDefinition &definition = game.templates.DefinitionAt(source.definition);
	const auto generator = content::ReadMinefieldGenerator(definition, game.templates.Content().gameData);
	if (!generator)
		return made;
	if (auto *state = world.IsAlive(source.generator) ? world.Get<MinefieldGenerator>(source.generator) : nullptr)
	{
		if (state->generated != 0)
			return made;
		state->generated = 1;
	}
	const std::string &name = upgraded ? generator->upgradedMine : generator->mine;
	const content::ObjectDefinition *mineDefinition = game.templates.Content().objects.Find(name);
	if (mineDefinition == nullptr)
		return made;
	const auto mineContent = content::ReadMinefield(*mineDefinition, game.step);
	const Fixed mineRadius = content::BoundingCircleRadius(mineDefinition->geometry);
	const Fixed mineWidth = mineRadius * Fixed::FromInt(2);
	const Fixed jitter = mineRadius * generator->jitter;
	const Fixed covered = mineRadius * generator->underStructure;
	std::uint32_t team = source.team;
	if (team < game.roster.TeamCount())
		if (const auto own = game.roster.DefaultTeam(game.roster.TeamAt(team).owner))
			team = *own;
	const FixedVector3 target = source.transform.position;
	const auto &grid = world.Resource<gp::NavigationGrid>();
	const gp::SpatialIndex *spatial = world.FindResource<gp::SpatialIndex>();
	const Fixed gravity = game.templates.Content().gameData.gravity;
	// placeMineAt.
	const auto place = [&](FixedVector3 at) {
		const auto cell = [](Fixed value) { return static_cast<std::int32_t>((value / Fixed::FromInt(gp::PathfindCellSize)).Floor()); };
		if (grid.Width() > 0 && grid.Contains(cell(at.x), cell(at.y)))
		{
			const gp::PathfindCellType type = grid.Type(cell(at.x), cell(at.y));
			if (type == gp::PathfindCellType::Water || type == gp::PathfindCellType::Cliff)
				return;
		}
		bool underStructure = false;
		if (spatial != nullptr)
			spatial->ForEachWithin(at.XY(), covered, [&](const gp::SpatialEntry &entry) {
				if ((entry.classes & gp::target_class::Structure) == 0)
					return;
				const Fixed reach = covered + entry.radius;
				if (Engine::Math::DistanceSquared(entry.position.XY(), at.XY()) < reach * reach)
					underStructure = true;
			});
		if (underStructure)
			return;
		const Engine::Math::TurnAngle facing{static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, 0xFFFFFFFFll))};
		const ecs::Entity mine = SpawnObject(game, name, at.XY(), facing, team, "");
		if (!world.IsAlive(mine))
			return;
		made.push_back(mine);
		if (auto *field = world.Get<gp::Minefield>(mine))
		{
			field->producer = source.generator;
			if (auto *transform = world.Get<gp::Transform>(mine))
			{
				FixedVector3 end = transform->position;
				Scoot(*field, *transform, source.transform.position, end, mineContent ? mineContent->scootTicks : 0, gravity);
			}
		}
	};
	const auto ring = [&](Fixed radius) {
		std::int64_t count = mineWidth > Fixed{} ? (TwoPi() * radius / mineWidth).Ceil() : 1;
		count = std::max<std::int64_t>(count, 1);
		for (std::int64_t index = 0; index < count; ++index)
		{
			const Engine::Math::TurnAngle angle{static_cast<std::uint32_t>((static_cast<std::uint64_t>(index) << 32) / static_cast<std::uint64_t>(count))};
			FixedVector3 at{target.x + radius * Engine::Math::Cos(angle), target.y + radius * Engine::Math::Sin(angle), {}};
			if (jitter > Fixed{})
			{
				at.x += Engine::Math::UniformFixed(game.random, Fixed{} - jitter, jitter);
				at.y += Engine::Math::UniformFixed(game.random, Fixed{} - jitter, jitter);
			}
			at.z = game.ground.At(at.XY());
			place(at);
		}
	};
	const Fixed footprint = content::BoundingCircleRadius(definition.geometry);
	if (generator->smartBorder)
	{
		Fixed radius = footprint;
		if (!generator->smartBorderSkipInterior)
		{
			radius = mineRadius;
			place({target.x, target.y, game.ground.At(target.XY())});
		}
		radius += mineRadius;
		do
		{
			ring(radius);
			radius += mineWidth;
		} while (radius < generator->distance);
	}
	else if (generator->borderOnly)
		ring(footprint + generator->distance);
	else
	{
		// placeMinesInFootprint over the grown circle: random points, kept apart, none in its own footprint.
		const Fixed radius = footprint + generator->distance;
		const Fixed area = TwoPi() / Fixed::FromInt(2) * radius * radius;
		const std::int64_t count = std::max<std::int64_t>((generator->density * area).Ceil(), 1);
		std::vector<FixedVector3> laid;
		for (std::int64_t index = 0; index < count; ++index)
		{
			FixedVector3 at;
			for (int retry = 0; retry < 100; ++retry)
			{
				const Engine::Math::TurnAngle angle{static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, 0xFFFFFFFFll))};
				const Fixed distance = Engine::Math::UniformFixed(game.random, Fixed{}, radius);
				at = {target.x + distance * Engine::Math::Cos(angle), target.y + distance * Engine::Math::Sin(angle), {}};
				if (std::none_of(laid.begin(), laid.end(), [&](const FixedVector3 &other) {
						return Engine::Math::DistanceSquared(other.XY(), at.XY()) < mineWidth * mineWidth;
					}))
					break;
			}
			if (Engine::Math::DistanceSquared(at.XY(), target.XY()) <= footprint * footprint)
				continue;
			at.z = game.ground.At(at.XY());
			const std::size_t before = made.size();
			place(at);
			if (made.size() > before)
				laid.push_back(at);
		}
	}
	if (!generator->effect.empty())
		if (auto *effects = world.FindResource<MinefieldEffects>())
			effects->played.push_back({generator->effect, {target.x.Raw(), target.y.Raw(), target.z.Raw()}});
	return made;
}

// GenerateMinefieldBehavior::update: an Upgradable minefield swaps its mines for the upgraded ones once its maker has
// Upgrade_ChinaEMPMines (its own mines removed: destroyObject, then laid again).
inline void TendMinefields(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto emp = game.templates.Content().upgrades.Find("Upgrade_ChinaEMPMines");
	if (!emp)
		return;
	std::vector<ecs::Entity> swapping;
	ecs::Query<ecs::Read<MinefieldGenerator>, ecs::Read<gp::Upgradable>, ecs::Read<gp::DefinitionRef>> generators(world);
	generators.ForEachChunk([&](auto chunk) {
		const auto states = chunk.template Get<MinefieldGenerator>();
		const auto upgrades = chunk.template Get<gp::Upgradable>();
		const auto definitions = chunk.template Get<gp::DefinitionRef>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < states.size(); ++row)
		{
			if (states[row].generated == 0 || states[row].upgraded != 0 || !upgrades[row].completed.Has(static_cast<std::uint32_t>(*emp)))
				continue;
			const auto generator = content::ReadMinefieldGenerator(game.templates.DefinitionAt(definitions[row].index), game.templates.Content().gameData);
			if (generator && generator->upgradable)
				swapping.push_back(entities[row]);
		}
	});
	for (const ecs::Entity maker : swapping)
	{
		std::vector<ecs::Entity> old;
		ecs::Query<ecs::Read<gp::Minefield>> mines(world);
		mines.ForEachChunk([&](auto chunk) {
			const auto fields = chunk.template Get<gp::Minefield>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < fields.size(); ++row)
				if (fields[row].producer == maker)
					old.push_back(entities[row]);
		});
		RetireNow(game, old);
		MinefieldGenerator &state = *world.Get<MinefieldGenerator>(maker);
		state.upgraded = 1;
		state.generated = 0;
		const auto *member = world.Get<gp::TeamMember>(maker);
		PlaceMines(game, {maker, world.Get<gp::DefinitionRef>(maker)->index, *world.Get<gp::Transform>(maker), member != nullptr ? member->team : 0xFFFFFFFFu},
			true);
	}
}
}
