export module games.generalszh.hosts.game.military_caption_view;
import std;

import Engine.UI.WND;
import Graphics.Renderer2D;
import Assets.Cache;
import Assets.Runtime;
import games.generalszh.hosts.game.game_client;
export import games.generalszh.content.global.language_fonts;

// Draws the military caption (InGameUI::postDraw's military subtitle): from its position (authored for 800x600, scaled
// to the screen) its lines one under another, the first in the title font and the rest in the caption font, each in
// the caption's colour with a black drop at its alpha; then, while it blinks on, a block of the caption's colour after
// the last letter typed (as tall as the current line's font, four fifths as wide).
export namespace generalszh::host
{
class MilitaryCaptionView
{
public:
	bool Load(const content::LanguageFont &title, const content::LanguageFont &line, float fontScale)
	{
		m_title = Build(title, fontScale);
		m_line = Build(line, fontScale);
		return m_title != nullptr && m_line != nullptr;
	}

	bool Draw(const OverlayCaption &caption, float screenWidth, float screenHeight, Graphics::Renderer2D &renderer)
	{
		if (!caption.shown || caption.lines.empty() || !m_title || !m_line)
			return true;
		const float x = caption.at[0] * screenWidth / 800.0f, y = caption.at[1] * screenHeight / 600.0f;
		m_list.Clear();
		float lineY = y, blockX = x, blockY = y;
		float blockHeight = 0.0f;
		for (std::size_t index = 0; index < caption.lines.size(); ++index)
		{
			const Engine::UI::WND::FontFace &font = index == 0 ? *m_title : *m_line;
			const std::u16string &text = caption.lines[index];
			float width = 0.0f;
			for (const char16_t character : text)
				width += static_cast<float>(font.Get_Char_Spacing(static_cast<std::uint16_t>(character)));
			Engine::UI::WND::TextStyle style;
			style.color = {caption.color[0], caption.color[1], caption.color[2], caption.color[3]};
			style.drop_color = {0.0f, 0.0f, 0.0f, caption.color[3]};
			Engine::UI::WND::StaticTextVisual visual;
			visual.rectangle = {x, lineY, x + width + 2.0f, lineY + static_cast<float>(font.Height() + 2)};
			Engine::UI::WND::StaticTextContent content;
			content.font = &font;
			content.text = reinterpret_cast<const std::uint16_t *>(text.c_str());
			content.style = style;
			if (!text.empty())
				Engine::UI::WND::Add_Static_Text(m_list, visual, content);
			blockX = x + width;
			blockY = lineY;
			blockHeight = static_cast<float>(font.Height());
			lineY += static_cast<float>(font.Height());
		}
		const bool drawn = m_renderer.Render_Draw_List(m_list, renderer);
		if (caption.block)
			renderer.Add_Rect({blockX, blockY, blockX + blockHeight * 0.8f, blockY + blockHeight},
				Graphics::Color2D{caption.color[0], caption.color[1], caption.color[2], caption.color[3]});
		return drawn;
	}

private:
	static std::unique_ptr<Engine::UI::WND::FontFace> Build(const content::LanguageFont &font, float fontScale)
	{
		Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
		if (cache == nullptr)
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

	std::unique_ptr<Engine::UI::WND::FontFace> m_title, m_line;
	Engine::UI::WND::DrawList m_list;
	Engine::UI::WND::Renderer m_renderer;
};
}
