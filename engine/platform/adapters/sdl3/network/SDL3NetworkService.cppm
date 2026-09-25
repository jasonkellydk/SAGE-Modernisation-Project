module;
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include <memory>
#include <string>
export module engine.platform.adapters.sdl3.network;
import engine.platform.network;
import engine.platform.adapters.sdl3.network.socket;

export namespace engine::platform::sdl3
{
class SDL3NetworkService final : public INetworkService
{
public:
	[[nodiscard]] std::unique_ptr<ISocket> create_socket(SocketKind kind, AddressFamily family) override
	{
		return std::make_unique<SDL3Socket>(kind, family);
	}
	[[nodiscard]] std::string last_error() const override
	{
		const char* error = SDL_GetError();
		return error ? error : "";
	}
};
}
