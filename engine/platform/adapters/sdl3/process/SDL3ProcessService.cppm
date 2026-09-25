module;
#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <vector>
export module engine.platform.adapters.sdl3.process;
import engine.platform.process;
import engine.platform.adapters.sdl3.process.interface;
export namespace engine::platform::sdl3
{
class SDL3ProcessService final : public IProcessService
{
public:
	[[nodiscard]] std::unique_ptr<IProcess> create(const ProcessConfig& config) override
	{
		if (config.arguments.empty()) return {};
		std::vector<const char*> arguments; arguments.reserve(config.arguments.size() + 1);
		for (const auto& arg : config.arguments) arguments.push_back(arg.c_str()); arguments.push_back(nullptr);
		const auto props = SDL_CreateProperties(); if (!props) return {};
		SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, arguments.data());
		if (!config.working_directory.empty()) SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, config.working_directory.c_str());
		if (config.capture_standard_io) {
			SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
			SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
			SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
		}
		SDL_Process* process = SDL_CreateProcessWithProperties(props); SDL_DestroyProperties(props);
		return process ? std::make_unique<SDL3Process>(process) : nullptr;
	}
};
}
