module;
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <optional>
#include <memory>
export module engine.platform.network.socket;
export namespace engine::platform
{
struct NetworkEndpoint { std::string host; std::uint16_t port{}; };
enum class SocketOption : std::uint8_t { receive_buffer_size, send_buffer_size, broadcast, reuse_address, keep_alive };
enum class SocketReadiness : std::uint8_t { none = 0, readable = 1, writable = 2, error = 4 };
struct SocketWaitResult { SocketReadiness ready{SocketReadiness::none}; bool timed_out{}; };
class ISocket
{
public:
	virtual ~ISocket() = default;
	virtual bool bind(const NetworkEndpoint& endpoint) = 0;
	virtual bool connect(const NetworkEndpoint& endpoint) = 0;
	[[nodiscard]] virtual std::unique_ptr<ISocket> accept() = 0;
	virtual bool set_blocking(bool blocking) = 0;
	virtual bool set_option(SocketOption option, int value) = 0;
	[[nodiscard]] virtual std::optional<int> option(SocketOption option) const = 0;
	[[nodiscard]] virtual SocketWaitResult wait(SocketReadiness interest, std::uint32_t timeout_milliseconds) = 0;
	[[nodiscard]] virtual std::int64_t send(std::span<const std::byte> data) = 0;
	[[nodiscard]] virtual std::int64_t receive(std::span<std::byte> data) = 0;
	[[nodiscard]] virtual std::int64_t send_to(std::span<const std::byte> data, const NetworkEndpoint& endpoint) = 0;
	[[nodiscard]] virtual std::int64_t receive_from(std::span<std::byte> data, NetworkEndpoint& endpoint) = 0;
};
}
