export module games.generalszh.hosts.game.mouse_tooltip_view;
import std;

import Engine.UI.WND;
import Graphics.Renderer2D;
import Assets.Cache;
import Assets.Runtime;
import games.generalszh.hosts.game.game_client;
export import games.generalszh.content.global.language_fonts;
export import games.generalszh.content.global.mouse;
import games.generalszh.presentation.interaction.algorithms.mouse_tooltip;
import games.generalszh.presentation.hud.algorithms.language_font_choice;

// Draws what the mouse draws over everything (W3DMouse::draw, last in the frame): the cursor text centred on the
// pointer (Mouse::drawCursorText), then the tooltip (Mouse::drawTooltip): its box and text laid out by mouse_tooltip's
// LayOutTooltip from the text as wrapped (the display string's word wrap), the text clipped to the highlight's run and
// its last 15 pixels again in the highlight colour, both with their drop colour; then the highlight runs on
// (AdvanceTooltipHighlight). The font: Language.ini's TooltipFontName, else Mouse.ini's (Mouse::onResolutionChanged),
// at GlobalLanguage::adjustFontSize's size for the screen.
export namespace generalszh::host
{
class MouseTooltipView
{
public:
	bool Load(const content::LanguageFont &language, const content::MouseTooltipContent &mouse, float fontScale)
	{
		const content::LanguageFont font = presentation::LanguageFontOr(language, mouse.fontName, mouse.fontSize, mouse.fontBold);
		m_font = Build(font.name, font.size, font.bold, fontScale);
		return m_font != nullptr;
	}

	bool Draw(const GameClient::MouseTooltipFrame &frame, float pointerX, float pointerY, float screenWidth, float screenHeight, std::uint32_t nowMs,
		Graphics::Renderer2D &renderer)
	{
		if (frame.tooltip == nullptr || frame.settings == nullptr || !m_font)
			return true;
		presentation::MouseTooltip &tooltip = *frame.tooltip;
		const content::MouseTooltipContent &look = frame.settings->look;
		const int x = static_cast<int>(pointerX), y = static_cast<int>(pointerY);
		m_list.Clear();
		// drawCursorText.
		if (!tooltip.cursorText.empty())
		{
			m_cursorText = tooltip.cursorText;
			const auto [width, height] = Measure(m_cursorText, {});
			const auto at = presentation::CursorTextAt(x, y, width, height);
			Engine::UI::WND::TextStyle style;
			style.color = Color(tooltip.cursorTextColor);
			style.drop_color = Color(tooltip.cursorTextDropColor);
			m_list.Add_Text(m_font.get(), nullptr, Text(m_cursorText), static_cast<float>(at[0]), static_cast<float>(at[1]), {}, style);
		}
		// drawTooltip.
		if (!tooltip.empty && !tooltip.text.empty())
		{
			m_tooltipText = tooltip.text;
			Engine::UI::WND::TextLayoutOptions wrap;
			wrap.wrapping_width = tooltip.wrapWidth;
			const auto [width, height] = Measure(m_tooltipText, wrap);
			const presentation::TooltipDraw draw = presentation::LayOutTooltip(tooltip, look, x, y, width, height, static_cast<int>(screenWidth),
				static_cast<int>(screenHeight), frame.scriptFade);
			if (draw.shown)
			{
				const Graphics::Rect2D box{static_cast<float>(draw.x), static_cast<float>(draw.y), static_cast<float>(draw.x + draw.boxWidth),
					static_cast<float>(draw.y + draw.boxHeight)};
				m_list.Add_Rect(box, Color(draw.back));
				m_list.Add_Outline(box, 1.0f, Color(draw.border));
				Engine::UI::WND::TextStyle style;
				style.color = Color(draw.text);
				style.drop_color = Color(draw.shadow);
				m_list.Add_Text(m_font.get(), nullptr, Text(m_tooltipText), static_cast<float>(draw.textX), static_cast<float>(draw.textY), wrap, style, true,
					{static_cast<float>(draw.textX), static_cast<float>(draw.clipTop), static_cast<float>(draw.textRight), static_cast<float>(draw.clipBottom)});
				style.color = Color(draw.highlight);
				m_list.Add_Text(m_font.get(), nullptr, Text(m_tooltipText), static_cast<float>(draw.textX), static_cast<float>(draw.textY), wrap, style, true,
					{static_cast<float>(draw.highlightLeft), static_cast<float>(draw.clipTop), static_cast<float>(draw.highlightRight),
						static_cast<float>(draw.clipBottom)});
				presentation::AdvanceTooltipHighlight(tooltip, look, width, nowMs);
			}
		}
		return m_renderer.Render_Draw_List(m_list, renderer);
	}

private:
	static Graphics::Color2D Color(const presentation::TooltipColor &color) noexcept
	{
		return {static_cast<float>(color[0]) / 255.0f, static_cast<float>(color[1]) / 255.0f, static_cast<float>(color[2]) / 255.0f,
			static_cast<float>(color[3]) / 255.0f};
	}

	static const std::uint16_t *Text(const std::u16string &text) noexcept { return reinterpret_cast<const std::uint16_t *>(text.c_str()); }

	// DisplayString::getSize: the text's size as laid out.
	std::array<int, 2> Measure(const std::u16string &text, Engine::UI::WND::TextLayoutOptions options) const
	{
		std::uint32_t width = 0, height = 0;
		if (!Engine::UI::WND::Get_Text_Renderer().Measure(*m_font, Text(text), options, width, height))
			return {0, 0};
		return {static_cast<int>(width), static_cast<int>(height)};
	}

	static std::unique_ptr<Engine::UI::WND::FontFace> Build(const std::string &name, int size, bool bold, float fontScale)
	{
		Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
		if (cache == nullptr)
			return nullptr;
		const auto points = static_cast<std::uint32_t>(std::max(1, static_cast<int>(std::floor(static_cast<float>(size) * fontScale))));
		const Assets::FontAssetHandle handle = cache->Request_Font(name, points, bold);
		cache->Wait(handle);
		const Assets::FontAsset *asset = cache->Try_Get_Font(handle);
		auto face = std::make_unique<Engine::UI::WND::FontFace>();
		if (asset == nullptr || !face->Build(*asset))
			return nullptr;
		return face;
	}

	std::unique_ptr<Engine::UI::WND::FontFace> m_font;
	std::u16string m_cursorText, m_tooltipText; // kept while the draw list points at them
	Engine::UI::WND::DrawList m_list;
	Engine::UI::WND::Renderer m_renderer;
};
}
