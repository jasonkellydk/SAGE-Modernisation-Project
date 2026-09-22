module;
#include <SDL3/SDL.h>
#include <span>
export module engine.platform.adapters.sdl3.process.interface;
import engine.platform.process.interface;
export namespace engine::platform::sdl3
{
class SDL3Process final : public IProcess
{
public:
	explicit SDL3Process(SDL_Process* process) noexcept : m_process(process) {}
	~SDL3Process() override { if (m_process) SDL_DestroyProcess(m_process); }
	bool wait(bool block, int& exit_code) override { return SDL_WaitProcess(m_process, block, &exit_code); }
	bool terminate(bool force) override { return SDL_KillProcess(m_process, force); }
	std::size_t read_stdout(std::span<std::byte> destination) override
	{
		auto* stream = SDL_GetProcessOutput(m_process); return stream && !destination.empty() ? SDL_ReadIO(stream, destination.data(), destination.size()) : 0;
	}
	std::size_t write_stdin(std::span<const std::byte> source) override
	{
		auto* stream = SDL_GetProcessInput(m_process); return stream && !source.empty() ? SDL_WriteIO(stream, source.data(), source.size()) : 0;
	}
private:
	SDL_Process* m_process{};
};
}
