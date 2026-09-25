module;
#include <memory>
#include <cstdint>
#include <string>
export module engine.platform.network;
export import engine.platform.network.socket;
export namespace engine::platform
{
enum class SocketKind : std::uint8_t { stream, datagram };
enum class AddressFamily : std::uint8_t { ipv4, ipv6 };
class INetworkService
{
public:
	virtual ~INetworkService() = default;
	[[nodiscard]] virtual std::unique_ptr<ISocket> create_socket(SocketKind kind, AddressFamily family) = 0;
	[[nodiscard]] virtual std::string last_error() const = 0;
};
}
