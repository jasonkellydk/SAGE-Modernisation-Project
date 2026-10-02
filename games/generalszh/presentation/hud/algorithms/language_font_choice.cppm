export module games.generalszh.presentation.hud.algorithms.language_font_choice;
import std;

export import games.generalszh.content.global.language_fonts;
export import games.generalszh.content.global.draw_group_info;
export import games.generalszh.content.global.in_game_ui;

// Which font the interface's texts take, and where a control group's number goes.
//
// The original lets Language.ini's fonts replace the INI's own wherever one is named (InGameUI::init: MessageFont,
// MilitaryCaption(Title)Font, SuperweaponCountdown*Font, NamedTimerCountdown*Font, DrawableCaptionFont; Mouse::
// onResolutionChanged: TooltipFontName; GameClient::init: DrawGroupInfoFont): a name, its point size and bold together.
export namespace generalszh::presentation
{
inline content::LanguageFont LanguageFontOr(const content::LanguageFont &language, std::string_view name, int pointSize, bool bold)
{
	return language.name.empty() ? content::LanguageFont{std::string(name), pointSize, bold} : language;
}

// The fonts the in-game texts are drawn in (InGameUI::init after InGameUI.ini, with Language.ini's replacing them).
struct InGameFonts
{
	content::LanguageFont message;
	content::LanguageFont militaryCaptionTitle;
	content::LanguageFont militaryCaption;
	content::LanguageFont superweaponNormal;
	content::LanguageFont superweaponReady;
	content::LanguageFont namedTimerNormal;
	content::LanguageFont namedTimerReady;
	content::LanguageFont drawableCaption; // construction percent and the drawables' captions
};

inline InGameFonts ChooseInGameFonts(const content::LanguageFonts &language, const content::InGameUiContent &ui)
{
	InGameFonts fonts;
	fonts.message = LanguageFontOr(language.message, ui.messageFont, ui.messagePointSize, ui.messageBold);
	fonts.militaryCaptionTitle = LanguageFontOr(language.militaryCaptionTitle, ui.militaryCaptionTitleFont, ui.militaryCaptionTitlePointSize,
		ui.militaryCaptionTitleBold);
	fonts.militaryCaption = LanguageFontOr(language.militaryCaption, ui.militaryCaptionFont, ui.militaryCaptionPointSize, ui.militaryCaptionBold);
	fonts.superweaponNormal = LanguageFontOr(language.superweaponNormal, ui.superweaponNormalFont, ui.superweaponNormalPointSize, ui.superweaponNormalBold);
	fonts.superweaponReady = LanguageFontOr(language.superweaponReady, ui.superweaponReadyFont, ui.superweaponReadyPointSize, ui.superweaponReadyBold);
	fonts.namedTimerNormal = LanguageFontOr(language.namedTimerNormal, ui.namedTimerNormalFont, ui.namedTimerNormalPointSize, ui.namedTimerNormalBold);
	fonts.namedTimerReady = LanguageFontOr(language.namedTimerReady, ui.namedTimerReadyFont, ui.namedTimerReadyPointSize, ui.namedTimerReadyBold);
	fonts.drawableCaption = LanguageFontOr(language.drawableCaption, ui.drawableCaptionFont, ui.drawableCaptionPointSize, ui.drawableCaptionBold);
	return fonts;
}

// Drawable::drawsAnyUIText / drawUIText: a number shows for hotkey squads 0..9 (NO_HOTKEY_SQUAD -1, NUM_HOTKEY_SQUADS 10).
inline bool ShowsGroupNumber(int group) noexcept { return group > -1 && group < 10; }

// A group number as Drawable::drawUIText draws it: from the health bar region's top left, each axis moved by its pixels
// or by its share of the region's width (the Y share too is of the width, as the original), the share added as a Real to
// the Int position and cut back to an Int; in the player's colour or DrawGroupInfo's, with its drop shadow colour and
// offset. The numeral itself is the string NUMBER:<n> in DrawGroupInfo's font (W3DDisplayStringManager::postProcessLoad,
// not resized for the screen).
struct GroupNumberDraw
{
	int x{0};
	int y{0};
	std::array<std::uint8_t, 4> color{};
	std::array<std::uint8_t, 4> dropColor{};
	int dropX{0};
	int dropY{0};
};

inline GroupNumberDraw PlaceGroupNumber(const content::DrawGroupInfoContent &info, int regionLeft, int regionTop, int regionWidth,
	const std::array<std::uint8_t, 4> &playerColor) noexcept
{
	const auto share = [](const Engine::Math::Fixed &percent) {
		return static_cast<float>(static_cast<double>(percent.Raw()) / 65536.0) / 100.0f; // INI::scanPercentToReal
	};
	GroupNumberDraw draw;
	draw.x = regionLeft;
	draw.y = regionTop;
	if (info.usingPixelOffsetX)
		draw.x += info.pixelOffsetX;
	else
		draw.x = static_cast<int>(static_cast<float>(draw.x) + static_cast<float>(regionWidth) * share(info.percentOffsetX));
	if (info.usingPixelOffsetY)
		draw.y += info.pixelOffsetY;
	else
		draw.y = static_cast<int>(static_cast<float>(draw.y) + static_cast<float>(regionWidth) * share(info.percentOffsetY));
	draw.color = info.usePlayerColor ? playerColor : info.colorForText;
	draw.dropColor = info.colorForTextDropShadow;
	draw.dropX = info.dropShadowOffsetX;
	draw.dropY = info.dropShadowOffsetY;
	return draw;
}
}
