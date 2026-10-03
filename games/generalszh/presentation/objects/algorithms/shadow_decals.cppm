export module games.generalszh.presentation.objects.algorithms.shadow_decals;
import std;

// An object's SHADOW_DECAL shadow (W3DModelDraw::allocateShadows -> W3DProjectedShadowManager::addShadow): its
// ShadowTexture (".tga" added; one letter or none: "shadow.tga") laid on the terrain under it, multiplied in, turned
// with it and stretched over ShadowSizeX by ShadowSizeY (none: its model's box, twice its extents), moved by
// ShadowOffsetX / ShadowOffsetY; drawn while the object is (queueDecal: only for a visible render object).
export namespace generalszh::presentation
{
// A thing's decal shadow as its template gives it.
struct ShadowDecalLook
{
	std::string texture; // with ".tga"
	float sizeX{0.0f};
	float sizeY{0.0f};
	float offsetX{0.0f};
	float offsetY{0.0f};
};

// ThingTemplate::validate's default (a sphere or cylinder "shadow", a box "shadows") when none is given, then
// addShadow's file: one letter or none is "shadow.tga", else the name and ".tga".
inline std::string ShadowDecalTexture(std::string_view given, bool boxGeometry)
{
	std::string name(given);
	if (name.empty())
		name = boxGeometry ? "shadows" : "shadow";
	if (name.size() <= 1)
		return "shadow.tga";
	return name + ".tga";
}

// Where the decal lies this frame (queueDecal with addShadow's factors): its centre (the object's position), its
// texture axes over the terrain (u along the object's x axis flattened, v that turned a quarter clockwise, each over
// the decal's size, the v one negative), the texture offset (0.5 plus the offsets over the sizes, both negated), and
// the box it covers about the centre (min x, max x, min y, max y). `boxExtent`: half its model's box (x, y), used
// when the template gives no size.
struct ShadowDecalPlacement
{
	std::array<float, 2> center{};
	std::array<float, 2> uAxis{};
	std::array<float, 2> vAxis{};
	std::array<float, 2> uvOffset{};
	std::array<float, 4> extent{};
};

inline ShadowDecalPlacement PlaceShadowDecal(const ShadowDecalLook &look, std::array<float, 2> position, std::array<float, 2> xAxis,
	std::array<float, 2> yAxis, std::array<float, 2> boxExtent)
{
	// addShadow: one over the width; minus one over the height; the offsets over them, negated.
	const float oowX = look.sizeX != 0.0f ? 1.0f / look.sizeX : 1.0f / (boxExtent[0] * 2.0f);
	const float oowY = look.sizeY != 0.0f ? -1.0f / look.sizeY : -1.0f / (boxExtent[1] * 2.0f);
	const float offsetU = look.offsetX != 0.0f ? -look.offsetX * oowX : 0.0f;
	const float offsetV = look.offsetY != 0.0f ? -look.offsetY * oowY : 0.0f;
	const float sizeX = 1.0f / oowX;
	const float sizeY = 1.0f / oowY;
	// queueDecal: u the x axis flattened and normalized, v it turned by -90 degrees; with no x axis, v the y axis
	// (else 0, -1) and u that turned by 90.
	std::array<float, 2> u{}, v{};
	if (const float length = std::sqrt(xAxis[0] * xAxis[0] + xAxis[1] * xAxis[1]); length != 0.0f)
	{
		u = {xAxis[0] / length, xAxis[1] / length};
		v = {u[1], -u[0]};
	}
	else
	{
		if (const float yLength = std::sqrt(yAxis[0] * yAxis[0] + yAxis[1] * yAxis[1]); yLength != 0.0f)
			v = {yAxis[0] / yLength, yAxis[1] / yLength};
		else
			v = {0.0f, -1.0f};
		u = {-v[1], v[0]};
	}
	// The box's corners: left/right along u, top/bottom along v.
	const std::array<float, 2> left{-sizeX * u[0] * (0.5f + offsetU), -sizeX * u[1] * (0.5f + offsetU)};
	const std::array<float, 2> right{sizeX * u[0] * (0.5f - offsetU), sizeX * u[1] * (0.5f - offsetU)};
	const std::array<float, 2> top{-sizeY * v[0] * (0.5f + offsetV), -sizeY * v[1] * (0.5f + offsetV)};
	const std::array<float, 2> bottom{sizeY * v[0] * (0.5f - offsetV), sizeY * v[1] * (0.5f - offsetV)};
	const std::array<std::array<float, 2>, 4> corners{{{left[0] + top[0], left[1] + top[1]}, {right[0] + top[0], right[1] + top[1]},
		{right[0] + bottom[0], right[1] + bottom[1]}, {left[0] + bottom[0], left[1] + bottom[1]}}};
	ShadowDecalPlacement placement;
	placement.center = position;
	placement.extent = {corners[0][0], corners[0][0], corners[0][1], corners[0][1]};
	for (const auto &corner : corners)
	{
		placement.extent[0] = std::min(placement.extent[0], corner[0]);
		placement.extent[1] = std::max(placement.extent[1], corner[0]);
		placement.extent[2] = std::min(placement.extent[2], corner[1]);
		placement.extent[3] = std::max(placement.extent[3], corner[1]);
	}
	placement.uAxis = {u[0] * oowX, u[1] * oowX};
	placement.vAxis = {v[0] * oowY, v[1] * oowY};
	placement.uvOffset = {offsetU + 0.5f, offsetV + 0.5f};
	return placement;
}
}
