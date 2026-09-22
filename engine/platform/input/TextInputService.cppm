module;
#include <cstdint>
export module engine.platform.text_input;
import engine.platform.core.types;

export namespace engine::platform
{
struct TextInputArea
{
	int x{};
	int y{};
	int width{};
	int height{};
	int cursor{};
};
}
