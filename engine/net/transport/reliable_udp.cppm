export module engine.net.transport.reliable_udp;
import std;

export import engine.net.transport.connection;
export import engine.net.transport.udp_socket;

// Reliable, ordered connections over datagrams (UDP), in the manner of ENet and Fiedler's "reliable ordered messages":
// each message is cut into segments of at most `segmentBytes`, numbered in one sequence per direction; the receiver
// acknowledges the next segment it expects and, in a 32-bit field, those it holds beyond it; the sender resends what is
// not acknowledged after a timeout that doubles with each try (up to `maxResend`), and never runs more than `window`
// segments ahead. Segments are delivered in order and messages whole, exactly once. A client says hello (with a token of
// its own) until the host welcomes it; either side may close; a peer not heard from in `timeout` is gone.
// Everything happens in Poll (no threads): call it every frame, before and after the connections are used.
export namespace engine::net
{
// Where a reliable host's datagrams go and come from: a UdpSocket, or a test's lossy link.
struct DatagramLink
{
	std::function<void(Endpoint to, std::span<const std::byte> bytes)> send;
	std::function<std::optional<Datagram>()> receive;
};

inline DatagramLink LinkOf(UdpSocket &socket)
{
	return {[&socket](Endpoint to, std::span<const std::byte> bytes) { socket.SendTo(to, bytes); }, [&socket] { return socket.Receive(); }};
}

struct ReliableSettings
{
	std::chrono::milliseconds resend{100};
	std::chrono::milliseconds maxResend{1000};
	std::chrono::milliseconds timeout{10000};
	std::chrono::milliseconds hello{250};
	std::chrono::milliseconds keepAlive{500};
	std::size_t segmentBytes{1024};
	std::uint32_t window{512};
};

namespace reliable_detail
{
inline constexpr std::uint16_t Magic = 0x5A48;
enum class Kind : std::uint8_t
{
	Hello = 1,
	Welcome = 2,
	Data = 3,
	Ack = 4,
	Close = 5,
};

using Clock = std::chrono::steady_clock;

struct Segment
{
	std::vector<std::byte> payload;
	bool last{false};
	Clock::time_point sent{};
	std::uint32_t tries{0};
};

enum class State : std::uint8_t
{
	Connecting,
	Connected,
	Closed,
};

struct Peer
{
	Endpoint remote;
	std::uint32_t token{0};
	State state{State::Connecting};
	bool closeWanted{false}; // this side closed: send Close
	// Sending.
	std::deque<std::vector<std::byte>> outbox;     // messages not yet cut into segments
	std::map<std::uint32_t, Segment> unacked;      // by sequence
	std::uint32_t nextSequence{0};
	std::uint32_t base{0};                         // lowest sequence not yet acknowledged
	// Receiving.
	std::uint32_t expected{0};
	std::map<std::uint32_t, Segment> held;         // beyond `expected`
	std::vector<std::byte> partial;                // the message being put together
	std::deque<std::vector<std::byte>> inbox;      // whole messages, in order
	bool ackDue{false};
	Clock::time_point heard{};
	Clock::time_point lastSent{};
	Clock::time_point lastHello{};
};

inline void PutU16(std::vector<std::byte> &out, std::uint16_t value)
{
	out.push_back(static_cast<std::byte>(value & 0xFF));
	out.push_back(static_cast<std::byte>(value >> 8));
}
inline void PutU32(std::vector<std::byte> &out, std::uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8)
		out.push_back(static_cast<std::byte>((value >> shift) & 0xFF));
}
inline std::uint32_t GetU32(std::span<const std::byte> bytes, std::size_t at)
{
	std::uint32_t value = 0;
	for (int index = 0; index < 4; ++index)
		value |= std::to_integer<std::uint32_t>(bytes[at + static_cast<std::size_t>(index)]) << (8 * index);
	return value;
}
inline std::vector<std::byte> Header(Kind kind, std::uint32_t token)
{
	std::vector<std::byte> out;
	PutU16(out, Magic);
	out.push_back(static_cast<std::byte>(kind));
	PutU32(out, token);
	return out;
}
inline constexpr std::size_t HeaderBytes = 7;

// Whether `a` comes before `b` in a sequence that wraps.
inline bool Before(std::uint32_t a, std::uint32_t b) noexcept { return static_cast<std::int32_t>(a - b) < 0; }
}

class ReliableUdpHost;

// One end of a reliable connection (its state lives with the host that polls it).
class ReliableUdpConnection final : public Connection
{
public:
	explicit ReliableUdpConnection(std::shared_ptr<reliable_detail::Peer> peer) : m_peer(std::move(peer)) {}
	~ReliableUdpConnection() override { Close(); }
	ReliableUdpConnection(const ReliableUdpConnection &) = delete;
	ReliableUdpConnection &operator=(const ReliableUdpConnection &) = delete;

	void Send(std::vector<std::byte> message) override
	{
		if (Open())
			m_peer->outbox.push_back(std::move(message));
	}
	std::optional<std::vector<std::byte>> Receive() override
	{
		if (m_peer->inbox.empty())
			return std::nullopt;
		std::vector<std::byte> message = std::move(m_peer->inbox.front());
		m_peer->inbox.pop_front();
		return message;
	}
	bool Open() const noexcept override { return m_peer->state != reliable_detail::State::Closed && !m_peer->closeWanted; }
	void Close() noexcept override
	{
		if (m_peer->state != reliable_detail::State::Closed)
			m_peer->closeWanted = true;
	}
	// Welcomed by the host (a client's connection; an accepted one always is).
	bool Connected() const noexcept { return m_peer->state == reliable_detail::State::Connected; }
	Endpoint Remote() const noexcept { return m_peer->remote; }

private:
	std::shared_ptr<reliable_detail::Peer> m_peer;
};

class ReliableUdpHost
{
public:
	explicit ReliableUdpHost(DatagramLink link, ReliableSettings settings = {}) : m_link(std::move(link)), m_settings(settings) {}

	// Takes connections that say hello (a hosting machine).
	void Listen(bool on) noexcept { m_listening = on; }

	// A connection to `to`: it says hello until welcomed (messages sent meanwhile wait).
	std::unique_ptr<ReliableUdpConnection> Connect(Endpoint to)
	{
		auto peer = std::make_shared<reliable_detail::Peer>();
		peer->remote = to;
		peer->token = NewToken();
		peer->state = reliable_detail::State::Connecting;
		peer->heard = reliable_detail::Clock::now();
		m_peers.push_back(peer);
		return std::make_unique<ReliableUdpConnection>(peer);
	}

	// The connections accepted since last asked.
	std::vector<std::unique_ptr<ReliableUdpConnection>> Accept() { return std::exchange(m_accepted, {}); }

	// Receives, acknowledges, sends and resends; forgets peers gone.
	void Poll(reliable_detail::Clock::time_point now = reliable_detail::Clock::now())
	{
		using namespace reliable_detail;
		while (m_link.receive)
		{
			auto datagram = m_link.receive();
			if (!datagram)
				break;
			Handle(*datagram, now);
		}
		for (const auto &peer : m_peers)
			Pump(*peer, now);
		std::erase_if(m_peers, [](const std::shared_ptr<Peer> &peer) { return peer->state == State::Closed && peer.use_count() == 1; });
	}

	std::size_t PeerCount() const noexcept { return m_peers.size(); }

private:
	std::uint32_t NewToken()
	{
		std::uint32_t token = 0;
		while (token == 0)
			token = static_cast<std::uint32_t>(m_random());
		return token;
	}

	void Send(reliable_detail::Peer &peer, std::span<const std::byte> bytes, reliable_detail::Clock::time_point now)
	{
		if (m_link.send)
			m_link.send(peer.remote, bytes);
		peer.lastSent = now;
	}

	std::shared_ptr<reliable_detail::Peer> Find(Endpoint from, std::uint32_t token)
	{
		for (const auto &peer : m_peers)
			if (peer->remote == from && peer->token == token)
				return peer;
		return nullptr;
	}

	void Handle(const Datagram &datagram, reliable_detail::Clock::time_point now)
	{
		using namespace reliable_detail;
		const std::span<const std::byte> bytes = datagram.bytes;
		if (bytes.size() < HeaderBytes || (std::to_integer<std::uint16_t>(bytes[0]) | (std::to_integer<std::uint16_t>(bytes[1]) << 8)) != Magic)
			return;
		const auto kind = static_cast<Kind>(std::to_integer<std::uint8_t>(bytes[2]));
		const std::uint32_t token = GetU32(bytes, 3);
		std::shared_ptr<Peer> peer = Find(datagram.from, token);
		if (kind == Kind::Hello)
		{
			if (peer == nullptr)
			{
				if (!m_listening)
					return;
				peer = std::make_shared<Peer>();
				peer->remote = datagram.from;
				peer->token = token;
				peer->state = State::Connected;
				m_peers.push_back(peer);
				m_accepted.push_back(std::make_unique<ReliableUdpConnection>(peer));
			}
			peer->heard = now;
			const std::vector<std::byte> welcome = Header(Kind::Welcome, token);
			Send(*peer, welcome, now);
			return;
		}
		if (peer == nullptr || peer->state == State::Closed)
			return;
		peer->heard = now;
		switch (kind)
		{
		case Kind::Welcome:
			if (peer->state == State::Connecting)
				peer->state = State::Connected;
			break;
		case Kind::Close:
			peer->state = State::Closed;
			break;
		case Kind::Ack:
			if (bytes.size() >= HeaderBytes + 8)
				Acknowledge(*peer, GetU32(bytes, HeaderBytes), GetU32(bytes, HeaderBytes + 4));
			break;
		case Kind::Data:
			if (bytes.size() >= HeaderBytes + 5)
			{
				if (peer->state == State::Connecting)
					peer->state = State::Connected; // its welcome was lost; data says the same
				Receive(*peer, GetU32(bytes, HeaderBytes), std::to_integer<std::uint8_t>(bytes[HeaderBytes + 4]) != 0, bytes.subspan(HeaderBytes + 5));
			}
			break;
		default:
			break;
		}
	}

	void Acknowledge(reliable_detail::Peer &peer, std::uint32_t next, std::uint32_t beyond)
	{
		using reliable_detail::Before;
		for (auto it = peer.unacked.begin(); it != peer.unacked.end();)
		{
			const std::uint32_t sequence = it->first;
			const bool below = Before(sequence, next);
			const std::uint32_t offset = sequence - next - 1;
			const bool selective = !below && sequence != next && offset < 32 && ((beyond >> offset) & 1u) != 0;
			it = below || selective ? peer.unacked.erase(it) : std::next(it);
		}
		if (Before(peer.base, next))
			peer.base = next;
	}

	void Receive(reliable_detail::Peer &peer, std::uint32_t sequence, bool last, std::span<const std::byte> payload)
	{
		using reliable_detail::Before;
		peer.ackDue = true;
		if (Before(sequence, peer.expected) || sequence - peer.expected >= m_settings.window)
			return; // already had it, or too far ahead
		if (!peer.held.contains(sequence))
			peer.held[sequence] = {std::vector<std::byte>(payload.begin(), payload.end()), last, {}, 0};
		for (auto found = peer.held.find(peer.expected); found != peer.held.end(); found = peer.held.find(peer.expected))
		{
			peer.partial.insert(peer.partial.end(), found->second.payload.begin(), found->second.payload.end());
			if (found->second.last)
				peer.inbox.push_back(std::exchange(peer.partial, {}));
			peer.held.erase(found);
			++peer.expected;
		}
	}

	void Pump(reliable_detail::Peer &peer, reliable_detail::Clock::time_point now)
	{
		using namespace reliable_detail;
		if (peer.state == State::Closed)
			return;
		if (now - peer.heard > m_settings.timeout)
		{
			peer.state = State::Closed;
			return;
		}
		if (peer.closeWanted)
		{
			const std::vector<std::byte> close = Header(Kind::Close, peer.token);
			for (int copy = 0; copy < 3; ++copy)
				Send(peer, close, now);
			peer.state = State::Closed;
			return;
		}
		if (peer.state == State::Connecting)
		{
			if (now - peer.lastHello >= m_settings.hello)
			{
				const std::vector<std::byte> hello = Header(Kind::Hello, peer.token);
				Send(peer, hello, now);
				peer.lastHello = now;
			}
			return;
		}
		// Cut waiting messages into segments.
		while (!peer.outbox.empty())
		{
			const std::vector<std::byte> message = std::move(peer.outbox.front());
			peer.outbox.pop_front();
			std::size_t at = 0;
			do
			{
				const std::size_t length = std::min(m_settings.segmentBytes, message.size() - at);
				Segment segment;
				segment.payload.assign(message.begin() + static_cast<std::ptrdiff_t>(at), message.begin() + static_cast<std::ptrdiff_t>(at + length));
				at += length;
				segment.last = at >= message.size();
				peer.unacked.emplace(peer.nextSequence++, std::move(segment));
			} while (at < message.size());
		}
		// Send what is new within the window, and again what waited too long.
		for (auto &[sequence, segment] : peer.unacked)
		{
			if (sequence - peer.base >= m_settings.window)
				break;
			const auto wait = std::min<std::chrono::milliseconds>(m_settings.resend * (1u << std::min<std::uint32_t>(segment.tries, 4)), m_settings.maxResend);
			if (segment.tries != 0 && now - segment.sent < wait)
				continue;
			std::vector<std::byte> packet = Header(Kind::Data, peer.token);
			PutU32(packet, sequence);
			packet.push_back(static_cast<std::byte>(segment.last ? 1 : 0));
			packet.insert(packet.end(), segment.payload.begin(), segment.payload.end());
			Send(peer, packet, now);
			segment.sent = now;
			++segment.tries;
		}
		// Acknowledge what came in (and keep the link alive).
		if (peer.ackDue || now - peer.lastSent >= m_settings.keepAlive)
		{
			std::uint32_t beyond = 0;
			for (std::uint32_t offset = 0; offset < 32; ++offset)
				if (peer.held.contains(peer.expected + 1 + offset))
					beyond |= 1u << offset;
			std::vector<std::byte> ack = Header(Kind::Ack, peer.token);
			PutU32(ack, peer.expected);
			PutU32(ack, beyond);
			Send(peer, ack, now);
			peer.ackDue = false;
		}
	}

	DatagramLink m_link;
	ReliableSettings m_settings;
	bool m_listening{false};
	std::vector<std::shared_ptr<reliable_detail::Peer>> m_peers;
	std::vector<std::unique_ptr<ReliableUdpConnection>> m_accepted;
	std::mt19937 m_random{std::random_device{}()};
};
}
