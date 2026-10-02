export module games.generalszh.content.global.draw_group_info;
import std;

export import engine.config.binding.schema;
export import games.generalszh.content.global.language_fonts;

// DrawGroupInfo.ini (INIDrawGroupInfo.cpp, DrawGroupInfo.cpp): how the control group numbers are drawn by a selected
// unit's health bar (Drawable::drawUIText) - their font, the player's colour or a fixed one, the drop shadow and its
// offset, and where from the health bar's top left: each axis either pixels (DrawPosition?Pixel) or a share of the bar's
// width (DrawPosition?Percent), whichever came last (the original keeps both in one union, flagged). Percents are kept
// as the percent written (-20 for "-20%"); the presentation divides by 100 as INI::scanPercentToReal does.
export namespace generalszh::content
{
struct DrawGroupInfoContent
{
	// DrawGroupInfo::DrawGroupInfo's defaults.
	std::string fontName{"Arial"};
	int fontSize{10};
	bool fontIsBold{false};
	bool usePlayerColor{true};
	std::array<std::uint8_t, 4> colorForText{255, 255, 255, 255};
	std::array<std::uint8_t, 4> colorForTextDropShadow{0, 0, 0, 255};
	int dropShadowOffsetX{-1};
	int dropShadowOffsetY{-1};
	bool usingPixelOffsetX{false};
	int pixelOffsetX{0};
	Engine::Math::Fixed percentOffsetX{Engine::Math::Fixed::FromInt(-5)}; // m_percentOffsetX -0.05
	bool usingPixelOffsetY{true};
	int pixelOffsetY{-10};
	Engine::Math::Fixed percentOffsetY{};
};

// INI::parseDrawGroupNumberDefinition: every DrawGroupInfo block onto `info` (keys left out keep their values).
inline void BindDrawGroupInfo(const engine::config::Document &document, DrawGroupInfoContent &info)
{
	// INI::parseColorInt: R:, G:, B: and an optional A: (255 when left out).
	const auto color = [](const engine::config::Node &field, std::array<std::uint8_t, 4> &out) {
		std::array<int, 4> read{out[0], out[1], out[2], 255};
		for (const std::string_view value : field.values)
			for (const auto &[prefix, index] : {std::pair{"R:", 0}, std::pair{"G:", 1}, std::pair{"B:", 2}, std::pair{"A:", 3}})
				if (value.starts_with(prefix))
					std::from_chars(value.data() + 2, value.data() + value.size(), read[static_cast<std::size_t>(index)]);
		for (std::size_t index = 0; index < 4; ++index)
			out[index] = static_cast<std::uint8_t>(std::clamp(read[index], 0, 255));
	};
	const auto integer = [](const engine::config::Node &field, int &out) {
		if (!field.values.empty())
			std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), out);
	};
	// INI::parsePercentToReal's number ("-20%": -20).
	const auto percent = [](const engine::config::Node &field, Engine::Math::Fixed &out) {
		if (field.values.empty())
			return;
		std::string_view text = field.Value();
		if (text.ends_with('%'))
			text.remove_suffix(1);
		if (const auto value = engine::config::values::ParseFixed(text))
			out = *value;
	};
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "DrawGroupInfo")
			continue;
		for (const engine::config::Node &field : root.children)
		{
			const std::string_view key = field.key;
			if (key == "UsePlayerColor" && !field.values.empty())
				info.usePlayerColor = engine::config::values::ParseBool(field.Value()).value_or(info.usePlayerColor);
			else if (key == "ColorForText")
				color(field, info.colorForText);
			else if (key == "ColorForTextDropShadow")
				color(field, info.colorForTextDropShadow);
			else if (key == "FontName" && !field.values.empty())
			{
				// INI::parseQuotedAsciiString: the quoted name, its words joined.
				std::string name;
				for (const std::string_view token : field.values)
					name += (name.empty() ? "" : " ") + std::string(token);
				std::erase(name, '"');
				info.fontName = name;
			}
			else if (key == "FontSize")
				integer(field, info.fontSize);
			else if (key == "FontIsBold" && !field.values.empty())
				info.fontIsBold = engine::config::values::ParseBool(field.Value()).value_or(info.fontIsBold);
			else if (key == "DropShadowOffsetX")
				integer(field, info.dropShadowOffsetX);
			else if (key == "DropShadowOffsetY")
				integer(field, info.dropShadowOffsetY);
			else if (key == "DrawPositionXPixel")
			{
				integer(field, info.pixelOffsetX);
				info.usingPixelOffsetX = true;
			}
			else if (key == "DrawPositionXPercent")
			{
				percent(field, info.percentOffsetX);
				info.usingPixelOffsetX = false;
			}
			else if (key == "DrawPositionYPixel")
			{
				integer(field, info.pixelOffsetY);
				info.usingPixelOffsetY = true;
			}
			else if (key == "DrawPositionYPercent")
			{
				percent(field, info.percentOffsetY);
				info.usingPixelOffsetY = false;
			}
		}
	}
}

// GameClient::init: Language.ini's DrawGroupInfoFont, when it names one, replaces the font.
inline void ApplyLanguageFont(DrawGroupInfoContent &info, const LanguageFont &language)
{
	if (language.name.empty())
		return;
	info.fontName = language.name;
	info.fontSize = language.size;
	info.fontIsBold = language.bold;
}
}
