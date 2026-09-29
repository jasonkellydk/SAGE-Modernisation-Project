export module engine.level.presentation.terrain_mesh;
import std;

export import engine.level.model.level;
import Engine.Core.Math.FixedPresentation;

// Level terrain -> renderable quads (renderer-neutral), following the
// original terrain visual: one cell per heightmap quad, corners
// counter-clockwise from (x, y), world origin at the inner edge of the
// border, per-corner normals and static lighting from the level's
// time-of-day terrain lights. Texturing is layered on separately.
export namespace engine::level::presentation
{
struct TerrainMeshCell
{
	std::array<float, 2> origin{};
	std::array<float, 2> spacing{};
	std::array<float, 4> heights{};
	std::array<std::array<float, 3>, 4> normals{};
	std::array<std::array<float, 3>, 4> colors{}; // static diffuse RGB in [0, 1]
};

// Normal from central differences, as the original surface shader input.
inline std::array<float, 3> SurfaceNormal(float left, float right, float below, float above, float spacing) noexcept
{
	const float x = (left - right) / (2.0f * spacing);
	const float y = (below - above) / (2.0f * spacing);
	const float inverseLength = 1.0f / std::sqrt(x * x + y * y + 1.0f);
	return {x * inverseLength, y * inverseLength, inverseLength};
}

struct TerrainLightingInput
{
	const LightingSet *set{nullptr};
	// Number of global lights the original applies (GameData NumberGlobalLights).
	std::size_t lightCount{3};
};

class TerrainMesh
{
public:
	TerrainMesh(const Level &level, TerrainLightingInput lighting) :
		m_level(level), m_lighting(lighting),
		m_width(static_cast<int>(level.terrain.width)), m_height(static_cast<int>(level.terrain.height)),
		m_border(static_cast<int>(level.terrain.border)),
		m_cellSize(Engine::Math::ToFloat(level.terrain.cellSize))
	{
	}

	float Height(int x, int y) const
	{
		x = std::clamp(x, 0, m_width - 1);
		y = std::clamp(y, 0, m_height - 1);
		return Engine::Math::ToFloat(m_level.terrain.At(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)));
	}

	// Height at a world position (origin at the playable corner): the cell's
	// triangle under the point, split along its (x, y) -> (x + 1, y + 1)
	// diagonal as the simulation's ground height.
	float HeightAt(float worldX, float worldY) const
	{
		const float gx = worldX / m_cellSize + static_cast<float>(m_border);
		const float gy = worldY / m_cellSize + static_cast<float>(m_border);
		const int x = static_cast<int>(std::floor(gx)), y = static_cast<int>(std::floor(gy));
		const float fx = gx - static_cast<float>(x), fy = gy - static_cast<float>(y);
		const float p0 = Height(x, y), p2 = Height(x + 1, y + 1);
		if (fy > fx)
		{
			const float p3 = Height(x, y + 1);
			return p3 + (1.0f - fy) * (p0 - p3) + fx * (p2 - p3);
		}
		const float p1 = Height(x + 1, y);
		return p1 + fy * (p2 - p1) + (1.0f - fx) * (p0 - p1);
	}

	// Static diffuse RGB in [0, 1], as the original computes it per vertex.
	std::array<float, 3> StaticDiffuse(int x, int y) const
	{
		x = std::clamp(x, 0, m_width - 1);
		y = std::clamp(y, 0, m_height - 1);
		const int left = std::max(x - 1, 0), right = std::min(x + 1, m_width - 1);
		const int below = std::max(y - 1, 0), above = std::min(y + 1, m_height - 1);
		// l2r x n2f, normalized (the original's per-texel normal).
		const std::array<float, 3> l2r{2 * m_cellSize, 0, Height(right, y) - Height(left, y)};
		const std::array<float, 3> n2f{0, 2 * m_cellSize, Height(x, above) - Height(x, below)};
		std::array<float, 3> normal{l2r[1] * n2f[2] - l2r[2] * n2f[1], l2r[2] * n2f[0] - l2r[0] * n2f[2], l2r[0] * n2f[1] - l2r[1] * n2f[0]};
		const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
		if (length > 0)
			for (float &component : normal)
				component /= length;

		std::array<float, 3> shade{};
		if (m_lighting.set == nullptr || m_lighting.set->terrain.empty())
			return {1, 1, 1};
		// Only the first terrain light contributes ambient.
		for (std::size_t channel = 0; channel < 3; ++channel)
			shade[channel] = Engine::Math::ToFloat(m_lighting.set->terrain[0].ambient[channel]);
		const std::size_t lights = std::min(m_lighting.lightCount, m_lighting.set->terrain.size());
		for (std::size_t index = 0; index < lights; ++index)
		{
			const Light &light = m_lighting.set->terrain[index];
			const std::array<float, 3> ray{-Engine::Math::ToFloat(light.direction.x), -Engine::Math::ToFloat(light.direction.y),
				-Engine::Math::ToFloat(light.direction.z)};
			const float intensity = std::clamp(ray[0] * normal[0] + ray[1] * normal[1] + ray[2] * normal[2], 0.0f, 1.0f);
			for (std::size_t channel = 0; channel < 3; ++channel)
				shade[channel] += intensity * Engine::Math::ToFloat(light.diffuse[channel]);
		}
		for (float &channel : shade)
			channel = std::floor(std::clamp(channel, 0.0f, 1.0f) * 255.0f) / 255.0f;
		return shade;
	}

	// All cells, row-major, (width - 1) * (height - 1) of them.
	std::vector<TerrainMeshCell> BuildCells() const
	{
		constexpr int cornerX[4]{0, 1, 1, 0};
		constexpr int cornerY[4]{0, 0, 1, 1};
		std::vector<TerrainMeshCell> cells;
		if (m_width < 2 || m_height < 2)
			return cells;
		cells.reserve(static_cast<std::size_t>(m_width - 1) * static_cast<std::size_t>(m_height - 1));
		for (int y = 0; y < m_height - 1; ++y)
		{
			for (int x = 0; x < m_width - 1; ++x)
			{
				TerrainMeshCell cell;
				cell.origin = {static_cast<float>(x - m_border) * m_cellSize, static_cast<float>(y - m_border) * m_cellSize};
				cell.spacing = {m_cellSize, m_cellSize};
				for (int corner = 0; corner < 4; ++corner)
				{
					const int cx = x + cornerX[corner];
					const int cy = y + cornerY[corner];
					cell.heights[corner] = Height(cx, cy);
					cell.normals[corner] = SurfaceNormal(Height(cx - 1, cy), Height(cx + 1, cy),
						Height(cx, cy - 1), Height(cx, cy + 1), m_cellSize);
					const auto diffuse = StaticDiffuse(cx, cy);
					cell.colors[corner] = diffuse;
				}
				cells.push_back(cell);
			}
		}
		return cells;
	}

	// World-space extent of the playable area (inside the border).
	std::array<float, 2> PlayableSize() const
	{
		return {static_cast<float>(m_width - 1 - 2 * m_border) * m_cellSize, static_cast<float>(m_height - 1 - 2 * m_border) * m_cellSize};
	}

private:
	const Level &m_level;
	TerrainLightingInput m_lighting;
	int m_width;
	int m_height;
	int m_border;
	float m_cellSize;
};
}
