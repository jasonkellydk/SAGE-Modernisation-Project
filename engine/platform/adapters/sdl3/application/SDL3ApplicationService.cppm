module;
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include <filesystem>
#include <algorithm>
#include <array>
#include <climits>
#include <string>
export module engine.platform.adapters.sdl3.application;
import engine.platform.application;

export namespace engine::platform::sdl3
{
class SDL3ApplicationService final : public IApplicationService
{
public:
	~SDL3ApplicationService() override
	{
		if (m_instanceSocket) NET_DestroyDatagramSocket(m_instanceSocket);
		if (m_loopback) NET_UnrefAddress(m_loopback);
	}
	[[nodiscard]] std::filesystem::path executable_directory() const override
	{
		const char* path = SDL_GetBasePath();
		return path ? std::filesystem::path(path) : std::filesystem::path{};
	}
	bool acquire_single_instance(const std::string& identifier) override
	{
		if (identifier.empty() || (m_instanceSocket && identifier == m_identifier)) return m_instanceSocket != nullptr;
		if (m_instanceSocket || !loopback_address()) return false;
		const Uint16 port = activation_port(identifier);
		m_instanceSocket = NET_CreateDatagramSocket(m_loopback, port, 0);
		if (!m_instanceSocket) return false;
		m_identifier = identifier;
		return true;
	}
	bool send_activation_request(const std::string& identifier) override
	{
		if (identifier.empty() || !loopback_address()) return false;
		NET_DatagramSocket* socket = NET_CreateDatagramSocket(m_loopback, 0, 0);
		if (!socket) return false;
		const bool sent = NET_SendDatagram(socket, m_loopback, activation_port(identifier), identifier.data(), static_cast<int>(identifier.size()));
		NET_DestroyDatagramSocket(socket);
		return sent;
	}
	bool poll_activation_request(std::uint32_t timeout) override
	{
		if (!m_instanceSocket) return false;
		void* sockets[] = { m_instanceSocket };
		const int waitMilliseconds = timeout == 0xFFFFFFFFu ? -1 : (timeout > INT_MAX ? INT_MAX : static_cast<int>(timeout));
		const int result = NET_WaitUntilInputAvailable(sockets, 1, waitMilliseconds);
		if (result <= 0) return false;
		NET_Datagram* datagram{};
		if (!NET_ReceiveDatagram(m_instanceSocket, &datagram) || !datagram) return false;
		const bool matches = datagram->buflen == static_cast<int>(m_identifier.size()) &&
			std::equal(m_identifier.begin(), m_identifier.end(), reinterpret_cast<const char*>(datagram->buf));
		NET_DestroyDatagram(datagram);
		return matches;
	}
	bool activate_window(WindowId id) override
	{
		auto* window = SDL_GetWindowFromID(id);
		if (!window) return false;
		SDL_ShowWindow(window);
		return SDL_RaiseWindow(window);
	}
private:
	bool loopback_address()
	{
		if (m_loopback) return true;
		m_loopback = NET_ResolveHostname("127.0.0.1");
		if (!m_loopback) return false;
		if (NET_WaitUntilResolved(m_loopback, -1) != NET_SUCCESS) { NET_UnrefAddress(m_loopback); m_loopback = nullptr; return false; }
		return true;
	}
	static Uint16 activation_port(const std::string& identifier) noexcept
	{
		std::uint32_t hash = 2166136261u;
		for (const auto value : identifier) { hash ^= static_cast<unsigned char>(value); hash *= 16777619u; }
		return static_cast<Uint16>(20000u + hash % 40000u);
	}
	NET_Address* m_loopback{};
	NET_DatagramSocket* m_instanceSocket{};
	std::string m_identifier;
};
}
