export module engine.gameplay.common.spatial.resources.ground_height;
export import engine.core.serialization.byte_stream;
import std;

export import engine.level.model.level;
import engine.ecs.system.system;

// The ground height under a world position, from the level's height field:
// each cell is split along its (x, y) -> (x + 1, y + 1) diagonal and the
// triangle under the point is interpolated. World (0, 0) is the playable
// corner, `border` samples in from the field's corner. Water areas are
// flat polygons at a height; the surface is the higher of ground and water.
export namespace engine::gameplay
{
struct WaterArea
{
	std::vector<Engine::Math::FixedVector2> outline;
	Engine::Math::Fixed height;
	Engine::Math::FixedVector2 low;
	Engine::Math::FixedVector2 high;
	std::string name; // its polygon's (a script changes its height by it)

	bool Contains(Engine::Math::FixedVector2 point) const noexcept
	{
		if (outline.size() < 3 || point.x < low.x || point.y < low.y || point.x > high.x || point.y > high.y)
			return false;
		// Even-odd crossing test.
		bool inside = false;
		for (std::size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++)
		{
			const auto &a = outline[i];
			const auto &b = outline[j];
			if ((a.y > point.y) != (b.y > point.y))
			{
				const Engine::Math::Fixed crossX = a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y);
				if (point.x < crossX)
					inside = !inside;
			}
		}
		return inside;
	}
};

class GroundHeight
{
public:
	explicit GroundHeight(const engine::level::Heightfield &field) noexcept : m_field(&field)
	{
		// BaseHeightMapRenderObjClass::m_maxHeight: the highest sample.
		for (const Engine::Math::Fixed sample : field.heights)
			m_maxHeight = std::max(m_maxHeight, sample);
	}

	// The playable boundaries the level gives (none: one, the field less its border), and which is active.
	std::size_t BoundaryCount() const noexcept { return m_field->playableExtents.size(); }
	std::array<std::int32_t, 2> Boundary(std::size_t index) const noexcept { return m_field->playableExtents[index]; }
	std::uint32_t ActiveBoundary() const noexcept { return m_activeBoundary; }
	void SetActiveBoundary(std::uint32_t index) noexcept { m_activeBoundary = index; }

	void AddWater(std::vector<Engine::Math::FixedVector2> outline, Engine::Math::Fixed height, std::string name = {})
	{
		if (outline.empty())
			return;
		WaterArea area{std::move(outline), height, {}, {}, std::move(name)};
		area.low = area.outline.front();
		area.high = area.outline.front();
		for (const auto &point : area.outline)
		{
			area.low = {std::min(area.low.x, point.x), std::min(area.low.y, point.y)};
			area.high = {std::max(area.high.x, point.x), std::max(area.high.y, point.y)};
		}
		m_water.push_back(std::move(area));
	}

	// Height of the highest water area covering a point, if any.
	bool Water(Engine::Math::FixedVector2 position, Engine::Math::Fixed &height) const noexcept
	{
		bool found = false;
		for (const WaterArea &area : m_water)
			if (area.Contains(position) && (!found || area.height > height))
			{
				height = area.height;
				found = true;
			}
		return found;
	}

	// The higher of ground and water.
	Engine::Math::Fixed Surface(Engine::Math::FixedVector2 position) const noexcept
	{
		const Engine::Math::Fixed ground = At(position);
		Engine::Math::Fixed water;
		return Water(position, water) ? std::max(ground, water) : ground;
	}

	const std::vector<WaterArea> &WaterAreas() const noexcept { return m_water; }

	// TerrainLogic::getWaterHandleByName: the water area of that name.
	std::optional<std::size_t> WaterNamed(std::string_view name) const noexcept
	{
		for (std::size_t index = 0; index < m_water.size(); ++index)
			if (m_water[index].name == name)
				return index;
		return std::nullopt;
	}
	// setWaterHeight on a polygon's water: its points' height (whole units, toward zero, as the polygon's integer points).
	void SetWaterHeight(std::size_t index, Engine::Math::Fixed height) noexcept
	{
		if (index < m_water.size())
			m_water[index].height = Engine::Math::Fixed::FromInt(height >= Engine::Math::Fixed{} ? height.Floor() : -(Engine::Math::Fixed{} - height).Floor());
	}
	// TerrainLogic::isUnderwater: in water whose surface is above the ground there.
	bool Underwater(Engine::Math::FixedVector2 position) const noexcept
	{
		Engine::Math::Fixed water;
		return Water(position, water) && At(position) < water;
	}
	// The water areas' heights (a checkpoint's: what scripts made of them).
	void SaveWater(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_water.size()));
		for (const WaterArea &area : m_water)
			writer.I64(area.height.Raw());
	}
	bool LoadWater(engine::core::serialization::ByteReader &reader)
	{
		const auto count = reader.U32();
		if (!count || *count != m_water.size())
			return false;
		for (WaterArea &area : m_water)
		{
			const auto raw = reader.I64();
			if (!raw)
				return false;
			area.height = Engine::Math::Fixed::FromRaw(*raw);
		}
		return true;
	}

	Engine::Math::Fixed At(Engine::Math::FixedVector2 position) const noexcept
	{
		using Engine::Math::Fixed;
		const auto &field = *m_field;
		if (field.width < 2 || field.height < 2 || field.cellSize <= Fixed{})
			return {};
		const Fixed gx = position.x / field.cellSize + Fixed::FromInt(field.border);
		const Fixed gy = position.y / field.cellSize + Fixed::FromInt(field.border);
		const std::int64_t maxX = field.width - 2, maxY = field.height - 2;
		const std::int64_t ix = std::clamp<std::int64_t>(gx.Floor(), 0, maxX);
		const std::int64_t iy = std::clamp<std::int64_t>(gy.Floor(), 0, maxY);
		const Fixed fx = std::clamp(gx - Fixed::FromInt(ix), Fixed{}, Fixed::One());
		const Fixed fy = std::clamp(gy - Fixed::FromInt(iy), Fixed{}, Fixed::One());
		const auto x = static_cast<std::uint32_t>(ix), y = static_cast<std::uint32_t>(iy);
		const Fixed p0 = field.At(x, y), p2 = field.At(x + 1, y + 1);
		if (fy > fx)
		{
			const Fixed p3 = field.At(x, y + 1);
			return p3 + (Fixed::One() - fy) * (p0 - p3) + fx * (p2 - p3);
		}
		const Fixed p1 = field.At(x + 1, y);
		return p1 + fy * (p2 - p1) + (Fixed::One() - fx) * (p0 - p1);
	}

	// TerrainLogic::getExtent: the playable area from the origin, its active boundary (the first the level gives until a
	// script switches it; none: the field less its border).
	std::array<Engine::Math::FixedVector2, 2> Extent() const noexcept
	{
		using Engine::Math::Fixed;
		const auto &field = *m_field;
		if (!field.playableExtents.empty())
		{
			const auto &boundary = field.playableExtents[std::min<std::size_t>(m_activeBoundary, field.playableExtents.size() - 1)];
			return {Engine::Math::FixedVector2{}, {field.cellSize * Fixed::FromInt(boundary[0]), field.cellSize * Fixed::FromInt(boundary[1])}};
		}
		return {Engine::Math::FixedVector2{}, {field.cellSize * Fixed::FromInt(static_cast<std::int64_t>(field.width) - 1 - 2 * field.border),
												 field.cellSize * Fixed::FromInt(static_cast<std::int64_t>(field.height) - 1 - 2 * field.border)}};
	}
	// getExtentIncludingBorder: the whole field, the border behind the origin.
	std::array<Engine::Math::FixedVector2, 2> ExtentIncludingBorder() const noexcept
	{
		using Engine::Math::Fixed;
		const auto &field = *m_field;
		const Fixed border = field.cellSize * Fixed::FromInt(field.border);
		return {Engine::Math::FixedVector2{Fixed{} - border, Fixed{} - border},
			{field.cellSize * Fixed::FromInt(field.width) - border, field.cellSize * Fixed::FromInt(field.height) - border}};
	}
	// findClosestEdgePoint: the point straight out to the nearest edge of the playable area (top, right, bottom, left
	// on ties), on the ground.
	Engine::Math::FixedVector3 ClosestEdgePoint(Engine::Math::FixedVector2 to) const noexcept
	{
		const auto [low, high] = Extent();
		const Engine::Math::Fixed distances[] = {Engine::Math::Abs(to.y - low.y), Engine::Math::Abs(to.x - high.x), Engine::Math::Abs(to.y - high.y),
			Engine::Math::Abs(to.x - low.x)};
		std::size_t best = 0;
		for (std::size_t index = 1; index < 4; ++index)
			if (distances[index] < distances[best])
				best = index;
		Engine::Math::FixedVector2 point = to;
		if (best == 0)
			point.y = low.y;
		else if (best == 1)
			point.x = high.x;
		else if (best == 2)
			point.y = high.y;
		else
			point.x = low.x;
		return {point.x, point.y, At(point)};
	}

	// TerrainLogic::isClearLineOfSight (BaseHeightMapRenderObjClass::isClearLineOfSight, DO_BRESENHAM): a Bresenham walk
	// over the height field's cells from `from` to `to`, the sight line's height moving evenly between theirs; blocked
	// where a cell's highest corner rises more than LOS_FUDGE (0.5) over the line. Off the field the walk ends (clear);
	// above the highest terrain and still rising, likewise.
	bool ClearLineOfSight(Engine::Math::FixedVector3 from, Engine::Math::FixedVector3 to) const noexcept
	{
		using Engine::Math::Fixed;
		const auto &field = *m_field;
		if (field.width < 2 || field.height < 2 || field.cellSize <= Fixed{})
			return false;
		const auto border = static_cast<std::int64_t>(field.border);
		const std::int64_t startX = (from.x / field.cellSize).Floor() + border, startY = (from.y / field.cellSize).Floor() + border;
		const std::int64_t endX = (to.x / field.cellSize).Floor() + border, endY = (to.y / field.cellSize).Floor() + border;
		const std::int64_t deltaX = endX >= startX ? endX - startX : startX - endX;
		const std::int64_t deltaY = endY >= startY ? endY - startY : startY - endY;
		std::int64_t x = startX, y = startY;
		std::int64_t xStep1 = endX >= startX ? 1 : -1, xStep2 = xStep1;
		std::int64_t yStep1 = endY >= startY ? 1 : -1, yStep2 = yStep1;
		std::int64_t denominator = 0, numerator = 0, add = 0, pixels = 0;
		if (deltaX >= deltaY)
		{
			xStep1 = 0;
			yStep2 = 0;
			denominator = deltaX;
			numerator = deltaX / 2;
			add = deltaY;
			pixels = deltaX;
		}
		else
		{
			xStep2 = 0;
			yStep1 = 0;
			denominator = deltaY;
			numerator = deltaY / 2;
			add = deltaX;
			pixels = deltaY;
		}
		if (pixels <= 0)
			return true;
		Fixed z = from.z;
		const Fixed rise = (to.z - from.z) / Fixed::FromInt(pixels);
		const Fixed fudge = Fixed::FromRatio(1, 2);
		const auto width = static_cast<std::int64_t>(field.width), height = static_cast<std::int64_t>(field.height);
		for (std::int64_t pixel = 0; pixel < pixels; ++pixel)
		{
			if (x < 0 || y < 0 || x >= width - 1 || y >= height - 1)
				break;
			const auto cx = static_cast<std::uint32_t>(x), cy = static_cast<std::uint32_t>(y);
			const Fixed top = std::max({field.At(cx, cy), field.At(cx + 1, cy), field.At(cx, cy + 1), field.At(cx + 1, cy + 1)});
			if (top > z + fudge)
				return false;
			if (z >= m_maxHeight && rise > Fixed{})
				break;
			z += rise;
			numerator += add;
			if (numerator >= denominator)
			{
				numerator -= denominator;
				x += xStep1;
				y += yStep1;
			}
			x += xStep2;
			y += yStep2;
		}
		return true;
	}

private:
	const engine::level::Heightfield *m_field;
	std::uint32_t m_activeBoundary{0}; // TerrainLogic::m_activeBoundary (checkpointed by its owner)
	Engine::Math::Fixed m_maxHeight;
	std::vector<WaterArea> m_water;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::GroundHeight>
{
	static constexpr std::string_view StableName = "engine.gameplay.ground_height";
};
}
