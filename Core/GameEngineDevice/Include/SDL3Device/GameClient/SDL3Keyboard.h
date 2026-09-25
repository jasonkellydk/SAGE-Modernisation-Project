#pragma once

#include "GameClient/Keyboard.h"
#include <SDL3/SDL.h>
import engine.platform;

class SDL3Keyboard : public Keyboard
{
public:
	SDL3Keyboard(engine::platform::IInputService& input, engine::platform::IClockService& clock);
	~SDL3Keyboard() override = default;
	void init() override;
	void reset() override;
	void update() override;
	Bool getCapsState() override;
protected:
	void getKey(KeyboardIO *key) override;
private:
	int m_scanCode = 0;
	Bool m_previousState[SDL_SCANCODE_COUNT]{};
	engine::platform::IInputService& m_input;
	engine::platform::IClockService& m_clock;
};
