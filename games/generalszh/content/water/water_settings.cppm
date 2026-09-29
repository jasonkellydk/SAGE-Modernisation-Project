export module games.generalszh.content.water.water_settings;
import std;

export import engine.config.binding.schema;

// Water.ini (and a map's map.ini overrides): "WaterSet <time of day>" blocks,
// the per time-of-day look of the water plane, and the single
// "WaterTransparency" block. Plain data; renderers interpret it.
export namespace generalszh::content
{
// 8-bit colour with alpha ("R:150 G:150 B:150 A:128"); alpha defaults to
// opaque when the data leaves it out.
struct WaterColor
{
	std::uint8_t r{0};
	std::uint8_t g{0};
	std::uint8_t b{0};
	std::uint8_t a{255};
	constexpr bool operator==(const WaterColor &) const noexcept = default;
};

// Index order of the original TimeOfDay (MORNING = 1 in the data, stored
// here from 0), matching engine::level::Lighting::current.
enum class WaterTimeOfDay : std::uint8_t
{
	Morning,
	Afternoon,
	Evening,
	Night,
};
inline constexpr std::size_t WaterTimeOfDayCount = 4;

struct WaterSetDefinition
{
	std::string skyTexture;
	std::string waterTexture;
	// Vertex00 (bottom left), Vertex10 (bottom right), Vertex01 (top left), Vertex11 (top right).
	WaterColor vertex00{0, 0, 0, 0};
	WaterColor vertex10{0, 0, 0, 0};
	WaterColor vertex01{0, 0, 0, 0};
	WaterColor vertex11{0, 0, 0, 0};
	WaterColor diffuse{0, 0, 0, 0};
	WaterColor transparentDiffuse{0, 0, 0, 0};
	Engine::Math::Fixed uScrollPerMs;
	Engine::Math::Fixed vScrollPerMs;
	Engine::Math::Fixed skyTexelsPerUnit;
	std::int32_t waterRepeatCount{0};
};

struct WaterTransparencyDefinition
{
	Engine::Math::Fixed transparentWaterDepth{Engine::Math::Fixed::FromInt(3)};
	Engine::Math::Fixed minWaterOpacity{Engine::Math::Fixed::One()};
	engine::config::Rgb standingWaterColor{255, 255, 255};
	std::string standingWaterTexture{"TWWater01.tga"};
	bool additiveBlending{false};
	engine::config::Rgb radarWaterColor{140, 140, 255};
	std::string skyboxTextureN{"TSMorningN.tga"};
	std::string skyboxTextureE{"TSMorningE.tga"};
	std::string skyboxTextureS{"TSMorningS.tga"};
	std::string skyboxTextureW{"TSMorningW.tga"};
	std::string skyboxTextureT{"TSMorningT.tga"};
};

struct WaterSettings
{
	std::array<WaterSetDefinition, WaterTimeOfDayCount> sets{};
	WaterTransparencyDefinition transparency;

	const WaterSetDefinition &For(std::uint32_t timeOfDay) const noexcept
	{
		return sets[timeOfDay < sets.size() ? timeOfDay : static_cast<std::size_t>(WaterTimeOfDay::Afternoon)];
	}
};

// "R:<0-255> G:<0-255> B:<0-255> [A:<0-255>]".
std::optional<WaterColor> ParseWaterColor(std::string_view text) noexcept;

engine::config::Schema<WaterSetDefinition> WaterSetSchema();
engine::config::Schema<WaterTransparencyDefinition> WaterTransparencySchema();

// The time of day a WaterSet block names (MORNING, AFTERNOON, EVENING, NIGHT).
std::optional<WaterTimeOfDay> ParseWaterTimeOfDay(std::string_view name) noexcept;

// Applies every WaterSet/WaterTransparency block of `document` onto
// `settings`. Keys a block leaves out keep their current values, so a
// map's overrides can be applied after Water.ini.
void BindWaterSettings(const engine::config::Document &document, WaterSettings &settings, engine::config::BindContext &context);
}

namespace generalszh::content
{
namespace
{
bool EqualsIgnoringCase(std::string_view a, std::string_view b) noexcept
{
	if (a.size() != b.size())
		return false;
	for (std::size_t index = 0; index < a.size(); ++index)
	{
		const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
		if (lower(a[index]) != lower(b[index]))
			return false;
	}
	return true;
}

engine::config::Schema<WaterSetDefinition>::Handler ColorHandler(WaterColor WaterSetDefinition::*member)
{
	return [member](const engine::config::Node &node, WaterSetDefinition &out, engine::config::BindContext &context) {
		if (const auto color = ParseWaterColor(node.text))
			out.*member = *color;
		else
			context.diagnostics.Error(node.location, "'" + std::string(node.key) + "': expected R:<0-255> G:<0-255> B:<0-255> [A:<0-255>]");
	};
}
}

std::optional<WaterColor> ParseWaterColor(std::string_view text) noexcept
{
	// Tokens split on blanks, '=' and ':' alternate label, value.
	std::vector<std::string_view> tokens;
	std::size_t index = 0;
	const auto separator = [](char c) { return c == ' ' || c == '\t' || c == '=' || c == ':'; };
	while (index < text.size())
	{
		while (index < text.size() && separator(text[index]))
			++index;
		const std::size_t start = index;
		while (index < text.size() && !separator(text[index]))
			++index;
		if (index > start)
			tokens.push_back(text.substr(start, index - start));
	}
	if (tokens.size() % 2 != 0)
		return std::nullopt;
	WaterColor color;
	bool red = false;
	bool green = false;
	bool blue = false;
	for (std::size_t token = 0; token < tokens.size(); token += 2)
	{
		const auto value = engine::config::values::ParseInt(tokens[token + 1]);
		if (!value || *value < 0 || *value > 255)
			return std::nullopt;
		const auto channel = static_cast<std::uint8_t>(*value);
		const std::string_view label = tokens[token];
		if (EqualsIgnoringCase(label, "R"))
			color.r = channel, red = true;
		else if (EqualsIgnoringCase(label, "G"))
			color.g = channel, green = true;
		else if (EqualsIgnoringCase(label, "B"))
			color.b = channel, blue = true;
		else if (EqualsIgnoringCase(label, "A"))
			color.a = channel;
		else
			return std::nullopt;
	}
	if (!red || !green || !blue)
		return std::nullopt;
	return color;
}

engine::config::Schema<WaterSetDefinition> WaterSetSchema()
{
	using D = WaterSetDefinition;
	engine::config::Schema<D> schema;
	schema.String("SkyTexture", &D::skyTexture)
		.String("WaterTexture", &D::waterTexture)
		.On("Vertex00Color", ColorHandler(&D::vertex00))
		.On("Vertex10Color", ColorHandler(&D::vertex10))
		.On("Vertex01Color", ColorHandler(&D::vertex01))
		.On("Vertex11Color", ColorHandler(&D::vertex11))
		.On("DiffuseColor", ColorHandler(&D::diffuse))
		.On("TransparentDiffuseColor", ColorHandler(&D::transparentDiffuse))
		.Fixed("UScrollPerMS", &D::uScrollPerMs)
		.Fixed("VScrollPerMS", &D::vScrollPerMs)
		.Fixed("SkyTexelsPerUnit", &D::skyTexelsPerUnit)
		.Integer("WaterRepeatCount", &D::waterRepeatCount);
	return schema;
}

engine::config::Schema<WaterTransparencyDefinition> WaterTransparencySchema()
{
	using D = WaterTransparencyDefinition;
	engine::config::Schema<D> schema;
	schema.Fixed("TransparentWaterDepth", &D::transparentWaterDepth)
		.Fixed("TransparentWaterMinOpacity", &D::minWaterOpacity)
		.Color("StandingWaterColor", &D::standingWaterColor)
		.String("StandingWaterTexture", &D::standingWaterTexture)
		.Boolean("AdditiveBlending", &D::additiveBlending)
		.Color("RadarWaterColor", &D::radarWaterColor)
		.String("SkyboxTextureN", &D::skyboxTextureN)
		.String("SkyboxTextureE", &D::skyboxTextureE)
		.String("SkyboxTextureS", &D::skyboxTextureS)
		.String("SkyboxTextureW", &D::skyboxTextureW)
		.String("SkyboxTextureT", &D::skyboxTextureT);
	return schema;
}

std::optional<WaterTimeOfDay> ParseWaterTimeOfDay(std::string_view name) noexcept
{
	constexpr std::array<std::string_view, WaterTimeOfDayCount> names{"MORNING", "AFTERNOON", "EVENING", "NIGHT"};
	for (std::size_t index = 0; index < names.size(); ++index)
		if (EqualsIgnoringCase(name, names[index]))
			return static_cast<WaterTimeOfDay>(index);
	return std::nullopt;
}

void BindWaterSettings(const engine::config::Document &document, WaterSettings &settings, engine::config::BindContext &context)
{
	const auto waterSet = WaterSetSchema();
	const auto transparency = WaterTransparencySchema();
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key == "WaterSet")
		{
			const auto timeOfDay = ParseWaterTimeOfDay(root.Value());
			if (!timeOfDay)
			{
				context.diagnostics.Error(root.location, "'WaterSet' needs a time of day (MORNING, AFTERNOON, EVENING or NIGHT)");
				continue;
			}
			waterSet.Bind(root, settings.sets[static_cast<std::size_t>(*timeOfDay)], context);
		}
		else if (root.key == "WaterTransparency")
			transparency.Bind(root, settings.transparency, context);
	}
}
}
