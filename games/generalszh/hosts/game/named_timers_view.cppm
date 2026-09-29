export module games.generalszh.hosts.game.named_timers_view;
import std;

import Engine.UI.WND;
import Graphics.Renderer2D;
import Assets.Cache;
import Assets.Runtime;
import games.generalszh.hosts.game.game_client;
export import games.generalszh.content.global.language_fonts;

// Draws the named timers (InGameUI::postDraw): from their position (a share of the screen) each line one up from the
// last by its font's height, right-aligned there when the position is in the right half, in the normal font (the ready
// font for a countdown at 0:00), in its colour with a black drop shadow.
export namespace generalszh::host
{
class NamedTimersView
{
public:
	bool Load(const content::LanguageFont &normal, const content::LanguageFont &ready, float fontScale)
	{
		m_normal = Make(normal, fontScale);
		m_ready = Make(ready, fontScale);
		return m_normal != nullptr && m_ready != nullptr;
	}

	bool Draw(const std::vector<InGameOverlay::NamedTimerLine> &lines, std::array<float, 2> at, float width, float height, Graphics::Renderer2D &renderer)
	{
		if (!m_normal || !m_ready || lines.empty())
			return true;
		m_list.Clear();
		const bool fromRight = at[0] >= 0.5f;
		const float x = std::floor(at[0] * width);
		float y = std::floor(at[1] * height);
		for (const InGameOverlay::NamedTimerLine &line : lines)
		{
			const Engine::UI::WND::FontFace &font = line.ready ? *m_ready : *m_normal;
			float textWidth = 0.0f;
			for (const char16_t character : line.text)
				textWidth += static_cast<float>(font.Get_Char_Spacing(static_cast<std::uint16_t>(character)));
			const float left = fromRight ? x - textWidth : x;
			Engine::UI::WND::TextStyle style;
			style.color = {line.color[0], line.color[1], line.color[2], line.color[3]};
			style.drop_color = {0.0f, 0.0f, 0.0f, 1.0f};
			Engine::UI::WND::StaticTextVisual visual;
			visual.rectangle = {left, y, left + textWidth + 2.0f, y + static_cast<float>(font.Height() + 2)};
			Engine::UI::WND::StaticTextContent content;
			content.font = &font;
			content.text = reinterpret_cast<const std::uint16_t *>(line.text.c_str());
			content.style = style;
			Engine::UI::WND::Add_Static_Text(m_list, visual, content);
			y -= static_cast<float>(font.Height());
		}
		return m_renderer.Render_Draw_List(m_list, renderer);
	}

private:
	static std::unique_ptr<Engine::UI::WND::FontFace> Make(const content::LanguageFont &font, float fontScale)
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

	std::unique_ptr<Engine::UI::WND::FontFace> m_normal, m_ready;
	Engine::UI::WND::DrawList m_list;
	Engine::UI::WND::Renderer m_renderer;
};
}
