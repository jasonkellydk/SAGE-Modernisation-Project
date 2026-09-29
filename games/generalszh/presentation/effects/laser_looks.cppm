export module games.generalszh.presentation.effects.laser_looks;
import std;

export import games.generalszh.content.objects.object_definition;


// A laser beam object (a weapon's LaserName): how W3DLaserDraw draws it
// (beams from the inner to the outer width and colour, its texture tiled and
// scrolling, an optional arc over segments), the particle systems LaserUpdate
// puts at its muzzle and target, and how long it lives (LifetimeUpdate).
export namespace generalszh::content
{
struct LaserLook
{
	std::string texture;
	std::uint32_t beams{1};
	float innerWidth{0.0f};
	float outerWidth{0.0f};
	std::array<float, 4> innerColor{1.0f, 1.0f, 1.0f, 1.0f}; // 0..1
	std::array<float, 4> outerColor{1.0f, 1.0f, 1.0f, 1.0f};
	float scrollRate{0.0f};
	bool tile{false};
	float tilingScalar{1.0f};
	std::uint32_t segments{1};
	float arcHeight{0.0f};
	float segmentOverlap{0.0f};
	std::string muzzleSystem;
	std::string targetSystem;
	// Its lifetime (LifetimeUpdate: MinLifetime..MaxLifetime in milliseconds, up to whole frames; one picked per beam).
	std::uint32_t minLifetimeFrames{0};
	std::uint32_t maxLifetimeFrames{0};
};

namespace laser_detail
{
inline std::string_view Text(const engine::config::Node *node) { return node != nullptr ? node->Value() : std::string_view{}; }

inline float Number(const engine::config::Node *node, float fallback)
{
	const auto value = node != nullptr ? engine::config::values::ParseFixed(node->Value()) : std::nullopt;
	return value ? static_cast<float>(value->Raw()) / 65536.0f : fallback;
}

// "R:255 G:0 B:180 A:120" as 0..1 (alpha 1 when absent).
inline std::array<float, 4> Color(const engine::config::Node *node)
{
	std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
	if (node == nullptr)
		return color;
	const std::string_view text = node->text;
	for (std::size_t at = 0; at < text.size(); ++at)
	{
		const char label = static_cast<char>(std::toupper(static_cast<unsigned char>(text[at])));
		if (at + 1 >= text.size() || text[at + 1] != ':' || (label != 'R' && label != 'G' && label != 'B' && label != 'A'))
			continue;
		std::size_t end = at + 2;
		while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])))
			++end;
		if (end == at + 2)
			continue;
		const int value = std::stoi(std::string(text.substr(at + 2, end - at - 2)));
		color[label == 'R' ? 0 : label == 'G' ? 1 : label == 'B' ? 2 : 3] = std::clamp(value, 0, 255) / 255.0f;
	}
	return color;
}
}

inline std::optional<LaserLook> ReadLaserLook(const ObjectDefinition &object)
{
	using namespace laser_detail;
	std::optional<LaserLook> look;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr)
			continue;
		const auto find = [&](std::string_view key) { return module.block->Find(key); };
		if (module.type == "W3DLaserDraw")
		{
			look.emplace();
			look->texture = std::string(Text(find("Texture")));
			look->beams = static_cast<std::uint32_t>(std::max(1.0f, Number(find("NumBeams"), 1.0f)));
			look->innerWidth = Number(find("InnerBeamWidth"), 0.0f);
			look->outerWidth = Number(find("OuterBeamWidth"), 0.0f);
			look->innerColor = Color(find("InnerColor"));
			look->outerColor = Color(find("OuterColor"));
			look->scrollRate = Number(find("ScrollRate"), 0.0f);
			look->tile = engine::config::values::ParseBool(Text(find("Tile"))).value_or(false);
			look->tilingScalar = Number(find("TilingScalar"), 1.0f);
			look->segments = static_cast<std::uint32_t>(std::max(1.0f, Number(find("Segments"), 1.0f)));
			look->arcHeight = Number(find("ArcHeight"), 0.0f);
			look->segmentOverlap = Number(find("SegmentOverlapRatio"), 0.0f);
		}
	}
	if (!look)
		return look;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr)
			continue;
		if (module.type == "LaserUpdate")
		{
			look->muzzleSystem = std::string(Text(module.block->Find("MuzzleParticleSystem")));
			look->targetSystem = std::string(Text(module.block->Find("TargetParticleSystem")));
		}
		else if (module.type == "LifetimeUpdate")
		{
			const float low = Number(module.block->Find("MinLifetime"), 0.0f), high = Number(module.block->Find("MaxLifetime"), low);
			look->minLifetimeFrames = static_cast<std::uint32_t>(std::ceil(low * 30.0f / 1000.0f));
			look->maxLifetimeFrames = std::max(look->minLifetimeFrames, static_cast<std::uint32_t>(std::ceil(high * 30.0f / 1000.0f)));
		}
	}
	return look;
}
}
