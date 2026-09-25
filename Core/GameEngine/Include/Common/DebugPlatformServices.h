#pragma once

import engine.platform.dialogs;
import engine.platform.time;
import engine.platform.window.interface;

void DebugInjectPlatformServices(engine::platform::IDialogService& dialogs,
	engine::platform::IClockService& clock, engine::platform::IWindow* mainWindow);
