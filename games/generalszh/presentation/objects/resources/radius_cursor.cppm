export module games.generalszh.presentation.objects.resources.radius_cursor;
import std;

export import games.generalszh.content.global.radius_decal;
import engine.ecs.system.system;
import Engine.Core.Math.FixedPresentation;

// InGameUI's radius cursor (m_curRadiusCursor, m_curRcType): the decal following the pointer while a command waits for
// its target. `looks` are InGameUI.ini's *RadiusCursor templates by RadiusCursorType; the rest is the cursor as it
// stands this frame, for the terrain to draw: its type (none: no cursor), its texture and style, where its centre is,
// its radius (the decal a square of twice it), its colour (0-1) and its opacity (0-255). `shown` is false when only its
// owner may see it and that is not the local player (createRadiusDecal made no decal).
export namespace generalszh::presentation
{
struct RadiusCursorLooks
{
	std::array<content::RadiusDecalLook, content::RadiusCursorNames.size()> looks{};
};

struct RadiusCursor
{
	std::uint8_t type{content::radius_cursor::None};
	bool shown{false};
	bool additive{false};
	std::string texture;
	std::array<float, 3> at{};
	float radius{0.0f};
	std::array<float, 3> color{1, 1, 1};
	std::int32_t opacity{255};
};

// The objects' radius decals this frame (RadiusDecal side of the simulation), as the terrain draws them.
struct RadiusDecalView
{
	std::string texture;
	bool additive{false};
	std::array<float, 3> at{};
	float radius{0.0f};
	std::array<float, 3> color{1, 1, 1};
	std::int32_t opacity{255};
};
struct RadiusDecalViews
{
	std::vector<RadiusDecalView> decals;
};

// RadiusDecal::update's opacity (0-255) at a logic frame: (min + (sin(2pi (frame % throb) / throb) + 1) / 2 (max - min))
// x 255, truncated (REAL_TO_INT); 0 while scripts have icon UI off.
inline std::int32_t RadiusDecalOpacity(const content::RadiusDecalLook &look, std::uint64_t frame, bool drawIconUi) noexcept
{
	if (!drawIconUi)
		return 0;
	const std::uint64_t throb = std::max<std::uint64_t>(look.throbTicks, 1);
	const float theta = 2.0f * std::numbers::pi_v<float> * static_cast<float>(frame % throb) / static_cast<float>(throb);
	const float percent = 0.5f * (std::sin(theta) + 1.0f);
	const float low = Engine::Math::ToFloat(look.minOpacity), high = Engine::Math::ToFloat(look.maxOpacity);
	return static_cast<std::int32_t>((low + percent * (high - low)) * 255.0f);
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::RadiusCursorLooks>
{
	static constexpr std::string_view StableName = "generalszh.presentation.radius_cursor_looks";
};
template<>
struct ResourceTraits<generalszh::presentation::RadiusCursor>
{
	static constexpr std::string_view StableName = "generalszh.presentation.radius_cursor";
};
template<>
struct ResourceTraits<generalszh::presentation::RadiusDecalViews>
{
	static constexpr std::string_view StableName = "generalszh.presentation.radius_decal_views";
};
}
