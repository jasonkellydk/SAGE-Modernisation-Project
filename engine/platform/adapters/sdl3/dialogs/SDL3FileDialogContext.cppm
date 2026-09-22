module;
#include <SDL3/SDL.h>
#include <vector>
export module engine.platform.adapters.sdl3.dialogs.context;
import engine.platform.dialogs;
export namespace engine::platform::sdl3
{
struct SDL3FileDialogContext
{
	std::vector<FileFilter> filters;
	std::vector<SDL_DialogFileFilter> native_filters;
	FileDialogCallback callback;
};
}
