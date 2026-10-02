export module games.generalszh.presentation.objects.algorithms.rally_lines;
import std;

export import games.generalszh.presentation.objects.resources.waypoint_paths;
export import games.generalszh.presentation.objects.resources.look_catalog;

// W3DWaypointBuffer::drawWaypoints out of waypoint mode (GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/
// W3dWaypointBuffer.cpp): a selected factory's rally line, from its door (getExitPosition) through its natural rally
// point (getNaturalRallyPoint without the offset) and, when the rally point lies back past the building, the corners of
// its footprint the line wraps round (the near elbow, and the far one when the rally point flanks that too), to the
// rally point; the SCMNode "hockey puck" at each elbow and at the natural rally point. The line's float maths as the
// original's Coord3D (3D lengths where it took them).
export namespace generalszh::presentation
{
using Point3 = std::array<float, 3>;

// One line through `points` (m_line->Set_Points, drawLine): EXLaser.tga tiled along it, a texture's width per `width`.
inline void AppendPathLine(WaypointPaths &paths, std::span<const Point3> points, float width, const std::array<float, 4> &color)
{
	float along = 0.0f;
	for (std::size_t index = 1; index < points.size(); ++index)
	{
		const Point3 &from = points[index - 1], &to = points[index];
		const float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
		const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
		paths.segments.push_back({from, to, width, color, WaypointPaths::Texture, length / width, along / width});
		along += length;
	}
}

// m_waypointNodeRobj at `at` (the little hockey puck).
inline void AppendWaypointNode(WaypointPaths &paths, const Point3 &at, std::uint32_t nodeLook)
{
	if (nodeLook == LookCatalog::NoLook)
		return;
	ObjectInstance node;
	node.look = nodeLook;
	node.clipLook = nodeLook;
	node.world = {1, 0, 0, at[0], 0, 1, 0, at[1], 0, 0, 1, at[2], 0, 0, 0, 1};
	paths.nodes.push_back(node);
}

namespace rally_line_detail
{
inline float Length(float x, float y, float z) noexcept
{
	return std::sqrt(x * x + y * y + z * z);
}
// Coord3D::normalize: unchanged when of no length.
inline void Normalize(float &x, float &y, float &z) noexcept
{
	const float length = Length(x, y, z);
	if (length != 0.0f)
	{
		x /= length;
		y /= length;
		z /= length;
	}
}
}

// The factory standing at `center` facing `orientation` (radians), its footprint `majorRadius` by `minorRadius`: its
// rally line from `exitPoint` by `natural` to `rally`, and its nodes. `boxWrap` false: its natural rally point is its door
// (a helipad's), the line goes straight on.
inline void AppendRallyLine(WaypointPaths &paths, const Point3 &exitPoint, const Point3 &natural, const Point3 &rally, const Point3 &center,
	float orientation, float majorRadius, float minorRadius, std::uint32_t nodeLook)
{
	using rally_line_detail::Length;
	using rally_line_detail::Normalize;
	std::array<Point3, 5> points{};
	std::size_t count = 0;
	points[count++] = exitPoint;
	const bool boxWrap = !(natural == exitPoint);
	if (boxWrap)
		points[count++] = natural;
	if (boxWrap)
	{
		// The natural rally point's way out of the door, and a point as good as infinitely far along it.
		float wayX = natural[0] - exitPoint[0], wayY = natural[1] - exitPoint[1], wayZ = 0.0f;
		Normalize(wayX, wayY, wayZ);
		wayX *= 99999.9f;
		wayY *= 99999.9f;
		wayZ *= 99999.9f;
		const float wayOutLength = Length(wayX, wayY, wayZ);
		wayX += natural[0];
		wayY += natural[1];
		wayZ += natural[2];
		// The quick test: a rally point nearer the far point than the natural rally point is never wrapped.
		if (100.0f + Length(wayX - rally[0], wayY - rally[1], wayZ - rally[2]) > wayOutLength)
		{
			Normalize(wayX, wayY, wayZ); // a normal shooting straight out the door
			float toRallyX = natural[0] - rally[0], toRallyY = natural[1] - rally[1], toRallyZ = natural[2] - rally[2];
			Normalize(toRallyX, toRallyY, toRallyZ);
			float dot = toRallyX * wayX + toRallyY * wayY;
			if (dot > 0.0f)
			{
				const float c = std::cos(orientation), s = std::sin(orientation);
				const float exc = majorRadius * c, eyc = minorRadius * c, exs = majorRadius * s, eys = minorRadius * s;
				const std::array<std::array<float, 2>, 4> corners{{{center[0] - exc - eys, center[1] + eyc - exs},
					{center[0] + exc - eys, center[1] + eyc + exs}, {center[0] + exc + eys, center[1] - eyc + exs},
					{center[0] - exc + eys, center[1] - eyc - exs}}};
				const std::array<float, 2> *nearElbow = nullptr; // nearest the rally point, the door's end
				const std::array<float, 2> *farElbow = nullptr;  // nearest the rally point, away from the door
				float nearDistance = 99999.9f, farDistance = 99999.9f;
				for (const auto &corner : corners)
				{
					float toExitX = exitPoint[0] - corner[0], toExitY = exitPoint[1] - corner[1], toExitZ = 0.0f;
					Normalize(toExitX, toExitY, toExitZ);
					dot = toExitX * wayX + toExitY * wayY;
					const float distance = Length(rally[0] - corner[0], rally[1] - corner[1], 0.0f);
					if (dot < 0.0f)
					{
						if (distance < nearDistance)
						{
							nearDistance = distance;
							nearElbow = &corner;
						}
					}
					else if (distance < farDistance)
					{
						farDistance = distance;
						farElbow = &corner;
					}
				}
				if (nearElbow != nullptr)
				{
					const Point3 elbow{(*nearElbow)[0], (*nearElbow)[1], center[2]};
					AppendWaypointNode(paths, elbow, nodeLook);
					points[count++] = elbow;
					if (farElbow != nullptr)
					{
						// Does the rally point wrap round this corner too?
						float firstX = natural[0] - (*nearElbow)[0], firstY = natural[1] - (*nearElbow)[1], firstZ = 0.0f;
						Normalize(firstX, firstY, firstZ);
						float toRpX = (*nearElbow)[0] - rally[0], toRpY = (*nearElbow)[1] - rally[1], toRpZ = 0.0f;
						Normalize(toRpX, toRpY, toRpZ);
						dot = toRpX * firstX + toRpY * firstY;
						if (dot < 0.0f)
						{
							const Point3 second{(*farElbow)[0], (*farElbow)[1], center[2]};
							AppendWaypointNode(paths, second, nodeLook);
							points[count++] = second;
						}
					}
				}
			}
		}
	}
	points[count++] = rally;
	AppendWaypointNode(paths, natural, nodeLook);
	AppendPathLine(paths, std::span<const Point3>(points.data(), count), WaypointPaths::Width, WaypointPaths::Color);
}

// A listening outpost's view of an enemy's way (REVEALS_ENEMY_PATHS): from the enemy through `points`, an orange line
// (0.95, 0.5, 0) three wide, with a node at each point.
inline constexpr std::array<float, 4> EnemyPathColor{0.95f, 0.5f, 0.0f, 1.0f};
inline constexpr float EnemyPathWidth = 3.0f;
inline void AppendEnemyPath(WaypointPaths &paths, const Point3 &from, std::span<const Point3> points, std::uint32_t nodeLook)
{
	if (points.empty())
		return;
	std::vector<Point3> line;
	line.reserve(points.size() + 1);
	line.push_back(from);
	for (const Point3 &point : points)
	{
		if (line.size() < WaypointPaths::MaxDisplayNodes + 1)
			line.push_back(point);
		AppendWaypointNode(paths, point, nodeLook);
	}
	AppendPathLine(paths, line, EnemyPathWidth, EnemyPathColor);
}
}
