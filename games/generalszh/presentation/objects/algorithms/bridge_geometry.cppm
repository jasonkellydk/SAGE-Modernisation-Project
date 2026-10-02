export module games.generalszh.presentation.objects.algorithms.bridge_geometry;
import std;

export import games.generalszh.presentation.objects.resources.bridge_art;

// A map-drawn bridge's geometry as W3DBridgeBuffer builds it (floats, as the original):
//   MeasureBridge (W3DBridge::load): the BRIDGE_LEFT mesh's x extent and y extent at rest, BRIDGE_SPAN's and
//     BRIDGE_RIGHT's x extents; sectional when it has all three and they line up (each joint within 5% of its whole
//     length), else fixed (the sections' extents then all BRIDGE_LEFT's right end when one is missing); its length
//     right end less left end, at least 1.
//   BuildBridgeGeometry (W3DBridge::getIndicesNVertices, getModelVerticesFixed, getModelVertices, getModelIndices): a
//     model without BRIDGE_SPAN is its BRIDGE_LEFT stretched from one end to the other; otherwise the left end, as many
//     spans as fit the bridge's length (to the nearest; one for a fixed bridge), and the right end after the last, all
//     squeezed to the bridge's length; across by the line's normal times BridgeScale, up tipped by the rise along it
//     (the original's (-rise, 0, run) whatever the bridge's heading) times BridgeScale. Each vertex lit as
//     BaseHeightMapRenderObjClass::computeVertexLighting lights terrain without dynamic lights: the first terrain
//     light's ambient plus each global light's diffuse by its ray on the normal, each clamped to 0..1, to bytes
//     (truncated), opaque.
//   BridgeModelAfterChange (W3DBridgeBuffer::drawBridges): on a change of damage state the new state's model, or when
//     that cannot load the model of the state shown before (load(curState)), or none when neither loads.
export namespace generalszh::presentation
{
struct BridgeShape
{
	bool sectional{false};
	float leftMinX{0}, leftMaxX{0};
	float minY{0}, maxY{0};
	float sectionMinX{0}, sectionMaxX{0};
	float rightMinX{0}, rightMaxX{0};
	float length{1};
};

struct BridgeLight
{
	std::array<float, 3> direction{}; // where the light shines (TheGlobalData m_terrainLightPos)
	std::array<float, 3> diffuse{};
};

// The time of day's terrain lighting (GlobalData m_terrainAmbient[0], m_terrainDiffuse / m_terrainLightPos for the
// m_numGlobalLights lights).
struct BridgeLighting
{
	std::array<float, 3> ambient{};
	std::array<BridgeLight, 3> lights{};
	std::size_t count{0};
};

struct BridgeVertex
{
	std::array<float, 3> position{};
	std::uint32_t color{0xFFFFFFFFu}; // A8R8G8B8
	std::array<float, 2> uv{};
};

struct BridgeGeometry
{
	std::vector<BridgeVertex> vertices;
	std::vector<std::uint32_t> indices;
};

namespace bridge_geometry_detail
{
using Vec3 = std::array<float, 3>;

inline Vec3 Add(const Vec3 &a, const Vec3 &b) noexcept { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
inline Vec3 Sub(const Vec3 &a, const Vec3 &b) noexcept { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline Vec3 Scale(const Vec3 &a, float k) noexcept { return {a[0] * k, a[1] * k, a[2] * k}; }
inline Vec3 Divide(const Vec3 &a, float k) noexcept { return {a[0] / k, a[1] / k, a[2] / k}; }
inline float LengthSquared(const Vec3 &a) noexcept { return a[0] * a[0] + a[1] * a[1] + a[2] * a[2]; }
inline float Length(const Vec3 &a) noexcept { return std::sqrt(LengthSquared(a)); }
inline float Dot(const Vec3 &a, const Vec3 &b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Vector3::Normalized_Legacy: unchanged when of no length.
inline Vec3 Normalized(const Vec3 &a) noexcept
{
	const float squared = LengthSquared(a);
	if (squared == 0.0f)
		return a;
	const float inverse = 1.0f / std::sqrt(squared);
	return {a[0] * inverse, a[1] * inverse, a[2] * inverse};
}

// Matrix3D::Transform_Point / Transform_Vector on a row-major 3x4.
inline Vec3 TransformPoint(const std::array<float, 12> &m, const Vec3 &p) noexcept
{
	return {m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3], m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
		m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]};
}
inline Vec3 TransformVector(const std::array<float, 12> &m, const Vec3 &v) noexcept
{
	return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[4] * v[0] + m[5] * v[1] + m[6] * v[2], m[8] * v[0] + m[9] * v[1] + m[10] * v[2]};
}

// The least and greatest of one axis of a mesh's vertices placed at rest.
inline std::pair<float, float> Extent(const BridgeMesh &mesh, std::size_t axis) noexcept
{
	float low = std::numeric_limits<float>::max();
	float high = -std::numeric_limits<float>::max();
	for (const auto &position : mesh.positions)
	{
		const float value = TransformPoint(mesh.rest, position)[axis];
		if (low > value)
			low = value;
		if (value > high)
			high = value;
	}
	return {low, high};
}
}

// computeVertexLighting without dynamic lights (A8R8G8B8, opaque).
inline std::uint32_t BridgeVertexLighting(const BridgeLighting &lighting, const std::array<float, 3> &normal) noexcept
{
	using namespace bridge_geometry_detail;
	std::array<float, 3> shade = lighting.ambient;
	for (std::size_t index = 0; index < lighting.count && index < lighting.lights.size(); ++index)
	{
		const BridgeLight &light = lighting.lights[index];
		const Vec3 ray = Normalized({-light.direction[0], -light.direction[1], -light.direction[2]});
		float amount = Dot(ray, normal);
		if (amount > 1.0f)
			amount = 1.0f;
		if (amount < 0.0f)
			amount = 0.0f;
		for (std::size_t channel = 0; channel < 3; ++channel)
			shade[channel] += amount * light.diffuse[channel];
	}
	std::array<std::uint32_t, 3> bytes{};
	for (std::size_t channel = 0; channel < 3; ++channel)
	{
		float value = shade[channel];
		if (value > 1.0f)
			value = 1.0f;
		if (value < 0.0f)
			value = 0.0f;
		bytes[channel] = static_cast<std::uint32_t>(static_cast<std::int32_t>(value * 255.0f)); // REAL_TO_INT: truncated
	}
	return bytes[2] | (bytes[1] << 8) | (bytes[0] << 16) | 0xFF000000u;
}

namespace bridge_geometry_detail
{
// The three axes a section's vertices are laid along: along the bridge, across it, up.
struct Axes
{
	Vec3 along, across, up;
};

// getModelVertices / getModelIndices: one mesh's vertices placed along the bridge from `start` (offset `xOffset` along
// it), lit, and its triangles.
inline void AppendMesh(BridgeGeometry &out, const BridgeMesh &mesh, float xOffset, const Axes &axes, const Vec3 &start, const BridgeLighting &lighting)
{
	const auto base = static_cast<std::uint32_t>(out.vertices.size());
	const std::size_t count = mesh.positions.size();
	for (std::size_t index = 0; index < count; ++index)
	{
		const Vec3 vertex = TransformPoint(mesh.rest, mesh.positions[index]);
		Vec3 at = Add(Add(Scale(axes.along, vertex[0] + xOffset), Scale(axes.across, vertex[1])), Scale(axes.up, vertex[2]));
		at = Add(at, start);
		const Vec3 source = index < mesh.normals.size() ? mesh.normals[index] : Vec3{0, 0, 1};
		const Vec3 turned = TransformVector(mesh.rest, source);
		const Vec3 normal = Normalized(Add(Add(Scale(axes.along, turned[0]), Scale(axes.across, turned[1])), Scale(axes.up, turned[2])));
		BridgeVertex made;
		made.position = at;
		made.color = BridgeVertexLighting(lighting, normal);
		made.uv = index < mesh.uvs.size() ? mesh.uvs[index] : std::array<float, 2>{};
		out.vertices.push_back(made);
	}
	for (const auto &triangle : mesh.triangles)
		for (const std::uint32_t corner : triangle)
			out.indices.push_back(base + corner);
}
}

inline BridgeShape MeasureBridge(const BridgeModel &model)
{
	using namespace bridge_geometry_detail;
	BridgeShape shape;
	if (!model.left)
		return shape;
	shape.sectional = model.span.has_value() && model.right.has_value();
	const auto [leftMinX, leftMaxX] = Extent(*model.left, 0);
	const auto [minY, maxY] = Extent(*model.left, 1);
	shape.leftMinX = leftMinX;
	shape.leftMaxX = leftMaxX;
	shape.minY = minY;
	shape.maxY = maxY;
	if (shape.sectional)
	{
		std::tie(shape.sectionMinX, shape.sectionMaxX) = Extent(*model.span, 0);
		std::tie(shape.rightMinX, shape.rightMaxX) = Extent(*model.right, 0);
	}
	else
		shape.sectionMinX = shape.sectionMaxX = shape.rightMinX = shape.rightMaxX = shape.leftMaxX;
	float length = shape.rightMaxX - shape.leftMinX;
	if (length < 1.0f)
		length = 1.0f;
	shape.length = length;
	if (shape.sectional)
	{
		const float allowableError = 0.05f * length;
		if (shape.leftMaxX > shape.sectionMinX + allowableError)
			shape.sectional = false;
		if (shape.rightMinX < shape.sectionMaxX - allowableError)
			shape.sectional = false;
	}
	return shape;
}

// How many spans a sectional bridge from `start` to `end` lays (one for a fixed bridge).
inline int BridgeSpans(const BridgeShape &shape, const std::array<float, 3> &start, const std::array<float, 3> &end) noexcept
{
	using namespace bridge_geometry_detail;
	if (!shape.sectional)
		return 1;
	Vec3 along = Sub(end, start);
	if (LengthSquared(along) < 1.0f)
		along = Normalized(along);
	const float desiredLength = Length(along);
	const float spanLength = shape.rightMinX - shape.leftMaxX;
	const float spannable = desiredLength - (shape.length - spanLength);
	// REAL_TO_INT_FLOOR.
	const int spans = static_cast<int>(std::floor((spannable + spanLength / 2) / spanLength));
	return spans < 0 ? 0 : spans;
}

inline void BuildBridgeGeometry(const BridgeModel &model, const BridgeShape &shape, float scale, const std::array<float, 3> &start,
	const std::array<float, 3> &end, const BridgeLighting &lighting, BridgeGeometry &out)
{
	using namespace bridge_geometry_detail;
	if (!model.span)
	{
		// getModelVerticesFixed: BRIDGE_LEFT alone, its length the bridge's.
		if (!model.left)
			return;
		Vec3 along = Sub(end, start);
		if (LengthSquared(along) < 1.0f)
			along = Normalized(along);
		Vec3 across = Normalized({-along[1], along[0], 0.0f});
		float deltaZ = end[2] - start[2];
		deltaZ /= Length(along);
		const auto deltaX = static_cast<float>(std::sqrt(1.0 - static_cast<double>(deltaZ * deltaZ)));
		Vec3 up{-deltaZ, 0.0f, deltaX};
		along = Divide(along, shape.length);
		across = Scale(across, scale);
		up = Scale(up, scale);
		AppendMesh(out, *model.left, -shape.leftMinX, {along, across, up}, start, lighting);
	}
	else
	{
		Vec3 along = Sub(end, start);
		if (LengthSquared(along) < 1.0f)
			along = Normalized(along);
		const Vec3 across = Scale(Normalized({-along[1], along[0], 0.0f}), scale);
		// Turned about y for the rise along it.
		float deltaZ = end[2] - start[2];
		const float desiredLength = Length(along);
		deltaZ /= desiredLength;
		const auto deltaX = static_cast<float>(std::sqrt(1.0 - static_cast<double>(deltaZ * deltaZ)));
		const Vec3 up = Scale(Vec3{-deltaZ, 0.0f, deltaX}, scale);
		const float spanLength = shape.rightMinX - shape.leftMaxX;
		const int spans = BridgeSpans(shape, start, end);
		const float bridgeLength = shape.length + static_cast<float>(spans - 1) * spanLength;
		const float xOffset = -shape.leftMinX;
		along = Divide(along, bridgeLength);
		const Axes axes{along, across, up};
		if (!model.left)
			return;
		AppendMesh(out, *model.left, xOffset, axes, start, lighting);
		for (int span = 0; span < spans; ++span)
			AppendMesh(out, *model.span, xOffset + static_cast<float>(span) * spanLength, axes, start, lighting);
		// The right end after the last span (none: the original stops there).
		if (!model.right)
			return;
		AppendMesh(out, *model.right, xOffset + static_cast<float>(spans - 1) * spanLength, axes, start, lighting);
	}
}

inline bool BridgeModelLoads(const BridgeKind &kind, std::uint8_t state) noexcept
{
	return state < kind.states.size() && kind.states[state].loaded;
}

inline std::uint8_t BridgeModelAfterChange(const BridgeKind &kind, std::uint8_t shownBefore, std::uint8_t now) noexcept
{
	if (BridgeModelLoads(kind, now))
		return now;
	if (BridgeModelLoads(kind, shownBefore))
		return shownBefore;
	return NoBridgeModel;
}
}
