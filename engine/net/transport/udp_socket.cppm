export module engine.net.transport.udp_socket;
import std;

// A non-blocking IPv4 UDP socket: datagrams to one address or broadcast to a
// subnet, received as they come. LAN lobbies speak through it; the platform
// sockets (Winsock, BSD) stay in the implementation.
export namespace engine::net
{
// An IPv4 address and port, both in host byte order (192.168.1.2 is 0xC0A80102).
struct Endpoint
{
	std::uint32_t address{0};
	std::uint16_t port{0};
	bool operator==(const Endpoint &) const = default;
};

inline constexpr std::uint32_t LoopbackAddress = 0x7F000001;
inline constexpr std::uint32_t BroadcastAddress = 0xFFFFFFFF;

std::string ToString(std::uint32_t address);

struct Datagram
{
	Endpoint from;
	std::vector<std::byte> bytes;
};

// One of this machine's IPv4 addresses (the original's IPEnumeration), with its subnet mask.
struct LocalAddress
{
	std::uint32_t address{0};
	std::uint32_t mask{0};
	// The subnet's broadcast address (all host bits set).
	std::uint32_t Broadcast() const noexcept { return (address & mask) | ~mask; }
};

// This machine's IPv4 addresses, loopback left out, in the system's order.
std::vector<LocalAddress> LocalAddresses();

// This machine's name (for a player's default name).
std::string MachineName();

class UdpSocket
{
public:
	// Bound to `port` on `address` (0: any; port 0: one the system picks), broadcasts allowed
	// when asked; null if the port is taken or sockets are unavailable.
	static std::unique_ptr<UdpSocket> Open(std::uint16_t port, bool broadcast, std::uint32_t address = 0);
	~UdpSocket();
	UdpSocket(const UdpSocket &) = delete;
	UdpSocket &operator=(const UdpSocket &) = delete;

	// Sends one datagram; false if the system refused it (not whether it arrived).
	bool SendTo(Endpoint to, std::span<const std::byte> bytes);
	// The next datagram waiting, if any (never blocks).
	std::optional<Datagram> Receive();
	// The port it is bound to.
	std::uint16_t Port() const noexcept { return m_port; }

private:
	UdpSocket(std::uintptr_t handle, std::uint16_t port) : m_handle(handle), m_port(port) {}
	std::uintptr_t m_handle;
	std::uint16_t m_port;
};
}
