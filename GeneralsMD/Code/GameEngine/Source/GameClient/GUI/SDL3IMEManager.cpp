#include "PreRTS.h"

#include <codecvt>
#include <locale>

#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/IMEManager.h"
import engine.platform;

namespace
{
UnicodeString decodeText(const char *text)
{
	if (text == nullptr || *text == '\0')
		return UnicodeString();
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	return UnicodeString(converter.from_bytes(text).c_str());
}
}

class SDL3IMEManager final : public IMEManagerInterface
{
public:
	SDL3IMEManager(engine::platform::ITextInputService& textInput, std::uint32_t windowId)
		: m_textInput(textInput), m_windowId(windowId) {}
	void init() override { if (m_windowId != 0) m_textInput.start(m_windowId); }
	void reset() override { detach(); m_composition.clear(); m_result.clear(); m_compositionCursor = 0; }
	void update() override {}
	void attach(GameWindow *window) override { m_window = window; if (m_windowId != 0) m_textInput.start(m_windowId); }
	void detach() override { m_window = nullptr; m_composition.clear(); m_compositionCursor = 0; if (m_windowId != 0) m_textInput.stop(m_windowId); }
	void enable() override { m_enabled = true; }
	void disable() override { m_enabled = false; }
	Bool isEnabled() override { return m_enabled; }
	Bool isAttachedTo(GameWindow *window) override { return m_window == window; }
	GameWindow *getWindow() override { return m_window; }
	Bool isComposing() override { return m_composition.getLength() != 0; }
	void getCompositionString(UnicodeString &string) override { string = m_composition; }
	Int getCompositionCursorPosition() override { return m_compositionCursor; }
	Int getIndexBase() override { return 0; }
	Int getCandidateCount() override { return 0; }
	const UnicodeString *getCandidate(Int) override { return nullptr; }
	Int getSelectedCandidateIndex() override { return 0; }
	Int getCandidatePageSize() override { return 0; }
	Int getCandidatePageStart() override { return 0; }
	Bool serviceIMEMessage(void*, UnsignedInt, Int, Int) override { return false; }
	Bool servicePlatformEvent(const engine::platform::PlatformEvent& event) override
	{
		if (!m_enabled)
			return false;
		if (event.type == engine::platform::EventType::text_input)
		{
			if (m_window == nullptr || TheWindowManager == nullptr)
				return false;

			// TEXT_INPUT is committed text. Clear any preceding composition before
			// dispatching it, otherwise GadgetTextEntryInput deliberately ignores
			// committed characters while it believes an IME composition is active.
			m_composition.clear();
			m_result = decodeText(event.text);
			for (Int i = 0; i < m_result.getLength(); ++i)
			{
				TheWindowManager->winSendInputMsg(
					m_window, GWM_IME_CHAR, static_cast<WindowMsgData>(m_result.getCharAt(i)), 0);
			}
			return true;
		}
		if (event.type == engine::platform::EventType::text_editing)
		{
			if (m_window == nullptr)
				return false;
			m_composition = decodeText(event.text);
			m_compositionCursor = event.text_start;
			return true;
		}
		return false;
	}
	Int result() override
	{
		const Int length = m_result.getLength();
		m_result.clear();
		return length;
	}

private:
	engine::platform::ITextInputService& m_textInput;
	std::uint32_t m_windowId{};
	GameWindow *m_window = nullptr;
	Bool m_enabled = true;
	UnicodeString m_composition;
	UnicodeString m_result;
	Int m_compositionCursor = 0;
};

IMEManagerInterface *TheIMEManager = nullptr;

IMEManagerInterface *CreateIMEManagerInterface(engine::platform::ITextInputService& textInput,
	std::uint32_t mainWindowId)
{
	return NEW SDL3IMEManager(textInput, mainWindowId);
}
