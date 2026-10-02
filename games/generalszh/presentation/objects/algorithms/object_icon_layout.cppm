export module games.generalszh.presentation.objects.algorithms.object_icon_layout;
import std;

export import games.generalszh.presentation.objects.components.object_icons;

// Where Drawable::drawIconUI puts an object's icons on the screen, in pixels (integer maths as the original's
// IRegion2D and ICoord2D; SCALE_ICONS_WITH_ZOOM_ML is off, so most icons keep their image's size):
//   computeHealthRegion: the health box position projected, its width over the zoom, 3 high, starting 0.45 of its
//   width left of centre;
//   drawDisabled: at the region's left, above it by the image and the bar's height;
//   drawHealing: centred three quarters along the bar, its bottom on the bar's top;
//   drawEnthusiastic: the image scaled by the object's kind (STRUCTURE or HUGE_VEHICLE 1, VEHICLE 0.75, else 0.5),
//   centred a quarter along the bar, a quarter of its height below it;
//   drawBombed (car bomb): half the bar wide (its height kept in proportion), centred on the bar, 5 below its middle;
//   drawVeterancy: the image (SCVeter1..3) at its own size, from one pixel right and below the health box position
//   moved right by half the health box width times 1.3 over the zoom.
export namespace generalszh::presentation
{
struct IconRegion
{
	int loX{0}, loY{0}, hiX{0}, hiY{0};
};

struct IconRect
{
	int x{0}, y{0}, width{0}, height{0};
};

// The kind scale drawEnthusiastic uses.
inline float EnthusiasticScale(bool structureOrHuge, bool vehicle) noexcept { return structureOrHuge ? 1.0f : vehicle ? 0.75f : 0.5f; }

// computeHealthRegion from the health box position on the screen (ICoord2D: `screenX`, `screenY`), the box's width
// (getHealthBoxDimensions) and the zoom.
inline IconRegion HealthRegion(int screenX, int screenY, float healthBoxWidth, float zoom) noexcept
{
	const float width = healthBoxWidth / zoom;
	const float height = 3.0f;
	IconRegion region;
	region.loX = static_cast<int>(static_cast<float>(screenX) - width * 0.45f);
	region.loY = static_cast<int>(static_cast<float>(screenY) - height * 0.5f);
	region.hiX = static_cast<int>(static_cast<float>(region.loX) + width);
	region.hiY = static_cast<int>(static_cast<float>(region.loY) + height);
	return region;
}

// Where an icon's current image (`frameWidth` by `frameHeight`) goes; `scale`: EnthusiasticScale for the enthusiastic
// icons.
inline IconRect PlaceIcon(ObjectIcon icon, const IconRegion &region, int frameWidth, int frameHeight, float scale = 1.0f) noexcept
{
	const int barWidth = region.hiX - region.loX;
	const int barHeight = region.hiY - region.loY;
	switch (icon)
	{
	case ObjectIcon::Disabled:
		return {region.loX, region.hiY - (frameHeight + barHeight), frameWidth, frameHeight};
	case ObjectIcon::DefaultHeal:
	case ObjectIcon::StructureHeal:
	case ObjectIcon::VehicleHeal:
		return {static_cast<int>(static_cast<float>(region.loX) + static_cast<float>(barWidth) * 0.75f - static_cast<float>(frameWidth) * 0.5f),
			region.loY - frameHeight, frameWidth, frameHeight};
	case ObjectIcon::Enthusiastic:
	case ObjectIcon::Subliminal: {
		const int width = static_cast<int>(static_cast<float>(frameWidth) * scale);
		const int height = static_cast<int>(static_cast<float>(frameHeight) * scale);
		const int x = static_cast<int>(static_cast<float>(region.loX) + static_cast<float>(barWidth) * 0.25f - static_cast<float>(width) * 0.5f);
		const int y = static_cast<int>(static_cast<double>(region.hiY) + static_cast<double>(height) * 0.25);
		return {x, y, width, height};
	}
	case ObjectIcon::CarBomb: {
		const int size = static_cast<int>(static_cast<float>(barWidth) * 0.5f);
		const int height = frameWidth > 0 ? static_cast<int>(static_cast<float>(size) / static_cast<float>(frameWidth) * static_cast<float>(frameHeight)) : 0;
		const int x = static_cast<int>(static_cast<float>(region.loX) + static_cast<float>(barWidth) * 0.5f - static_cast<float>(size) * 0.5f);
		const int y = static_cast<int>(static_cast<float>(region.loY) + static_cast<float>(barHeight) * 0.5f) + 5; // BOMB_ICON_EXTRA_OFFSET
		return {x, y, size, height};
	}
	}
	return {};
}

// drawVeterancy's rectangle for its image (`imageWidth` by `imageHeight`).
// Drawable::drawAmmo: its `index`th ammo pip (image `width` by `height`), left-aligned with its health bar and 1 apart,
// its top 1 under the screen height of its top (plus AmmoPipWorldOffset) moved by AmmoPipScreenOffset's y of its
// bounding sphere radius (REAL_TO_INT: truncated).
inline IconRect PlaceAmmoPip(const IconRegion &health, int centerY, float screenOffsetY, float boundingRadius, int width, int height, int index) noexcept
{
	const int x = health.loX + index * (width + 1);
	const int y = centerY + static_cast<int>(screenOffsetY * boundingRadius) + 1;
	return {x, y, width, height};
}

// Drawable::drawContained: its `index`th container pip, as an ammo pip (left-aligned with its health bar, 1 apart,
// ContainerPipScreenOffset's y of its bounding sphere radius from the projected top) but a full one's top right there,
// an empty one's 1 under it.
inline IconRect PlaceContainerPip(const IconRegion &health, int centerY, float screenOffsetY, float boundingRadius, int width, int height, int index,
	bool full) noexcept
{
	const int x = health.loX + index * (width + 1);
	const int y = centerY + static_cast<int>(screenOffsetY * boundingRadius) + (full ? 0 : 1);
	return {x, y, width, height};
}

inline IconRect PlaceVeterancy(int screenX, int screenY, float healthBoxWidth, float zoom, int imageWidth, int imageHeight) noexcept
{
	const float scale = 1.3f / zoom;
	const int x = static_cast<int>(static_cast<float>(screenX) + healthBoxWidth * scale * 0.5f);
	return {x + 1, screenY + 1, imageWidth, imageHeight};
}

// s_veterancyImage: none for REGULAR.
inline std::string_view VeterancyImage(std::uint32_t level) noexcept
{
	constexpr std::array<std::string_view, 4> images{"", "SCVeter1", "SCVeter2", "SCVeter3"};
	return level < images.size() ? images[level] : std::string_view{};
}
}
