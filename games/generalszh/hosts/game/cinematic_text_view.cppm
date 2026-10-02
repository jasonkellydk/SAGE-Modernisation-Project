export module games.generalszh.hosts.game.cinematic_text_view;
import std;

import Engine.UI.WND;
import Graphics.Renderer2D;
import Assets.Cache;
import Assets.Runtime;
import games.generalszh.hosts.game.game_client;
import games.generalszh.presentation.hud.algorithms.cinematic_text_layout;

// Draws the cinematic text (W3DDisplay::draw's m_cinematicText, over the letterbox): in its script's font (the size
// adjusted for the resolution: GlobalLanguage::adjustFontSize), white, word-wrapped centred at the screen's width less
// 20, where PlaceCinematicText puts it.
export namespace generalszh::host
{
class CinematicTextView
{
public:
	void Draw(const presentation::CinematicText &cinematic, float fontScale, std::int32_t width, std::int32_t height, Graphics::Renderer2D &renderer)
	{
		if (cinematic.serial != m_serial || cinematic.font.name != m_built.name || cinematic.font.size != m_built.size || cinematic.font.bold != m_built.bold)
		{
			m_serial = cinematic.serial;
			m_built = cinematic.font;
			m_font = Build(cinematic.font, fontScale);
		}
		if (!m_font || cinematic.text.empty())
			return;
		const auto *text = reinterpret_cast<const std::uint16_t *>(cinematic.text.c_str());
		Engine::UI::WND::TextLayoutOptions options;
		options.wrapping_width = width - 20;
		options.centered = true;
		std::uint32_t textWidth = 0, textHeight = 0;
		if (!Engine::UI::WND::Get_Text_Renderer().Measure(*m_font, text, options, textWidth, textHeight))
			return;
		const presentation::CinematicTextPlace place = presentation::PlaceCinematicText(width, height, static_cast<std::int32_t>(textWidth));
		options.wrapping_width = place.wrapWidth;
		Engine::UI::WND::TextStyle style;
		style.color = {1.0f, 1.0f, 1.0f, 1.0f};
		style.drop_color = {0.0f, 0.0f, 0.0f, 0.0f};
		m_list.Clear();
		m_list.Add_Text(m_font.get(), nullptr, text, static_cast<float>(place.x), static_cast<float>(place.y), options, style, false, {});
		m_renderer.Render_Draw_List(m_list, renderer);
	}

private:
	static std::unique_ptr<Engine::UI::WND::FontFace> Build(const presentation::CinematicFont &font, float fontScale)
	{
		Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
		if (cache == nullptr || font.name.empty())
			return nullptr;
		const auto size = static_cast<std::uint32_t>(std::max(1, static_cast<int>(std::floor(static_cast<float>(font.size) * fontScale))));
		const Assets::FontAssetHandle handle = cache->Request_Font(font.name, size, font.bold);
		cache->Wait(handle);
		const Assets::FontAsset *asset = cache->Try_Get_Font(handle);
		auto face = std::make_unique<Engine::UI::WND::FontFace>();
		if (asset == nullptr || !face->Build(*asset))
			return nullptr;
		return face;
	}

	std::uint32_t m_serial{0};
	presentation::CinematicFont m_built;
	std::unique_ptr<Engine::UI::WND::FontFace> m_font;
	Engine::UI::WND::DrawList m_list;
	Engine::UI::WND::Renderer m_renderer;
};
}
