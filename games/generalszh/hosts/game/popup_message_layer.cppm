export module games.generalszh.hosts.game.popup_message_layer;
import std;

import Engine.UI.WND;
import Engine.UI.WND.Document;
import Engine.UI.WND.Input;
import Engine.UI.WND.Bindings;
import Graphics.Renderer2D;
import engine.filesystem.core.virtual_file_system;
import engine.localization.model.string_table;
import engine.gui.mvvm.observable;
import games.generalszh.hosts.game.shell_menu;
import games.generalszh.presentation.hud.algorithms.popup_message_layout;

// The in-game popup message's window (InGameUI::popupMessage: winCreateLayout("InGamePopupMessage.wnd") and
// InGamePopupMessageInit): its text in the message's colour, the window placed and sized around the text wrapped at its
// width less 14 (PopupMessageLayout), the OK button under it; modal while it pauses the game. Its OK button, Enter or
// Esc dismiss it (InGamePopupMessageInput / System).
export namespace generalszh::host
{
class PopupMessageLayer
{
public:
	bool Show(const engine::filesystem::VirtualFileSystem &files, const engine::localization::StringTable &strings, const presentation::PopupMessage &popup,
		std::uint32_t width, std::uint32_t height, float fontScale, std::string &error)
	{
		Close();
		auto menu = std::make_unique<ShellMenu>();
		if (!menu->Load(files, "Window/InGamePopupMessage.wnd", strings, width, height, error, fontScale))
			return false;
		auto &document = menu->Document();
		Engine::UI::WND::WNDWindow *text = document.Find_Window(TextName);
		Engine::UI::WND::WNDWindow *ok = document.Find_Window(OkName);
		if (text == nullptr || ok == nullptr || document.Find_Window(ParentName) == nullptr)
		{
			error = "InGamePopupMessage.wnd lacks its windows";
			return false;
		}
		const float sx = menu->ScaleX(), sy = menu->ScaleY();
		// The text's height wrapped at the width less 14 (in pixels, as the original measures it).
		std::uint32_t textWidth = 0, textHeight = 0;
		if (text->font != nullptr)
		{
			Engine::UI::WND::TextLayoutOptions options;
			options.wrapping_width = popup.width - 14;
			Engine::UI::WND::Get_Text_Renderer().Measure(*text->font, reinterpret_cast<const std::uint16_t *>(popup.text.c_str()), options, textWidth,
				textHeight);
		}
		const int okWidth = static_cast<int>(static_cast<float>(ok->screen_region.right - ok->screen_region.left) * sx);
		const int okHeight = static_cast<int>(static_cast<float>(ok->screen_region.bottom - ok->screen_region.top) * sy);
		const presentation::PopupLayout layout = presentation::LayOutPopupMessage(popup, static_cast<std::int32_t>(textHeight), okWidth, okHeight);
		// Pixels to the layout's units.
		const auto lx = [sx](int pixels) { return static_cast<int>(static_cast<float>(pixels) / sx); };
		const auto ly = [sy](int pixels) { return static_cast<int>(static_cast<float>(pixels) / sy); };
		document.Move_Window(ParentName, lx(layout.window.x), ly(layout.window.y));
		document.Resize_Window(ParentName, lx(layout.window.width), ly(layout.window.height));
		document.Move_Window(TextName, lx(layout.window.x + layout.text.x), ly(layout.window.y + layout.text.y));
		document.Resize_Window(TextName, lx(layout.text.width), ly(layout.text.height));
		document.Move_Window(OkName, lx(layout.window.x + layout.ok.x), ly(layout.window.y + layout.ok.y));
		text = document.Find_Window(TextName);
		text->text = popup.text;
		// winSetEnabledTextColors(textColor, 0).
		text->text_styles[0].color = {static_cast<float>(popup.color[0]) / 255.0f, static_cast<float>(popup.color[1]) / 255.0f,
			static_cast<float>(popup.color[2]) / 255.0f, static_cast<float>(popup.color[3]) / 255.0f};
		text->text_styles[0].drop_color = {0.0f, 0.0f, 0.0f, 0.0f};
		document.Set_Window_Flag(ParentName, Engine::UI::WND::WindowFlag::Hidden, false);
		m_menu = std::move(menu);
		m_bindings.emplace(m_menu->Document());
		m_ok.SetAction([this] { m_closed = true; });
		m_bindings->BindCommand(OkName, m_ok);
		m_modal = popup.pause;
		m_closed = false;
		m_menu->Refresh();
		return true;
	}

	void Close()
	{
		m_bindings.reset();
		m_menu.reset();
		m_closed = false;
	}

	bool Showing() const noexcept { return m_menu != nullptr; }

	// Its OK button was pressed (or Enter / Esc let go) since the last call.
	bool TakeClosed() { return std::exchange(m_closed, false); }
	// InGamePopupMessageInput: Enter or Esc, let go, press OK.
	void Key() { m_closed = m_menu != nullptr; }

	// The mouse this frame (screen pixels): true while the popup takes it (over it, or anywhere while it is modal).
	bool Point(float x, float y, std::uint8_t pressed, std::uint8_t released, std::uint32_t milliseconds)
	{
		if (!m_menu)
			return false;
		auto &document = m_menu->Document();
		const auto [lx, ly] = m_menu->ToLayout(x, y);
		const bool over = Engine::UI::WND::Hit_Test(document, lx, ly).has_value();
		if (auto input = m_pointer.Move(document, lx, ly))
			m_bindings->Apply(*input);
		if ((pressed & 1u) != 0 && over)
			if (auto press = m_pointer.Press(document, lx, ly))
				m_bindings->Apply(*press);
		if ((released & 1u) != 0)
			if (auto release = m_pointer.Release(document, lx, ly, milliseconds))
				m_bindings->Apply(*release);
		if (m_bindings->TakeDirty() || m_pointer.TakeLook())
			m_menu->Refresh();
		return over || m_modal;
	}

	void Draw(Graphics::Renderer2D &renderer)
	{
		if (m_menu)
			m_menu->Draw(renderer);
	}

private:
	static constexpr std::string_view ParentName = "InGamePopupMessage.wnd:InGamePopupMessageParent";
	static constexpr std::string_view TextName = "InGamePopupMessage.wnd:StaticTextMessage";
	static constexpr std::string_view OkName = "InGamePopupMessage.wnd:ButtonOk";

	std::unique_ptr<ShellMenu> m_menu;
	engine::gui::mvvm::Command m_ok; // before the bindings (they hold on to it)
	std::optional<Engine::UI::WND::WNDBindings> m_bindings;
	Engine::UI::WND::WNDPointer m_pointer;
	bool m_modal{false};
	bool m_closed{false};
};
}
