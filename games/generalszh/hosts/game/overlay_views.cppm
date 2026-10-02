module;
#include <cstdio>

export module games.generalszh.hosts.game.overlay_views;
import std;

import Graphics.Renderer2D;
import engine.filesystem.core.virtual_file_system;
export import engine.localization.model.string_table;
export import games.generalszh.content.global.in_game_ui;
export import games.generalszh.content.global.language_fonts;
export import games.generalszh.hosts.game.game_client;
import games.generalszh.hosts.game.shell_menu;
import games.generalszh.hosts.game.floating_text_view;
import games.generalszh.hosts.game.world_animation_view;
import games.generalszh.hosts.game.message_view;
import games.generalszh.hosts.game.military_caption_view;
import games.generalszh.hosts.game.named_timers_view;
import games.generalszh.hosts.game.superweapon_timers_view;
import games.generalszh.presentation.hud.algorithms.language_font_choice;
import games.generalszh.presentation.hud.algorithms.in_game_ui_layout;
import Engine.Core.Math.FixedPresentation;

// The submit stage of the in-game overlay (InGameUI::postDraw, each Drawable's UI, W3DDisplay's letterbox): the
// overlay's views with their fonts and images, and the overlay's frame packet drawn through them.
export namespace generalszh::host
{
struct OverlayViews
{
	WorldAnimationView worldAnimations;
	FloatingTextView floatingTexts;
	MessageView messages;
	MilitaryCaptionView militaryCaption;
	NamedTimersView namedTimers;
	SuperweaponTimersView superweaponTimers;
};

// Each view's fonts: Language.ini's (MessageFont, MilitaryCaption, NamedTimerCountdown, SuperweaponCountdown), else
// InGameUI.ini's; floating texts in the display string font; the world animations' mapped images.
inline void LoadOverlayViews(OverlayViews &views, const engine::filesystem::VirtualFileSystem &files, const engine::localization::StringTable &strings,
	const content::LanguageFonts &language, const content::InGameUiContent &ui, std::uint32_t width, std::uint32_t height)
{
	const float scale = FontScale(width, height, Engine::Math::ToFloat(language.resolutionAdjustment));
	// InGameUI::init: InGameUI.ini's fonts, each replaced by Language.ini's where it names one.
	const presentation::InGameFonts fonts = presentation::ChooseInGameFonts(language, ui);
	if (!views.worldAnimations.Load(files))
		std::fprintf(stderr, "world animations: the mapped images could not be read\n");
	if (!views.floatingTexts.Load(language.displayString, scale))
		std::fprintf(stderr, "floating text: its font could not be made\n");
	if (!views.floatingTexts.LoadCaption(fonts.drawableCaption, scale))
		std::fprintf(stderr, "drawable captions: their font could not be made\n");
	if (!views.messages.Load(fonts.message, scale))
		std::fprintf(stderr, "messages: their font could not be made\n");
	if (!views.militaryCaption.Load(fonts.militaryCaptionTitle, fonts.militaryCaption, scale))
		std::fprintf(stderr, "military caption: its fonts could not be made\n");
	if (!views.namedTimers.Load(fonts.namedTimerNormal, fonts.namedTimerReady, scale))
		std::fprintf(stderr, "named timers: their fonts could not be made\n");
	if (!views.superweaponTimers.Load(fonts.superweaponNormal, fonts.superweaponReady, strings, scale))
		std::fprintf(stderr, "superweapon countdowns: their fonts could not be made\n");
}

// The control group numerals (W3DDisplayStringManager::postProcessLoad: the strings NUMBER:0..9 in DrawGroupInfo's font).
inline void LoadGroupNumberViews(OverlayViews &views, const engine::localization::StringTable &strings, const content::DrawGroupInfoContent &info)
{
	std::array<std::u16string, 10> numerals;
	for (std::size_t group = 0; group < numerals.size(); ++group)
		numerals[group] = Localized(strings, "NUMBER:" + std::to_string(group));
	if (!views.floatingTexts.LoadGroupNumbers(info, std::move(numerals)))
		std::fprintf(stderr, "group numbers: their font could not be made\n");
}

// The selection box being dragged, and the selected objects' health bars (Drawable::drawHealthBar: an outline and a fill
// from red to green by health, averaged towards red when really damaged and towards green when undamaged; blue to cyan
// while disabled; the outline at half the colour).
inline void DrawSelectionMarkers(Graphics::Renderer2D &renderer, const InGameOverlay &overlay)
{
	if (overlay.boxActive)
		renderer.Add_Outline({overlay.box[0], overlay.box[1], overlay.box[2], overlay.box[3]}, 1.0f, Graphics::Color2D{1.0f, 1.0f, 0.0f, 1.0f});
	for (const SelectedMarker &marker : overlay.selected)
	{
		Graphics::Color2D fill{0.0f, 0.0f, 0.0f, 1.0f}, outline{0.0f, 0.0f, 0.0f, 1.0f};
		if (marker.disabled)
		{
			fill = {0.0f, marker.health, 1.0f, 1.0f};
			outline = {0.0f, marker.health * 128.0f / 255.0f, 128.0f / 255.0f, 1.0f};
		}
		else
		{
			float red = marker.health >= 0.5f ? 1.0f - (marker.health - 0.5f) / 0.5f : 1.0f;
			float green = marker.health >= 0.5f ? 1.0f : 1.0f - (0.5f - marker.health) / 0.5f;
			outline = {red * 0.5f, green * 0.5f, 0.0f, 1.0f};
			if (marker.reallyDamaged)
			{
				red = (1.0f + red) * 0.5f;
				green *= 0.5f;
			}
			else if (!marker.damaged)
			{
				green = (1.0f + green) * 0.5f;
				red *= 0.5f;
			}
			fill = {red, green, 0.0f, 1.0f};
		}
		const float height = 3.0f;
		renderer.Add_Outline({marker.x, marker.y, marker.x + marker.width, marker.y + height}, 1.0f, outline);
		renderer.Add_Rect({marker.x + 1.0f, marker.y + 1.0f, marker.x + 1.0f + (marker.width - 2.0f) * marker.health, marker.y + height - 1.0f}, fill);
	}
}

// Everything of the overlay under the control bar, in InGameUI's order.
inline void DrawOverlay(OverlayViews &views, const InGameOverlay &overlay, float width, float height, Graphics::Renderer2D &renderer)
{
	DrawSelectionMarkers(renderer, overlay);
	views.worldAnimations.Draw(overlay.images, renderer);
	views.floatingTexts.DrawCaptions(overlay.captions, renderer);
	views.floatingTexts.Draw(overlay.texts, renderer);
	views.floatingTexts.DrawGroupNumbers(overlay.groupNumbers, renderer);
	views.messages.Draw(overlay.messages, overlay.messageAt, renderer);
	views.militaryCaption.Draw(overlay.caption, width, height, renderer);
	views.namedTimers.Draw(overlay.namedTimers, overlay.namedTimerAt, width, height, renderer);
	views.superweaponTimers.Draw(overlay.superweapons, overlay.superweaponAt, width, height, renderer);
	// InGameUI::postDraw's right-button scroll anchor (DrawRMBScrollAnchor): its black cross, then its green one.
	if (overlay.rmbAnchorShown)
		for (const presentation::AnchorRect &rect : presentation::RmbScrollAnchorRects(overlay.rmbAnchor[0], overlay.rmbAnchor[1]))
			renderer.Add_Rect({static_cast<float>(rect.x), static_cast<float>(rect.y), static_cast<float>(rect.x + rect.width),
								  static_cast<float>(rect.y + rect.height)},
				rect.green ? Graphics::Color2D{0.0f, 1.0f, 0.0f, 1.0f} : Graphics::Color2D{0.0f, 0.0f, 0.0f, 1.0f});
}

// W3DDisplay::renderLetterBox's fade level: a change of the scripts' letterbox starts it from now; on, it fades in over
// a second; once off, it fades out over one.
inline float StepLetterbox(float level, bool on, float elapsedSeconds)
{
	if (on)
		return level != 1.0f ? std::min(elapsedSeconds, 1.0f) : 1.0f;
	return level != 0.0f ? std::max(1.0f - elapsedSeconds, 0.0f) : level;
}

// W3DDisplay::renderLetterBox, over everything: black bars leaving a 16:9 picture; once off, the top one fading out
// (the bottom one goes at once, as the original draws it).
inline void DrawLetterbox(Graphics::Renderer2D &renderer, float level, bool on, float width, float height)
{
	if (level <= 0.0f)
		return;
	const float bar = (height - 9.0f / 16.0f * width) * 0.5f;
	const Graphics::Color2D black{0.0f, 0.0f, 0.0f, static_cast<float>(static_cast<int>(level * 255.0f)) / 255.0f};
	renderer.Add_Rect({0.0f, 0.0f, width, bar}, black);
	if (on)
		renderer.Add_Rect({0.0f, height - bar, width, height}, black);
}
}
