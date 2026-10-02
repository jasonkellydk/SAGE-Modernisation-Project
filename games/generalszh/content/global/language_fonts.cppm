export module games.generalszh.content.global.language_fonts;
import std;

import engine.config.document.document;
export import Engine.Core.Math.Fixed;
import engine.config.binding.values;

// The fonts Language.ini names (GlobalLanguage): the display string font (floating texts), the credits' title,
// minor title and normal fonts, and how fast fonts grow with the screen (ResolutionFontAdjustment). Each font is
// GlobalLanguage::parseFontDesc's "name" size Yes/No; mods (the 2160p control bar) enlarge them.
export namespace generalszh::content
{
struct LanguageFont
{
	std::string name;
	int size{12};
	bool bold{false};
};

struct LanguageFonts
{
	LanguageFont displayString{"Times New Roman", 12, false};
	std::array<LanguageFont, 3> credits{{{"Arial", 22, false}, {"Arial", 16, true}, {"Arial", 14, false}}};
	Engine::Math::Fixed resolutionAdjustment{Engine::Math::Fixed::FromRatio(7, 10)};
	// MessageFont: the messages at the top of the screen (none named: InGameUI.ini's own).
	LanguageFont message{"", 0, false};
	// SuperweaponCountdownNormalFont / ReadyFont: the countdowns (none named: InGameUI.ini's own).
	LanguageFont superweaponNormal{"", 0, false};
	LanguageFont superweaponReady{"", 0, false};
	// MilitaryCaptionTitleFont / MilitaryCaptionFont (none named: InGameUI.ini's own), MilitaryCaptionSpeed (ticks
	// between letters) and MilitaryCaptionDelayMS (before the first letter and each line), GlobalLanguage's defaults.
	LanguageFont militaryCaptionTitle{"", 0, false};
	// NamedTimerCountdownNormalFont / ReadyFont (none named: InGameUI.ini's own).
	LanguageFont namedTimerNormal{"", 0, false};
	LanguageFont namedTimerReady{"", 0, false};
	LanguageFont militaryCaption{"", 0, false};
	// TooltipFontName: the mouse's tooltips (none named: Mouse.ini's own; Mouse::onResolutionChanged).
	LanguageFont tooltip{"", 0, false};
	// CopyrightFont (Display::update: GUI:EACopyright under the logo movie), DrawableCaptionFont (InGameUI::init: the
	// drawables' captions and construction percent), DefaultWindowFont (GameWindowManager::winCreate's), NativeDebugDisplay
	// (W3DDisplay's debug display) and DrawGroupInfoFont (GameClient::init: the control group numbers); none named: the
	// original's own.
	LanguageFont copyright{"", 0, false};
	LanguageFont drawableCaption{"", 0, false};
	LanguageFont defaultWindow{"", 0, false};
	LanguageFont nativeDebugDisplay{"", 0, false};
	LanguageFont drawGroupInfo{"", 0, false};
	// UnicodeFontName (W3DFontLibrary::loadFontData: the glyphs past 255), UseHardWordWrap (W3DDisplayString: wrap
	// anywhere, not only at spaces) and LocalFontFile (each pushed to the front of the fonts to register).
	std::string unicodeFontName;
	bool useHardWrap{false};
	std::vector<std::string> localFontFiles;
	int militaryCaptionSpeed{0};
	int militaryCaptionDelayMs{750};
};

inline LanguageFonts ReadLanguageFonts(const engine::config::Document &language)
{
	LanguageFonts fonts;
	const auto font = [](const engine::config::Node &field, LanguageFont &out) {
		if (field.values.size() < 3)
			return;
		// The name is every token before the size and the bold flag, its quotes dropped.
		std::string name;
		for (std::size_t token = 0; token + 2 < field.values.size(); ++token)
			name += (name.empty() ? "" : " ") + std::string(field.values[token]);
		std::erase(name, '"');
		const std::string_view bold = field.values.back();
		out = {name, std::atoi(std::string(field.values[field.values.size() - 2]).c_str()), bold == "Yes" || bold == "yes" || bold == "YES"};
	};
	for (const engine::config::Node &root : language.Roots())
	{
		if (root.key != "Language")
			continue;
		for (const engine::config::Node &field : root.children)
		{
			if (field.key == "ResolutionFontAdjustment" && !field.values.empty())
				fonts.resolutionAdjustment = engine::config::values::ParseFixed(field.values.front()).value_or(fonts.resolutionAdjustment);
			else if (field.key == "DefaultDisplayStringFont")
				font(field, fonts.displayString);
			else if (field.key == "CreditsTitleFont")
				font(field, fonts.credits[0]);
			else if (field.key == "CreditsMinorTitleFont")
				font(field, fonts.credits[1]);
			else if (field.key == "CreditsNormalFont")
				font(field, fonts.credits[2]);
			else if (field.key == "MessageFont")
				font(field, fonts.message);
			else if (field.key == "SuperweaponCountdownNormalFont")
				font(field, fonts.superweaponNormal);
			else if (field.key == "SuperweaponCountdownReadyFont")
				font(field, fonts.superweaponReady);
			else if (field.key == "NamedTimerCountdownNormalFont")
				font(field, fonts.namedTimerNormal);
			else if (field.key == "NamedTimerCountdownReadyFont")
				font(field, fonts.namedTimerReady);
			else if (field.key == "MilitaryCaptionTitleFont")
				font(field, fonts.militaryCaptionTitle);
			else if (field.key == "MilitaryCaptionFont")
				font(field, fonts.militaryCaption);
			else if (field.key == "TooltipFontName")
				font(field, fonts.tooltip);
			else if (field.key == "CopyrightFont")
				font(field, fonts.copyright);
			else if (field.key == "DrawableCaptionFont")
				font(field, fonts.drawableCaption);
			else if (field.key == "DefaultWindowFont")
				font(field, fonts.defaultWindow);
			else if (field.key == "NativeDebugDisplay")
				font(field, fonts.nativeDebugDisplay);
			else if (field.key == "DrawGroupInfoFont")
				font(field, fonts.drawGroupInfo);
			else if (field.key == "UnicodeFontName" && !field.values.empty())
			{
				// INI::parseAsciiString: the next token, quotes and all joined ("Arial Unicode MS").
				std::string name;
				for (const std::string_view token : field.values)
					name += (name.empty() ? "" : " ") + std::string(token);
				std::erase(name, '"');
				fonts.unicodeFontName = name;
			}
			else if (field.key == "UseHardWordWrap" && !field.values.empty())
				fonts.useHardWrap = engine::config::values::ParseBool(field.values.front()).value_or(fonts.useHardWrap);
			else if (field.key == "LocalFontFile" && !field.values.empty())
				fonts.localFontFiles.insert(fonts.localFontFiles.begin(), std::string(field.values.front()));
			else if (field.key == "MilitaryCaptionSpeed" && !field.values.empty())
				fonts.militaryCaptionSpeed = std::atoi(std::string(field.values.front()).c_str());
			else if (field.key == "MilitaryCaptionDelayMS" && !field.values.empty())
				fonts.militaryCaptionDelayMs = std::atoi(std::string(field.values.front()).c_str());
		}
	}
	return fonts;
}
}
