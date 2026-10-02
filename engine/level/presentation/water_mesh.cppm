export module engine.level.presentation.water_mesh;
import std;

export import engine.level.model.level;
import Engine.Core.Math.FixedPresentation;

// Level water regions -> renderable triangle lists (renderer-neutral). Each
// water or river polygon becomes a flat-shaded surface at its authored
// heights; polygons may be concave, so they are ear clipped. UVs are world
// space (position * uvScale), so adjacent polygons tile seamlessly.
export namespace engine::level::presentation
{
struct WaterMeshVertex
{
	std::array<float, 3> position{};
	std::array<float, 2> uv{};
};

struct WaterSurfaceMesh
{
	std::uint32_t regionId{0};
	bool river{false};
	float height{0.0f}; // the polygon's first point, as the original water height query
	std::vector<WaterMeshVertex> vertices;
	std::vector<std::uint32_t> indices; // triangle list, counter-clockwise seen from above
};

struct WaterMeshOptions
{
	float uvScale{1.0f / 150.0f};
	bool includeRivers{true};
};

// Triangulates a simple polygon (either winding, convex or concave) by ear
// clipping. Returns indices into `points`, three per triangle, each
// triangle counter-clockwise. Repeated and collinear points are skipped; a
// self-intersecting outline still yields triangles covering its vertices.
std::vector<std::uint32_t> TriangulatePolygon(std::span<const std::array<float, 2>> points);

// One surface per water region with at least three points.
std::vector<WaterSurfaceMesh> BuildWaterSurfaces(const Level &level, const WaterMeshOptions &options = {});
}

namespace engine::level::presentation
{
namespace
{
using Point = std::array<float, 2>;

float Cross(const Point &a, const Point &b, const Point &c) noexcept
{
	return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

// Inside or on the edge of the counter-clockwise triangle abc.
bool InTriangle(const Point &p, const Point &a, const Point &b, const Point &c) noexcept
{
	return Cross(a, b, p) >= 0.0f && Cross(b, c, p) >= 0.0f && Cross(c, a, p) >= 0.0f;
}
}

std::vector<std::uint32_t> TriangulatePolygon(std::span<const std::array<float, 2>> points)
{
	std::vector<std::uint32_t> triangles;
	// Working outline without consecutive duplicates (including last == first).
	std::vector<std::uint32_t> ring;
	ring.reserve(points.size());
	for (std::uint32_t index = 0; index < points.size(); ++index)
		if (ring.empty() || points[ring.back()] != points[index])
			ring.push_back(index);
	while (ring.size() > 1 && points[ring.back()] == points[ring.front()])
		ring.pop_back();
	if (ring.size() < 3)
		return triangles;

	double twiceArea = 0.0;
	for (std::size_t index = 0; index < ring.size(); ++index)
	{
		const Point &a = points[ring[index]];
		const Point &b = points[ring[(index + 1) % ring.size()]];
		twiceArea += static_cast<double>(a[0]) * b[1] - static_cast<double>(b[0]) * a[1];
	}
	if (twiceArea == 0.0)
		return triangles;
	if (twiceArea < 0.0)
		std::reverse(ring.begin(), ring.end());

	// Tolerance relative to the polygon's extent, for collinearity tests.
	float extent = 0.0f;
	for (const std::uint32_t index : ring)
		extent = (std::max)({extent, std::fabs(points[index][0]), std::fabs(points[index][1])});
	const float epsilon = (std::max)(extent, 1.0f) * (std::max)(extent, 1.0f) * 1e-7f;

	triangles.reserve((ring.size() - 2) * 3);
	std::size_t guard = ring.size() * ring.size() + 8;
	std::size_t current = 0;
	while (ring.size() > 3 && guard-- > 0)
	{
		const std::size_t count = ring.size();
		bool clipped = false;
		for (std::size_t step = 0; step < count; ++step)
		{
			const std::size_t middle = (current + step) % count;
			const std::size_t previous = (middle + count - 1) % count;
			const std::size_t next = (middle + 1) % count;
			const Point &a = points[ring[previous]];
			const Point &b = points[ring[middle]];
			const Point &c = points[ring[next]];
			const float turn = Cross(a, b, c);
			if (std::fabs(turn) <= epsilon)
			{
				// Collinear (or spike): drop the vertex without a triangle.
				ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(middle));
				current = middle % ring.size();
				clipped = true;
				break;
			}
			if (turn < 0.0f)
				continue; // reflex
			bool ear = true;
			for (std::size_t other = 0; other < count && ear; ++other)
			{
				if (other == previous || other == middle || other == next)
					continue;
				const Point &p = points[ring[other]];
				if (p == a || p == b || p == c)
					continue;
				ear = !InTriangle(p, a, b, c);
			}
			if (!ear)
				continue;
			triangles.insert(triangles.end(), {ring[previous], ring[middle], ring[next]});
			ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(middle));
			current = middle % ring.size();
			clipped = true;
			break;
		}
		if (!clipped)
		{
			// No ear (self-intersecting outline): clip the first convex corner
			// anyway so the remaining vertices are still covered.
			std::size_t middle = 0;
			for (std::size_t index = 0; index < count; ++index)
				if (Cross(points[ring[(index + count - 1) % count]], points[ring[index]], points[ring[(index + 1) % count]]) > 0.0f)
				{
					middle = index;
					break;
				}
			triangles.insert(triangles.end(), {ring[(middle + count - 1) % count], ring[middle], ring[(middle + 1) % count]});
			ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(middle));
			current = 0;
		}
	}
	if (ring.size() == 3 && std::fabs(Cross(points[ring[0]], points[ring[1]], points[ring[2]])) > epsilon)
	{
		if (Cross(points[ring[0]], points[ring[1]], points[ring[2]]) > 0.0f)
			triangles.insert(triangles.end(), {ring[0], ring[1], ring[2]});
		else
			triangles.insert(triangles.end(), {ring[0], ring[2], ring[1]});
	}
	return triangles;
}

std::vector<WaterSurfaceMesh> BuildWaterSurfaces(const Level &level, const WaterMeshOptions &options)
{
	std::vector<WaterSurfaceMesh> surfaces;
	for (const Region &region : level.regions)
	{
		if (!region.water && !region.river)
			continue;
		if (region.river && !options.includeRivers)
			continue;
		if (region.points.size() < 3)
			continue;
		WaterSurfaceMesh surface;
		surface.regionId = region.id;
		surface.river = region.river;
		surface.height = Engine::Math::ToFloat(region.points.front().z);
		std::vector<Point> outline;
		outline.reserve(region.points.size());
		surface.vertices.reserve(region.points.size());
		for (const auto &point : region.points)
		{
			const float x = Engine::Math::ToFloat(point.x);
			const float y = Engine::Math::ToFloat(point.y);
			outline.push_back({x, y});
			surface.vertices.push_back({{x, y, Engine::Math::ToFloat(point.z)}, {x * options.uvScale, y * options.uvScale}});
		}
		surface.indices = TriangulatePolygon(outline);
		if (!surface.indices.empty())
			surfaces.push_back(std::move(surface));
	}
	return surfaces;
}
}
