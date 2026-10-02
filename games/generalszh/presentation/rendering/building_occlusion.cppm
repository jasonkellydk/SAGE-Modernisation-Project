export module games.generalszh.presentation.rendering.building_occlusion;
import std;

export import games.generalszh.presentation.objects.resources.object_instance;

// Building occlusion as the original draws it (GeneralsMD W3DScene.cpp: RTS3DScene::flushOccludedObjectsIntoStencil, while
// UseBehindBuildingMarker and the scripts' occlusion mode are on): the potential occludees first, each player's together,
// writing their player's colour index into the stencil (bits 3-6); then everything else; then the potential occluders,
// setting the stencil's top bit wherever they draw in front; then, for each player seen, a full-screen quad of that
// player's colour darkened (its HSV value x OccludedColorLuminanceScale, alpha 0.5) where the stencil is that player's
// index with the top bit (an occludee behind a building). Without both an occludee and an occluder in view, everything
// draws as usual. This is the plan; the renderer draws it.
export namespace generalszh::presentation
{
// playerIndexToColorIndex: the index's four bits mirrored (1 -> 8, 2 -> 4, 3 -> 12, ...), so the low stencil bits stay
// free for shadows.
inline std::uint32_t PlayerIndexToColorIndex(std::uint32_t index) noexcept
{
	constexpr std::uint32_t Bits = 4;
	std::uint32_t result = 0;
	for (std::uint32_t bit = 0; bit < Bits; ++bit)
		if ((index & (1u << bit)) != 0)
			result |= 1u << (Bits - 1 - bit);
	return result;
}

// RGB_To_HSV, the value scaled by OccludedColorLuminanceScale, HSV_To_RGB: scaling an HSV value scales each channel
// alike; alpha 0.5.
inline std::array<float, 4> OccludedColor(const std::array<float, 4> &color, float luminanceScale) noexcept
{
	return {color[0] * luminanceScale, color[1] * luminanceScale, color[2] * luminanceScale, 0.5f};
}

struct OcclusionPlan
{
	bool active{false};
	// The instances in drawing order with each one's stencil reference: the occludees (by player), then the rest (no
	// stencil), then the occluders.
	enum class Group : std::uint8_t { Occludee, Plain, Occluder };
	struct Entry
	{
		std::uint32_t instance{0};
		Group group{Group::Plain};
		std::uint8_t reference{0};
	};
	std::vector<Entry> order;
	// The quads drawn after: each seen player's darkened colour where the stencil is (its colour index << 3) | 0x80.
	struct Quad
	{
		std::array<float, 4> color{};
		std::uint8_t reference{0};
	};
	std::vector<Quad> quads;
};

inline OcclusionPlan PlanOcclusion(std::span<const ObjectInstance> instances, float luminanceScale)
{
	OcclusionPlan plan;
	bool occluders = false, occludees = false;
	for (const ObjectInstance &instance : instances)
	{
		occluders = occluders || instance.occlusion == 1;
		occludees = occludees || instance.occlusion == 2;
	}
	if (!occluders || !occludees)
		return plan;
	plan.active = true;
	// Bucket sort the occludees by player index; each player with some gets the next colour index.
	std::vector<std::uint32_t> players;
	for (const ObjectInstance &instance : instances)
		if (instance.occlusion == 2 && std::find(players.begin(), players.end(), instance.occludedPlayer) == players.end())
			players.push_back(instance.occludedPlayer);
	std::sort(players.begin(), players.end());
	std::uint32_t used = 1;
	for (const std::uint32_t player : players)
	{
		const std::uint32_t colorIndex = PlayerIndexToColorIndex(used++);
		bool first = true;
		for (std::uint32_t index = 0; index < instances.size(); ++index)
		{
			const ObjectInstance &instance = instances[index];
			if (instance.occlusion != 2 || instance.occludedPlayer != player)
				continue;
			if (first)
			{
				plan.quads.push_back({OccludedColor(instance.occludedColor, luminanceScale), 0});
				first = false;
			}
			plan.order.push_back({index, OcclusionPlan::Group::Occludee, static_cast<std::uint8_t>(colorIndex << 3)});
		}
	}
	for (std::uint32_t index = 0; index < instances.size(); ++index)
		if (instances[index].occlusion == 0)
			plan.order.push_back({index, OcclusionPlan::Group::Plain, 0});
	for (std::uint32_t index = 0; index < instances.size(); ++index)
		if (instances[index].occlusion == 1)
			plan.order.push_back({index, OcclusionPlan::Group::Occluder, 0xFF});
	for (std::size_t quad = 0; quad < plan.quads.size(); ++quad)
		plan.quads[quad].reference = static_cast<std::uint8_t>((PlayerIndexToColorIndex(static_cast<std::uint32_t>(quad) + 1) << 3) | 0x80u);
	return plan;
}
}
