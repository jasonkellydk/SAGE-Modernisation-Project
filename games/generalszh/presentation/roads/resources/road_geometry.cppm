export module games.generalszh.presentation.roads.resources.road_geometry;
import std;

import engine.ecs.system.system;

// The map's roads as the road buffer draws them (W3DRoadBuffer): one mesh per road type that has any segments, in the
// order they are drawn (by stacking order, then the road buffer's type order), each its Roads.ini texture and its
// segments' vertices (their place, their texture coordinates, and the height map vertex whose static terrain lighting
// colours them: RoadSegment::updateSegLighting) and triangles. Built once for a map (W3DRoadBuffer::loadRoads); frames
// only read it, and the renderer lights it with the time of day's terrain lighting.
export namespace generalszh::presentation
{
struct RoadVertex
{
	std::array<float, 3> position{};
	std::array<float, 2> uv{};
	std::array<std::int32_t, 2> lightCell{}; // the height map vertex (border included) getStaticDiffuse samples
};

struct RoadMesh
{
	std::uint32_t id{0};    // the road type (TerrainRoadType's id)
	std::string texture;    // Texture
	std::uint32_t stacking{0};
	std::vector<RoadVertex> vertices;
	std::vector<std::uint32_t> indices;
};

struct RoadGeometry
{
	std::vector<RoadMesh> meshes; // in drawing order
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::RoadGeometry>
{
	static constexpr std::string_view StableName = "generalszh.presentation.road_geometry";
};
}
