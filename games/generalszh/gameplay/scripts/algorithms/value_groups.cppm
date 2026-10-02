export module games.generalszh.gameplay.scripts.algorithms.value_groups;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.scripts.algorithms.command_button_targets;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.rts.vision.components.vision;
import engine.gameplay.rts.vision.resources.shroud_map;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.rts.combat.components.aggression;
import engine.gameplay.rts.movement.components.move_order;
import engine.ecs.query.query;

// The partition's value map and what a script finds in it (PartitionManager's cash values, getNearestGroupWithValue,
// ScriptActions::doSkirmishAttackNearestGroupWithValue):
//   CellCashValues (Object::addValue -> doValueAffect): each object a player controls, not under construction, not dead
//     and clearing shroud, is worth its template's BuildCost to its player over a disc of cells about its cell
//     (DiscreteCircle, the radius its vision range in cells rounded up, at least 1), each cell that cost times
//     1 - (cells from the centre) / (radius + 1), clamped to 0..1 and truncated (in fixed point: the original's single-precision reals
//     may round a cell's share a unit differently);
//   NearestCellWithValue (getNearestGroupWithValue, cellValueProc, iterateCellsBreadthFirst): the cells walked
//     breadth first from the one at `from` (each step queues the neighbours left, up, right, down of the cell taken
//     last: the original's lagging order, and its unchecked bottom edge kept inside the map here), the first whose
//     value summed over the players `player` counts enemies is above the value (the original's "greater" is the value
//     itself as a flag: a value of 0 asks for a cell below 0, which none is); its corner (cell * cell size) is the place;
//   SkirmishAttackNearestGroupWithValue: a team's group (its members with an AI) attack-moves there from its centre
//     (groupAttackMoveToPosition: each member able to attack attack-moves, the rest move), only for GREATER or
//     GREATER_EQUAL (both test above). Nothing found or another comparison: the original ordered a move to an
//     uninitialized place; here no order is given (a quirk fixed).
export namespace generalszh::gameplay
{
// Per cell (row-major, the shroud map's cells), per player summed into the players of `mask`.
inline std::vector<std::int64_t> CellCashValues(GameWorld &game, std::uint64_t mask)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	const auto &shroud = game.world.Resource<gp::ShroudMap>();
	const std::int32_t width = shroud.CellsX(), height = shroud.CellsY();
	std::vector<std::int64_t> values(static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0)), 0);
	ecs::Query<ecs::Read<gp::DefinitionRef>, ecs::Read<gp::Owner>, ecs::Read<gp::Transform>, ecs::Read<gp::Vision>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto refs = chunk.template Get<gp::DefinitionRef>();
		const auto owners = chunk.template Get<gp::Owner>();
		const auto transforms = chunk.template Get<gp::Transform>();
		const auto visions = chunk.template Get<gp::Vision>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < refs.size(); ++row)
		{
			const std::uint32_t player = owners[row].player;
			if (player >= 64 || (mask & (std::uint64_t{1} << player)) == 0)
				continue;
			const ecs::Entity entity = entities[row];
			if (game.world.Has<gp::UnderConstruction>(entity) || game.world.Has<gp::Dying>(entity) || visions[row].clearingRange <= Fixed{})
				continue;
			const content::ObjectDefinition &kind = game.templates.DefinitionAt(refs[row].index);
			const auto *aggression = game.world.Get<gp::Aggression>(entity);
			const Fixed vision = aggression != nullptr ? aggression->vision : kind.visionRange;
			const auto cell = shroud.CellOf(transforms[row].position.x, transforms[row].position.y);
			const std::int32_t radius = shroud.CellsFor(vision);
			const Fixed reach = Fixed::FromInt(radius + 1);
			const Fixed cost = Fixed::FromInt(kind.buildCost);
			const auto scan = [&](std::int32_t x1, std::int32_t x2, std::int32_t y) {
				if (y < 0 || y >= height || x1 >= width || x2 < 0)
					return;
				for (std::int32_t x = std::max(x1, 0); x <= std::min(x2, width - 1); ++x)
				{
					const Fixed distance = Engine::Math::Length(Engine::Math::FixedVector2{Fixed::FromInt(x - cell[0]), Fixed::FromInt(y - cell[1])});
					const Fixed share = std::clamp(Fixed::One() - distance / reach, Fixed{}, Fixed::One());
					values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] += (cost * share).Floor();
				}
			};
			// DiscreteCircle(cell, radius).drawCircle.
			std::vector<std::array<std::int32_t, 3>> edges;
			std::int32_t x = 0, y = radius, d = (1 - radius) << 1;
			while (y >= 0)
			{
				edges.push_back({cell[0] - x, cell[0] + x, cell[1] + y});
				if (d + y > 0)
				{
					--y;
					d -= ((y << 1) - 1);
				}
				if (x > d)
				{
					++x;
					d += ((x << 1) + 1);
				}
			}
			for (std::size_t index = 0; index < edges.size(); ++index)
			{
				if (index + 1 != edges.size() && edges[index][2] == edges[index + 1][2])
					continue;
				scan(edges[index][0], edges[index][1], edges[index][2]);
				if (edges[index][2] != cell[1])
					scan(edges[index][0], edges[index][1], (cell[1] << 1) - edges[index][2]);
			}
		}
	});
	return values;
}

inline std::optional<Engine::Math::FixedVector2> NearestCellWithValue(GameWorld &game, std::uint32_t player, Engine::Math::FixedVector2 from, std::int64_t value)
{
	namespace gp = engine::gameplay;
	const auto &shroud = game.world.Resource<gp::ShroudMap>();
	const auto &relationships = game.world.Resource<gp::Relationships>();
	std::uint64_t mask = 0;
	for (std::uint32_t other = 0; other < game.roster.PlayerCount() && other < 64; ++other)
		if (relationships.Between(player, other) == gp::Relationship::Enemies)
			mask |= std::uint64_t{1} << other;
	if (mask == 0)
		return std::nullopt;
	const std::int32_t width = shroud.CellsX(), height = shroud.CellsY();
	if (width <= 0 || height <= 0)
		return std::nullopt;
	const std::vector<std::int64_t> values = CellCashValues(game, mask);
	const bool greater = value != 0;
	auto start = shroud.CellOf(from.x, from.y);
	start[0] = std::clamp(start[0], 0, width - 1);
	start[1] = std::clamp(start[1], 0, height - 1);
	std::vector<std::uint8_t> done(values.size(), 0);
	std::deque<std::array<std::int32_t, 2>> queue;
	const auto push = [&](std::int32_t x, std::int32_t y) {
		const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
		if (done[at] == 0)
		{
			done[at] = 1;
			queue.push_back({x, y});
		}
	};
	push(start[0], start[1]);
	std::int32_t x = start[0], y = start[1];
	while (!queue.empty())
	{
		if (x - 1 >= 0)
			push(x - 1, y);
		if (y - 1 >= 0)
			push(x, y - 1);
		if (x + 1 < width)
			push(x + 1, y);
		if (y + 1 < height)
			push(x, y + 1);
		const auto cell = queue.front();
		queue.pop_front();
		x = cell[0];
		y = cell[1];
		const std::int64_t here = values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
		if ((here > value && greater) || (here < value && !greater))
			return Engine::Math::FixedVector2{shroud.CellSize() * Engine::Math::Fixed::FromInt(x), shroud.CellSize() * Engine::Math::Fixed::FromInt(y)};
	}
	return std::nullopt;
}

// Parameter::GREATER_EQUAL and GREATER (the comparisons' order: LESS_THAN, LESS_EQUAL, EQUAL, GREATER_EQUAL, GREATER,
// NOT_EQUAL).
inline void SkirmishAttackNearestGroupWithValue(GameWorld &game, const std::string &team, std::int32_t comparison, std::int64_t value)
{
	namespace gp = engine::gameplay;
	const auto index = ResolveTeam(game, team);
	if (!index || (comparison != 3 && comparison != 4))
		return;
	const std::uint32_t player = game.roster.TeamAt(*index).owner;
	const std::vector<ecs::Entity> group = button_target_detail::GroupOf(game, *index);
	const auto center = button_target_detail::CenterOf(game, group);
	if (!center)
		return;
	const auto place = NearestCellWithValue(game, player, *center, value);
	if (!place)
		return;
	for (const ecs::Entity member : group)
	{
		if (!HasAi(game, member))
			continue;
		if (game.world.Get<gp::Aggression>(member) != nullptr)
			OrderAttackMove(game, member, *place);
		else if (game.world.Get<gp::MoveOrder>(member) != nullptr)
			OrderMove(game, member, *place);
	}
}
}
