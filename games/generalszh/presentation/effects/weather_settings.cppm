export module games.generalszh.presentation.effects.weather_settings;
import std;

export import engine.config.binding.schema;
export import games.generalszh.content.loading.content_loader;

// Weather.ini (and a map's map.ini override): the single "Weather" block, the original's WeatherSetting (Snow.h /
// Snow.cpp: m_weatherSettingFieldParseTable). Its Reals are presentation data (the snow drawn around the camera), so
// they stay the original's floats (INI::parseReal). Plain data; the snow's presentation interprets it.
export namespace generalszh::presentation
{
struct WeatherSetting
{
	// WeatherSetting::WeatherSetting's defaults.
	std::string snowTexture{"EXSnowFlake.tga"};
	float snowFrequencyScaleX{0.0533f}; // side-to-side wave of a flake, per unit of height
	float snowFrequencyScaleY{0.0275f};
	float snowAmplitude{5.0f};          // how far a flake sways (world units)
	float snowPointSize{1.0f};          // the hardware point sprite's size (D3DRS_POINTSIZE)
	float snowMaxPointSize{64.0f};      // its largest size in pixels (D3DRS_POINTSIZE_MAX)
	float snowMinPointSize{0.0f};       // its smallest (D3DRS_POINTSIZE_MIN)
	float snowQuadSize{0.5f};           // a flake's quad when point sprites are off (world width and height)
	float snowBoxDimensions{200.0f};    // the box of snow around the camera (world units)
	float snowBoxDensity{1.0f};         // emitters per world unit
	float snowVelocity{4.0f};           // fall speed (world units a second)
	bool usePointSprites{true};         // SnowPointSprites
	bool snowEnabled{false};            // SnowEnabled
};

engine::config::Schema<WeatherSetting> WeatherSettingSchema();

// INI::parseWeatherDefinition: every "Weather" block of `document` onto `setting`; keys a block leaves out keep their
// values, so a map's override applies over Weather.ini.
void BindWeatherSetting(const engine::config::Document &document, WeatherSetting &setting, engine::config::BindContext &context);

// The weather a map plays with: Weather.ini (Default, then the main set), then the map's map.ini override when
// `mapPath` names its .map file (GameLogic::startNewGame loads map.ini with INI_LOAD_CREATE_OVERRIDES).
WeatherSetting LoadWeatherSetting(content::ContentLoader &loader, std::string_view mapPath = {});
}

namespace generalszh::presentation
{
namespace
{
// INI::scanReal (sscanf "%f").
std::optional<float> ReadReal(const engine::config::Node &node, engine::config::BindContext &context)
{
	const std::string_view token = node.Value();
	float value = 0.0f;
	const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
	if (token.empty() || error != std::errc{})
	{
		context.diagnostics.Error(node.location, "'" + std::string(node.key) + "': expected a number");
		return std::nullopt;
	}
	return value;
}

engine::config::Schema<WeatherSetting>::Handler RealField(float WeatherSetting::*member)
{
	return [member](const engine::config::Node &node, WeatherSetting &out, engine::config::BindContext &context) {
		if (const auto value = ReadReal(node, context))
			out.*member = *value;
	};
}

// "Maps/X/X.map" -> "Maps/X/map" (the map's map.ini content set).
std::string MapIniSet(std::string_view mapPath)
{
	const auto slash = mapPath.find_last_of("/\\");
	if (slash == std::string_view::npos)
		return "map";
	return std::string(mapPath.substr(0, slash + 1)) + "map";
}
}

engine::config::Schema<WeatherSetting> WeatherSettingSchema()
{
	using W = WeatherSetting;
	engine::config::Schema<W> schema;
	schema.String("SnowTexture", &W::snowTexture)
		.On("SnowFrequencyScaleX", RealField(&W::snowFrequencyScaleX))
		.On("SnowFrequencyScaleY", RealField(&W::snowFrequencyScaleY))
		.On("SnowAmplitude", RealField(&W::snowAmplitude))
		.On("SnowPointSize", RealField(&W::snowPointSize))
		.On("SnowMaxPointSize", RealField(&W::snowMaxPointSize))
		.On("SnowMinPointSize", RealField(&W::snowMinPointSize))
		.On("SnowQuadSize", RealField(&W::snowQuadSize))
		.On("SnowBoxDimensions", RealField(&W::snowBoxDimensions))
		.On("SnowBoxDensity", RealField(&W::snowBoxDensity))
		.On("SnowVelocity", RealField(&W::snowVelocity))
		.Boolean("SnowPointSprites", &W::usePointSprites)
		.Boolean("SnowEnabled", &W::snowEnabled);
	return schema;
}

void BindWeatherSetting(const engine::config::Document &document, WeatherSetting &setting, engine::config::BindContext &context)
{
	const auto schema = WeatherSettingSchema();
	for (const engine::config::Node &root : document.Roots())
		if (root.key == "Weather")
			schema.Bind(root, setting, context);
}

WeatherSetting LoadWeatherSetting(content::ContentLoader &loader, std::string_view mapPath)
{
	WeatherSetting setting;
	{
		const auto &document = loader.Load({"Data/INI/Default/Weather", "Data/INI/Weather"});
		engine::config::BindContext context{loader.DiagnosticsFor(document), engine::time::FixedStep{30}};
		BindWeatherSetting(document, setting, context);
	}
	if (!mapPath.empty())
	{
		const std::string set = MapIniSet(mapPath);
		const auto &document = loader.Load({set});
		engine::config::BindContext context{loader.DiagnosticsFor(document), engine::time::FixedStep{30}};
		BindWeatherSetting(document, setting, context);
	}
	return setting;
}
}
