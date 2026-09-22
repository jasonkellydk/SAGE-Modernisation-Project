#include "SDL3Device/Common/SDL3GameEngine.h"
#include "Common/GlobalData.h"
#include "GameClient/Display.h"
#include "GameClient/IMEManager.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Mouse.h"
import engine.platform.adapters.sdl3;

namespace
{
struct DisplaySizeChange
{
	bool changed = false;
	UnsignedInt oldWidth = 0;
	UnsignedInt oldHeight = 0;
	UnsignedInt newWidth = 0;
	UnsignedInt newHeight = 0;
};

DisplaySizeChange synchronizeDisplayToWindow(engine::platform::IWindow* window)
{
	if (window == nullptr || TheDisplay == nullptr || TheTacticalView == nullptr ||
		TheDisplay->getWidth() == 0 || TheDisplay->getHeight() == 0)
	{
		return {};
	}

	const auto drawable = window->drawable_size();
	const int pixelWidth = drawable.width;
	const int pixelHeight = drawable.height;
	if (pixelWidth <= 0 || pixelHeight <= 0)
	{
		return {};
	}

	const Bool windowed = window->mode() == engine::platform::WindowMode::fullscreen ? FALSE : TRUE;
	const UnsignedInt bitDepth = TheDisplay->getBitDepth() != 0 ?
		TheDisplay->getBitDepth() : DEFAULT_DISPLAY_BIT_DEPTH;
	if (TheDisplay->getWidth() == static_cast<UnsignedInt>(pixelWidth) &&
		TheDisplay->getHeight() == static_cast<UnsignedInt>(pixelHeight) &&
		TheDisplay->getWindowed() == windowed)
	{
		if (TheWritableGlobalData != nullptr)
			TheWritableGlobalData->m_windowed = windowed;
		return {};
	}

	const UnsignedInt oldWidth = TheDisplay->getWidth();
	const UnsignedInt oldHeight = TheDisplay->getHeight();
	if (!TheDisplay->setDisplayMode(static_cast<UnsignedInt>(pixelWidth),
		static_cast<UnsignedInt>(pixelHeight), bitDepth, windowed))
	{
		return {};
	}

	if (TheWritableGlobalData != nullptr)
		TheWritableGlobalData->m_windowed = windowed;

	return { true, oldWidth, oldHeight, TheDisplay->getWidth(), TheDisplay->getHeight() };
}

DisplaySizeChange toggleFullscreen(GameEngine& engine)
{
	auto* platformWindow = engine.mainWindow();
	if (platformWindow == nullptr)
		return {};

	const bool fullscreen = platformWindow->mode() == engine::platform::WindowMode::fullscreen;
	if (!platformWindow->set_mode(fullscreen ? engine::platform::WindowMode::windowed : engine::platform::WindowMode::fullscreen))
		return {};

	return synchronizeDisplayToWindow(platformWindow);
}
}

SDL3GameEngine::SDL3GameEngine()
	: GameEngine(std::make_unique<engine::platform::sdl3::SDL3PlatformAdapter>())
{
}

void SDL3GameEngine::update()
{
	serviceSDL3();
	GameEngine::update();
}

void SDL3GameEngine::serviceSDL3()
{
	engine::platform::PlatformEvent event{};
	while (platform().events().poll(event))
	{
		if (event.type == engine::platform::EventType::mouse_wheel && TheMouse != nullptr)
		{
			Real wheelDelta = static_cast<Real>(event.y);
			if (event.flipped)
				wheelDelta = -wheelDelta;
			TheMouse->addWheelDelta(wheelDelta);
		}

		if (event.type == engine::platform::EventType::key_down &&
			(event.key == engine::platform::KeyCode::enter || event.key == engine::platform::KeyCode::keypad_enter) &&
			!event.repeat && (event.modifiers & engine::platform::modifier_alt) != 0)
		{
			const DisplaySizeChange displayChange = toggleFullscreen(*this);
			if (displayChange.changed)
				onDisplaySizeChanged(displayChange.oldWidth, displayChange.oldHeight,
					displayChange.newWidth, displayChange.newHeight);
			if (TheKeyboard)
				TheKeyboard->resetKeys();
			continue;
		}

		if (event.type == engine::platform::EventType::window_resized)
		{
			const DisplaySizeChange displayChange = synchronizeDisplayToWindow(mainWindow());
			if (displayChange.changed)
				onDisplaySizeChanged(displayChange.oldWidth, displayChange.oldHeight,
					displayChange.newWidth, displayChange.newHeight);
			continue;
		}

		if (TheIMEManager != nullptr && TheIMEManager->servicePlatformEvent(event))
			continue;
		if (event.type == engine::platform::EventType::quit || event.type == engine::platform::EventType::window_close_requested)
			setQuitting(true);
		else if (event.type == engine::platform::EventType::focus_gained)
		{
			setIsActive(true);
			if (TheKeyboard)
				TheKeyboard->resetKeys();
			if (TheMouse)
				TheMouse->regainFocus();
		}
		else if (event.type == engine::platform::EventType::focus_lost)
		{
			setIsActive(false);
			if (TheKeyboard)
				TheKeyboard->resetKeys();
			if (TheMouse)
				TheMouse->loseFocus();
		}
	}
	if (auto* window = mainWindow(); window != nullptr && platform().application().poll_activation_request(0))
		platform().application().activate_window(window->id());
}
