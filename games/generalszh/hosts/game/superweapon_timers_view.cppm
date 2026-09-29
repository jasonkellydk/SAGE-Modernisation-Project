export module games.generalszh.hosts.game.superweapon_timers_view;
import std;

import Engine.UI.WND;
import Graphics.Renderer2D;
import Assets.Cache;
import Assets.Runtime;
import engine.localization.model.string_table;
import games.generalszh.hosts.game.game_client;
import games.generalszh.hosts.game.shell_menu;
export import games.generalszh.content.global.language_fonts;

// Draws the superweapon countdowns (InGameUI::postDraw, SuperweaponInfo::drawName / drawTime): from the countdown
// position (a share of the screen) down, each shown one's "GUI:<power>: " ending at that x and its m:ss starting there,
// in the normal font (the ready font once ready), in its colour with a black drop shadow; a line the font's height
// below the last. Past 82% of the screen's height, "..." stands for the rest.
export namespace generalszh::host
{
class SuperweaponTimersView
{
public:
	bool Load(const content::LanguageFont &normal, const content::LanguageFont &ready, const engine::localization::StringTable &strings, float fontScale)
	{
		m_strings = &strings;
		m_normal = Make(normal, fontScale);
		m_ready = Make(ready, fontScale);
		return m_normal != nullptr && m_ready != nullptr;
	}

	bool Draw(const std::vector<OverlaySuperweapon> &lines, std::array<float, 2> at, float width, float height, Graphics::Renderer2D &renderer)
	{
		if (!m_normal || !m_ready || lines.empty())
			return true;
		m_list.Clear();
		m_texts.clear();
		const float x = std::floor(at[0] * width);
		float y = std::floor(at[1] * height);
		const float bottom = std::floor(height * 0.82f);
		for (const OverlaySuperweapon &line : lines)
		{
			if (y >= bottom)
			{
				Text(*m_normal, u"...", x, y, {1, 1, 1, 1});
				break;
			}
			if (!line.shown)
				continue;
			const Engine::UI::WND::FontFace &font = line.ready ? *m_ready : *m_normal;
			const std::u16string name = Label(line.power) + u": ";
			Text(font, name, x - Width(font, name), y, line.color);
			Text(font, line.time, x, y, line.color);
			y += static_cast<float>(font.Height());
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

	static float Width(const Engine::UI::WND::FontFace &font, const std::u16string &text)
	{
		float width = 0.0f;
		for (const char16_t character : text)
			width += static_cast<float>(font.Get_Char_Spacing(static_cast<std::uint16_t>(character)));
		return width;
	}

	// GUI:<power>, looked up once.
	const std::u16string &Label(const std::string &power)
	{
		auto found = m_labels.find(power);
		if (found == m_labels.end())
			found = m_labels.emplace(power, m_strings != nullptr ? Localized(*m_strings, "GUI:" + power) : std::u16string(power.begin(), power.end())).first;
		return found->second;
	}

	void Text(const Engine::UI::WND::FontFace &font, const std::u16string &text, float x, float y, std::array<float, 4> color)
	{
		Engine::UI::WND::TextStyle style;
		style.color = {color[0], color[1], color[2], color[3]};
		style.drop_color = {0.0f, 0.0f, 0.0f, color[3]};
		Engine::UI::WND::StaticTextVisual visual;
		visual.rectangle = {x, y, x + Width(font, text) + 2.0f, y + static_cast<float>(font.Height() + 2)};
		Engine::UI::WND::StaticTextContent content;
		content.font = &font;
		m_texts.push_back(text);
		content.text = reinterpret_cast<const std::uint16_t *>(m_texts.back().c_str());
		content.style = style;
		Engine::UI::WND::Add_Static_Text(m_list, visual, content);
	}

	const engine::localization::StringTable *m_strings{nullptr};
	std::unique_ptr<Engine::UI::WND::FontFace> m_normal, m_ready;
	std::map<std::string, std::u16string> m_labels;
	std::deque<std::u16string> m_texts;
	Engine::UI::WND::DrawList m_list;
	Engine::UI::WND::Renderer m_renderer;
};
}
