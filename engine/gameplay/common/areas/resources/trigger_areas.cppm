export module engine.gameplay.common.areas.resources.trigger_areas;
import std;

import engine.ecs.system.system;

// The level's named polygon areas (the original's PolygonTrigger list: trigger areas, and water areas and rivers too),
// in whole map units, with their bounds. Contains: PolygonTrigger::pointInTrigger, a ray cast along +x counting the
// crossings of the edges (horizontal edges ignored; a point on a left edge is outside, on a right edge inside; an exact comparison in place of
// the original's float division). The original's list is built by prepending (PolygonTrigger::addPolygonTrigger), so it
// is walked last-read first: Find and the order objects enter areas follow it. Immutable level data.
export namespace engine::gameplay
{
struct TriggerArea
{
	std::string name;
	std::vector<std::array<std::int32_t, 2>> points;
	std::array<std::int32_t, 2> lo{};
	std::array<std::int32_t, 2> hi{};

	// getCenterPoint: its bounds' middle; getRadius: half its bounds' diagonal (the original's updateBounds added the
	// two y bounds where it meant their difference: a quirk fixed).
	std::array<std::int64_t, 2> CenterTimesTwo() const noexcept { return {std::int64_t{lo[0]} + hi[0], std::int64_t{lo[1]} + hi[1]}; }
	std::int64_t RadiusSquaredTimesFour() const noexcept
	{
		const std::int64_t width = std::int64_t{hi[0]} - lo[0], height = std::int64_t{hi[1]} - lo[1];
		return width * width + height * height;
	}

	bool Contains(std::int32_t x, std::int32_t y) const noexcept
	{
		if (points.empty() || x < lo[0] || y < lo[1] || x > hi[0] || y > hi[1])
			return false;
		bool inside = false;
		for (std::size_t index = 0; index < points.size(); ++index)
		{
			const auto &p1 = points[index];
			const auto &p2 = points[index + 1 == points.size() ? 0 : index + 1];
			if (p1[1] == p2[1])
				continue; // horizontal edges
			if (p1[1] < y && p2[1] < y)
				continue;
			if (p1[1] >= y && p2[1] >= y)
				continue;
			if (p1[0] < x && p2[0] < x)
				continue;
			// intersectionX = x1 + dx * (y - y1) / dy >= x  <=>  dx * (y - y1) / dy >= x - x1
			const std::int64_t dy = p2[1] - p1[1], dx = p2[0] - p1[0];
			const std::int64_t numerator = dx * (std::int64_t{y} - p1[1]), reach = (std::int64_t{x} - p1[0]) * dy;
			if (dy > 0 ? numerator >= reach : numerator <= reach)
				inside = !inside;
		}
		return inside;
	}
};

struct TriggerAreas
{
	static constexpr std::uint32_t None = 0xFFFFFFFFu;
	std::vector<TriggerArea> areas;

	void Add(std::string name, std::vector<std::array<std::int32_t, 2>> points)
	{
		TriggerArea area{std::move(name), std::move(points)};
		if (!area.points.empty())
		{
			area.lo = area.hi = area.points.front();
			for (const auto &point : area.points)
				for (std::size_t axis = 0; axis < 2; ++axis)
				{
					area.lo[axis] = std::min(area.lo[axis], point[axis]);
					area.hi[axis] = std::max(area.hi[axis], point[axis]);
				}
		}
		areas.push_back(std::move(area));
	}
	// TerrainLogic::getTriggerAreaByName: the first of the name in the list (the last read).
	std::uint32_t Find(std::string_view name) const noexcept
	{
		for (std::size_t index = areas.size(); index-- > 0;)
			if (areas[index].name == name)
				return static_cast<std::uint32_t>(index);
		return None;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::TriggerAreas>
{
	static constexpr std::string_view StableName = "engine.gameplay.trigger_areas";
};
}
