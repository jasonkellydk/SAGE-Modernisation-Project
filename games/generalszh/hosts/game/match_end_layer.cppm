export module games.generalszh.hosts.game.match_end_layer;
import std;

import Engine.UI.WND;
import Graphics.Renderer2D;
import engine.filesystem.core.virtual_file_system;
import engine.localization.model.string_table;
import games.generalszh.hosts.game.shell_menu;

// The window the end of a game shows over it (ScriptActions::doVictory / doDefeat / doLocalDefeat:
// winCreateFromScript of Menus/Victorious.wnd, Defeat.wnd, LocalDefeat.wnd, or ObserverQuit.wnd for an observer and
// for one who already saw their own defeat), until closed (closeWindows).
export namespace generalszh::host
{
class MatchEndLayer
{
public:
	// Shows `wnd` (a Window/ path) in place of what showed; false (and nothing shown) if it cannot be read.
	bool Show(const engine::filesystem::VirtualFileSystem &files, std::string_view wnd, const engine::localization::StringTable &strings,
		std::uint32_t width, std::uint32_t height, float fontScale, std::string &error)
	{
		m_menu = std::make_unique<ShellMenu>();
		if (!m_menu->Load(files, wnd, strings, width, height, error, fontScale))
		{
			m_menu.reset();
			return false;
		}
		return true;
	}

	void Close() { m_menu.reset(); }
	bool Showing() const noexcept { return m_menu != nullptr; }

	void Draw(Graphics::Renderer2D &renderer)
	{
		if (m_menu)
			m_menu->Draw(renderer);
	}

private:
	std::unique_ptr<ShellMenu> m_menu;
};
}
