module;
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
export module engine.platform.adapters.sdl3.network.socket;
import engine.platform.network.socket;
import engine.platform.network;

export namespace engine::platform::sdl3
{
class SDL3Socket final : public ISocket
{
public:
	SDL3Socket(SocketKind kind, AddressFamily family) noexcept : m_kind(kind), m_family(family) {}
	~SDL3Socket() override
	{
		if (m_server) NET_DestroyServer(m_server);
		if (m_stream) NET_DestroyStreamSocket(m_stream);
		if (m_datagrams) NET_DestroyDatagramSocket(m_datagrams);
		if (m_peer) NET_UnrefAddress(m_peer);
	}
	bool bind(const NetworkEndpoint& endpoint) override
	{
		if (m_server || m_datagrams) return false;
		NET_Address* address = endpoint.host.empty() ? nullptr : resolve(endpoint.host);
		if (!endpoint.host.empty() && !address) return false;
		if (m_kind == SocketKind::stream) m_server = NET_CreateServer(address, endpoint.port, 0);
		else {
			SDL_PropertiesID props = SDL_CreateProperties();
			SDL_SetBooleanProperty(props, NET_PROP_DATAGRAM_SOCKET_ALLOW_BROADCAST_BOOLEAN, m_broadcast);
			SDL_SetBooleanProperty(props, NET_PROP_DATAGRAM_SOCKET_REUSEADDR_BOOLEAN, m_reuse_address);
			m_datagrams = NET_CreateDatagramSocket(address, endpoint.port, props);
			SDL_DestroyProperties(props);
		}
		if (address) NET_UnrefAddress(address);
		return m_server != nullptr || m_datagrams != nullptr;
	}
	bool connect(const NetworkEndpoint& endpoint) override
	{
		if (endpoint.host.empty()) return false;
		NET_Address* address = resolve(endpoint.host);
		if (!address) return false;
		if (m_kind == SocketKind::stream) {
			m_stream = NET_CreateClient(address, endpoint.port, 0);
			if (m_stream && NET_WaitUntilConnected(m_stream, -1) != NET_SUCCESS) { NET_DestroyStreamSocket(m_stream); m_stream = nullptr; }
		} else {
			if (m_peer) NET_UnrefAddress(m_peer);
			m_peer = NET_RefAddress(address);
			m_peer_port = endpoint.port;
			if (!m_datagrams) m_datagrams = create_datagram_socket(nullptr, 0);
		}
		NET_UnrefAddress(address);
		return m_kind == SocketKind::stream ? m_stream != nullptr : m_datagrams != nullptr;
	}
	[[nodiscard]] std::unique_ptr<ISocket> accept() override
	{
		if (!m_server) return {};
		NET_StreamSocket* stream{};
		if (!NET_AcceptClient(m_server, &stream) || !stream) return {};
		return std::unique_ptr<ISocket>(new SDL3Socket(stream));
	}
	bool set_blocking(bool blocking) override { m_blocking = blocking; return true; }
	bool set_option(SocketOption option, int value) override
	{
		if (m_server || m_datagrams) return false;
		switch (option) {
		case SocketOption::broadcast: m_broadcast = value != 0; return true;
		case SocketOption::reuse_address: m_reuse_address = value != 0; return true;
		default: return false;
		}
	}
	[[nodiscard]] std::optional<int> option(SocketOption option) const override
	{
		switch (option) {
		case SocketOption::broadcast: return m_broadcast ? 1 : 0;
		case SocketOption::reuse_address: return m_reuse_address ? 1 : 0;
		default: return std::nullopt;
		}
	}
	SocketWaitResult wait(SocketReadiness interest, std::uint32_t timeout) override
	{
		void* socket = waitable();
		if (!socket) return {SocketReadiness::none, false};
		if ((static_cast<unsigned>(interest) & static_cast<unsigned>(SocketReadiness::writable)) != 0 && m_stream && NET_GetConnectionStatus(m_stream) == NET_SUCCESS)
			return {SocketReadiness::writable, false};
		void* sockets[] = {socket};
		const int result = NET_WaitUntilInputAvailable(sockets, 1, timeout > INT_MAX ? INT_MAX : static_cast<int>(timeout));
		if (result < 0) return {SocketReadiness::error, false};
		if (result == 0) return {SocketReadiness::none, true};
		return {SocketReadiness::readable, false};
	}
	std::int64_t send(std::span<const std::byte> data) override
	{
		if (m_stream) return NET_WriteToStreamSocket(m_stream, data.data(), checked_size(data.size())) ? static_cast<std::int64_t>(data.size()) : -1;
		if (m_datagrams && m_peer) return NET_SendDatagram(m_datagrams, m_peer, m_peer_port, data.data(), checked_size(data.size())) ? static_cast<std::int64_t>(data.size()) : -1;
		return -1;
	}
	std::int64_t receive(std::span<std::byte> data) override
	{
		if (m_stream) {
			if (m_blocking && wait(SocketReadiness::readable, 0xFFFFFFFFu).ready != SocketReadiness::readable) return -1;
			return NET_ReadFromStreamSocket(m_stream, data.data(), checked_size(data.size()));
		}
		NetworkEndpoint ignored;
		return receive_from(data, ignored);
	}
	std::int64_t send_to(std::span<const std::byte> data, const NetworkEndpoint& endpoint) override
	{
		if (m_kind != SocketKind::datagram) return -1;
		NET_Address* address = resolve(endpoint.host);
		if (!address) return -1;
		if (!m_datagrams) m_datagrams = create_datagram_socket(nullptr, 0);
		const bool sent = m_datagrams && NET_SendDatagram(m_datagrams, address, endpoint.port, data.data(), checked_size(data.size()));
		NET_UnrefAddress(address);
		return sent ? static_cast<std::int64_t>(data.size()) : -1;
	}
	std::int64_t receive_from(std::span<std::byte> data, NetworkEndpoint& endpoint) override
	{
		if (!m_datagrams) return -1;
		if (m_blocking && wait(SocketReadiness::readable, 0xFFFFFFFFu).ready != SocketReadiness::readable) return -1;
		NET_Datagram* datagram{};
		if (!NET_ReceiveDatagram(m_datagrams, &datagram) || !datagram) return 0;
		const auto count = static_cast<std::size_t>(std::max(0, datagram->buflen));
		const auto copied = std::min(count, data.size());
		if (copied) std::copy_n(reinterpret_cast<const std::byte*>(datagram->buf), copied, data.data());
		endpoint.host = datagram->addr && NET_GetAddressString(datagram->addr) ? NET_GetAddressString(datagram->addr) : "";
		endpoint.port = datagram->port;
		NET_DestroyDatagram(datagram);
		return static_cast<std::int64_t>(copied);
	}
private:
	explicit SDL3Socket(NET_StreamSocket* stream) noexcept : m_kind(SocketKind::stream), m_stream(stream) {}
	static int checked_size(std::size_t size) noexcept { return static_cast<int>(std::min<std::size_t>(size, INT_MAX)); }
	static NET_Address* resolve(const std::string& host)
	{
		NET_Address* address = NET_ResolveHostname(host.c_str());
		if (!address) return nullptr;
		if (NET_WaitUntilResolved(address, -1) != NET_SUCCESS) { NET_UnrefAddress(address); return nullptr; }
		return address;
	}
	NET_DatagramSocket* create_datagram_socket(NET_Address* address, Uint16 port)
	{
		SDL_PropertiesID props = SDL_CreateProperties();
		SDL_SetBooleanProperty(props, NET_PROP_DATAGRAM_SOCKET_ALLOW_BROADCAST_BOOLEAN, m_broadcast);
		SDL_SetBooleanProperty(props, NET_PROP_DATAGRAM_SOCKET_REUSEADDR_BOOLEAN, m_reuse_address);
		auto* socket = NET_CreateDatagramSocket(address, port, props);
		SDL_DestroyProperties(props);
		return socket;
	}
	void* waitable() const noexcept { return m_server ? static_cast<void*>(m_server) : m_stream ? static_cast<void*>(m_stream) : static_cast<void*>(m_datagrams); }
	SocketKind m_kind{};
	AddressFamily m_family{};
	NET_Server* m_server{};
	NET_StreamSocket* m_stream{};
	NET_DatagramSocket* m_datagrams{};
	NET_Address* m_peer{};
	Uint16 m_peer_port{};
	bool m_blocking{};
	bool m_broadcast{};
	bool m_reuse_address{true};
};
}
