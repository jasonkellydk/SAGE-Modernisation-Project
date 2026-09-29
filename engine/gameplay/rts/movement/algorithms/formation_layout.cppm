export module engine.gameplay.rts.movement.algorithms.formation_layout;
import std;

export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;

// A group's destinations in rows and columns (the original's navigation::planFormation, which AIGroup::
// groupMoveToPosition lays a group of ground units out with), in fixed point. The units, by id, are grouped by strength
// then kind (each group fills its own rows); the frontage faces from the group's centre toward the target and the
// column count is the one whose columns-to-rows ratio is nearest 1.5 (or the columns asked for) that still fits the
// bounds; within a group the units furthest forward take the front rows, and within a row the leftmost-to-rightmost
// order is kept (ties by id). The slots are spaced at least `minimumSpacing` apart (twice the largest radius, plus 2),
// centred on the target, and the target moved just enough to keep every slot `radius` inside the bounds. None when the
// units or bounds cannot take it.
export namespace engine::gameplay
{
struct FormationUnit
{
	std::uint32_t id{0};
	Engine::Math::FixedVector2 position;
	Engine::Math::Fixed radius;
	std::uint32_t type{0};
	std::uint32_t strength{0};
};

struct FormationSlot
{
	std::uint32_t id{0};
	Engine::Math::FixedVector2 position;
};

struct FormationPlan
{
	std::uint32_t columns{0};
	std::uint32_t rows{0};
	Engine::Math::Fixed spacing;
	std::vector<FormationSlot> slots;
};

inline FormationPlan PlanFormation(std::span<const FormationUnit> input, Engine::Math::FixedVector2 target, Engine::Math::FixedVector2 low,
	Engine::Math::FixedVector2 high, Engine::Math::Fixed minimumSpacing = Engine::Math::Fixed::FromInt(22), std::uint32_t requestedColumns = 0)
{
	using Engine::Math::Fixed;
	using Engine::Math::FixedVector2;
	FormationPlan result;
	if (input.empty() || !(low.x < high.x && low.y < high.y) || minimumSpacing <= Fixed{})
		return result;
	std::vector<FormationUnit> units(input.begin(), input.end());
	std::sort(units.begin(), units.end(), [](const FormationUnit &a, const FormationUnit &b) { return a.id < b.id; });
	FixedVector2 sum;
	Fixed radius;
	for (std::size_t index = 0; index < units.size(); ++index)
	{
		if ((index > 0 && units[index - 1].id == units[index].id) || units[index].radius < Fixed{})
			return result;
		sum += units[index].position;
		radius = std::max(radius, units[index].radius);
	}
	std::sort(units.begin(), units.end(), [](const FormationUnit &a, const FormationUnit &b) {
		if (a.strength != b.strength)
			return a.strength < b.strength;
		if (a.type != b.type)
			return a.type < b.type;
		return a.id < b.id;
	});
	std::vector<std::pair<std::size_t, std::size_t>> groups;
	for (std::size_t begin = 0; begin < units.size();)
	{
		std::size_t end = begin + 1;
		while (end < units.size() && units[end].type == units[begin].type && units[end].strength == units[begin].strength)
			++end;
		groups.emplace_back(begin, end);
		begin = end;
	}
	const Fixed count = Fixed::FromInt(static_cast<std::int64_t>(units.size()));
	const FixedVector2 centre = sum / count;
	FixedVector2 forward = target - centre;
	const Fixed length = Engine::Math::Length(forward);
	forward = length > Fixed::FromRatio(1, 1000) ? forward / length : FixedVector2{Fixed{}, Fixed::One()};
	const FixedVector2 side{-forward.y, forward.x};
	const Fixed spacing = std::max(minimumSpacing, radius * 2 + Fixed::FromInt(2));
	// A broad compact frontage: the column count adapts to the bounds before any slot is placed.
	std::optional<Fixed> best;
	std::uint32_t columns = 0, rows = 0;
	const Fixed half = Fixed::FromRatio(1, 2);
	for (std::uint32_t c = 1; c <= units.size(); ++c)
	{
		std::uint32_t r = 0;
		for (const auto &[begin, end] : groups)
			r += static_cast<std::uint32_t>((end - begin + c - 1) / c);
		const Fixed halfWidth = Fixed::FromInt(c - 1) * spacing * half;
		const Fixed halfDepth = Fixed::FromInt(static_cast<std::int64_t>(r) - 1) * spacing * half;
		const Fixed extentX = Engine::Math::Abs(side.x) * halfWidth + Engine::Math::Abs(forward.x) * halfDepth + radius;
		const Fixed extentY = Engine::Math::Abs(side.y) * halfWidth + Engine::Math::Abs(forward.y) * halfDepth + radius;
		if (extentX * 2 > high.x - low.x || extentY * 2 > high.y - low.y)
			continue;
		const Fixed score = requestedColumns != 0 ? Engine::Math::Abs(Fixed::FromInt(c) - Fixed::FromInt(requestedColumns))
												  : Engine::Math::Abs(Fixed::FromInt(c) / Fixed::FromInt(r) - Fixed::FromRatio(3, 2));
		if (!best || score < *best)
		{
			best = score;
			columns = c;
			rows = r;
		}
	}
	if (columns == 0)
		return result;
	const auto longitudinal = [&](const FormationUnit &unit) { return Engine::Math::Dot(unit.position - centre, forward); };
	const auto lateral = [&](const FormationUnit &unit) { return Engine::Math::Dot(unit.position - centre, side); };
	for (const auto &[begin, end] : groups)
		std::sort(units.begin() + static_cast<std::ptrdiff_t>(begin), units.begin() + static_cast<std::ptrdiff_t>(end), [&](const FormationUnit &a, const FormationUnit &b) {
			const Fixed first = longitudinal(a), second = longitudinal(b);
			return first != second ? first > second : a.id < b.id;
		});
	std::vector<FormationSlot> slots;
	slots.reserve(units.size());
	FixedVector2 offset;
	std::uint32_t row = 0;
	for (const auto &[groupBegin, groupEnd] : groups)
		for (std::size_t begin = groupBegin; begin < groupEnd; begin += columns, ++row)
		{
			const std::size_t end = std::min<std::size_t>(begin + columns, groupEnd);
			std::sort(units.begin() + static_cast<std::ptrdiff_t>(begin), units.begin() + static_cast<std::ptrdiff_t>(end), [&](const FormationUnit &a, const FormationUnit &b) {
				const Fixed first = lateral(a), second = lateral(b);
				return first != second ? first > second : a.id < b.id;
			});
			for (std::size_t index = begin; index < end; ++index)
			{
				const Fixed across = (Fixed::FromInt(static_cast<std::int64_t>(end - begin - 1)) * half - Fixed::FromInt(static_cast<std::int64_t>(index - begin))) * spacing;
				const Fixed ahead = -Fixed::FromInt(row) * spacing;
				slots.push_back({units[index].id, side * across + forward * ahead});
				offset += slots.back().position;
			}
		}
	const FixedVector2 average = offset / count;
	FixedVector2 lowest, highest;
	for (FormationSlot &slot : slots)
	{
		slot.position = slot.position - average;
		lowest = {std::min(lowest.x, slot.position.x), std::min(lowest.y, slot.position.y)};
		highest = {std::max(highest.x, slot.position.x), std::max(highest.y, slot.position.y)};
	}
	const FixedVector2 minimum{low.x + radius - lowest.x, low.y + radius - lowest.y};
	const FixedVector2 maximum{high.x - radius - highest.x, high.y - radius - highest.y};
	if (minimum.x > maximum.x || minimum.y > maximum.y)
		return result;
	target = {std::clamp(target.x, minimum.x, maximum.x), std::clamp(target.y, minimum.y, maximum.y)};
	for (FormationSlot &slot : slots)
		slot.position = slot.position + target;
	result.columns = columns;
	result.rows = rows;
	result.spacing = spacing;
	result.slots = std::move(slots);
	return result;
}
}
