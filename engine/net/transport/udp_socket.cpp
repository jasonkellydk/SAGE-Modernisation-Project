module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

module engine.net.transport.udp_socket;

namespace engine::net
{
namespace
{
#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket InvalidSocket = INVALID_SOCKET;

// Winsock is started once, for as long as the process runs.
bool StartSockets()
{
	static std::once_flag once;
	static bool started = false;
	std::call_once(once, [] {
		WSADATA data{};
		started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
	});
	return started;
}
void CloseNative(NativeSocket socket) { closesocket(socket); }
bool MakeNonBlocking(NativeSocket socket)
{
	u_long on = 1;
	return ioctlsocket(socket, FIONBIO, &on) == 0;
}
#else
using NativeSocket = int;
constexpr NativeSocket InvalidSocket = -1;
bool StartSockets() { return true; }
void CloseNative(NativeSocket socket) { close(socket); }
bool MakeNonBlocking(NativeSocket socket)
{
	const int flags = fcntl(socket, F_GETFL, 0);
	return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

sockaddr_in ToNative(Endpoint endpoint)
{
	sockaddr_in native{};
	native.sin_family = AF_INET;
	native.sin_port = htons(endpoint.port);
	native.sin_addr.s_addr = htonl(endpoint.address);
	return native;
}
}

std::string ToString(std::uint32_t address)
{
	return std::to_string(address >> 24) + '.' + std::to_string((address >> 16) & 0xFF) + '.' + std::to_string((address >> 8) & 0xFF) + '.' +
		std::to_string(address & 0xFF);
}

std::vector<LocalAddress> LocalAddresses()
{
	std::vector<LocalAddress> found;
	if (!StartSockets())
		return found;
#if defined(_WIN32)
	ULONG size = 16 * 1024;
	std::vector<std::byte> buffer(size);
	ULONG result = ERROR_BUFFER_OVERFLOW;
	for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt)
	{
		buffer.resize(size);
		result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
			reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()), &size);
	}
	if (result != NO_ERROR)
		return found;
	for (auto *adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()); adapter != nullptr; adapter = adapter->Next)
	{
		if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
			continue;
		for (auto *unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next)
		{
			if (unicast->Address.lpSockaddr == nullptr || unicast->Address.lpSockaddr->sa_family != AF_INET)
				continue;
			const auto *ip = reinterpret_cast<const sockaddr_in *>(unicast->Address.lpSockaddr);
			const std::uint32_t address = ntohl(ip->sin_addr.s_addr);
			const unsigned bits = unicast->OnLinkPrefixLength <= 32 ? unicast->OnLinkPrefixLength : 24;
			const std::uint32_t mask = bits == 0 ? 0 : 0xFFFFFFFFu << (32 - bits);
			if ((address >> 24) != 127)
				found.push_back({address, mask});
		}
	}
#else
	ifaddrs *list = nullptr;
	if (getifaddrs(&list) != 0)
		return found;
	for (ifaddrs *entry = list; entry != nullptr; entry = entry->ifa_next)
	{
		if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET || (entry->ifa_flags & IFF_LOOPBACK) != 0 || (entry->ifa_flags & IFF_UP) == 0)
			continue;
		const std::uint32_t address = ntohl(reinterpret_cast<const sockaddr_in *>(entry->ifa_addr)->sin_addr.s_addr);
		const std::uint32_t mask = entry->ifa_netmask != nullptr ? ntohl(reinterpret_cast<const sockaddr_in *>(entry->ifa_netmask)->sin_addr.s_addr) : 0xFFFFFF00u;
		found.push_back({address, mask});
	}
	freeifaddrs(list);
#endif
	return found;
}

std::string MachineName()
{
	if (!StartSockets())
		return {};
	std::array<char, 256> name{};
	if (gethostname(name.data(), static_cast<int>(name.size() - 1)) != 0)
		return {};
	return name.data();
}

std::unique_ptr<UdpSocket> UdpSocket::Open(std::uint16_t port, bool broadcast, std::uint32_t address)
{
	if (!StartSockets())
		return nullptr;
	const NativeSocket native = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (native == InvalidSocket)
		return nullptr;
	const int on = 1;
	if (broadcast && setsockopt(native, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&on), sizeof(on)) != 0)
	{
		CloseNative(native);
		return nullptr;
	}
	const sockaddr_in bound = ToNative({address, port});
	if (bind(native, reinterpret_cast<const sockaddr *>(&bound), sizeof(bound)) != 0 || !MakeNonBlocking(native))
	{
		CloseNative(native);
		return nullptr;
	}
	sockaddr_in actual{};
#if defined(_WIN32)
	int length = sizeof(actual);
#else
	socklen_t length = sizeof(actual);
#endif
	getsockname(native, reinterpret_cast<sockaddr *>(&actual), &length);
	return std::unique_ptr<UdpSocket>(new UdpSocket(static_cast<std::uintptr_t>(native), ntohs(actual.sin_port)));
}

UdpSocket::~UdpSocket() { CloseNative(static_cast<NativeSocket>(m_handle)); }

bool UdpSocket::SendTo(Endpoint to, std::span<const std::byte> bytes)
{
	const sockaddr_in native = ToNative(to);
	const auto sent = sendto(static_cast<NativeSocket>(m_handle), reinterpret_cast<const char *>(bytes.data()), static_cast<int>(bytes.size()), 0,
		reinterpret_cast<const sockaddr *>(&native), sizeof(native));
	return sent == static_cast<decltype(sent)>(bytes.size());
}

std::optional<Datagram> UdpSocket::Receive()
{
	std::array<std::byte, 2048> buffer{};
	sockaddr_in from{};
#if defined(_WIN32)
	int length = sizeof(from);
#else
	socklen_t length = sizeof(from);
#endif
	for (;;)
	{
		const auto received = recvfrom(static_cast<NativeSocket>(m_handle), reinterpret_cast<char *>(buffer.data()), static_cast<int>(buffer.size()), 0,
			reinterpret_cast<sockaddr *>(&from), &length);
		if (received >= 0)
		{
			Datagram datagram;
			datagram.from = {ntohl(from.sin_addr.s_addr), ntohs(from.sin_port)};
			datagram.bytes.assign(buffer.begin(), buffer.begin() + received);
			return datagram;
		}
#if defined(_WIN32)
		// A datagram to an unreachable port comes back as a reset on Windows: skip it.
		if (WSAGetLastError() == WSAECONNRESET)
			continue;
#endif
		return std::nullopt;
	}
}
}
