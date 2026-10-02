export module games.generalszh.presentation.objects.resources.tree_breeze;
import std;

import engine.ecs.system.system;

// The breeze as the map's trees feel it (W3DTreeBuffer's sway state): a
// hundred sway offsets round one sway (lean plus intensity times a cosine,
// as a direction along the breeze and a drop), and for each of the ten sway
// types how fast it steps through them (entries a logic frame), where it is,
// its own share of the breeze, and the sway it shows this frame (x, y, z per
// unit of height above the tree's base). `version` is the breeze version the
// offsets were rolled for.
export namespace generalszh::presentation
{
inline constexpr std::size_t TreeSwayEntries = 100; // NUM_SWAY_ENTRIES
inline constexpr std::size_t TreeSwayTypes = 10;    // MAX_SWAY_TYPES

struct TreeBreeze
{
	std::array<std::array<float, 3>, TreeSwayEntries> offsets{};
	std::array<float, TreeSwayTypes> step{};
	std::array<float, TreeSwayTypes> offset{};
	std::array<float, TreeSwayTypes> factor{};
	std::array<std::array<float, 3>, TreeSwayTypes> current{};
	std::int32_t version{-1};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::TreeBreeze>
{
	static constexpr std::string_view StableName = "generalszh.presentation.tree_breeze";
};
}
