export module engine.net.authority.server;
import std;
export import engine.net.transport.connection;
export import engine.core.serialization.byte_stream;

export namespace engine::net::authority
{
using PeerId = std::uint32_t;
struct Input
{
	PeerId peer;
	std::uint64_t sequence;
	std::uint64_t tick;
	std::vector<std::byte> payload;
};
struct Limits
{
	std::size_t players{4096};
	std::size_t inputsPerTick{32};
	std::size_t payloadBytes{4096};
	std::uint64_t futureTicks{8};
	std::size_t snapshotBytes{1024 * 1024};
};
inline std::vector<std::byte> EncodeInput(std::uint64_t sequence, std::uint64_t tick, std::span<const std::byte> payload)
{
	core::serialization::ByteWriter writer;
	writer.U32(1); // version
	writer.U8(1); // input
	writer.U64(sequence);
	writer.U64(tick);
	writer.Blob(payload);
	return writer.Take();
}
struct Snapshot { std::uint64_t tick; std::vector<std::byte> payload; };
inline std::optional<Snapshot> DecodeSnapshot(std::span<const std::byte> packet)
{
	core::serialization::ByteReader reader(packet);
	const auto version = reader.U32(); const auto kind = reader.U8(); const auto tick = reader.U64(); const auto payload = reader.Blob();
	if (!version || *version != 1 || !kind || *kind != 2 || !tick || !payload || !reader.AtEnd()) return std::nullopt;
	return Snapshot{*tick, *payload};
}

// Game-neutral authoritative server boundary over the existing transport.
// Identity comes from the host's admitted connection, never the packet. A slow
// or absent peer does not stall ticks. Game code validates the opaque input.
// Call on the session thread; spatial interest and prediction are later layers.
class Server
{
public:
	explicit Server(Limits limits = {}) : m_limits(limits)
	{
		if (!limits.players || !limits.inputsPerTick || !limits.payloadBytes || !limits.snapshotBytes ||
			limits.payloadBytes > 1024 * 1024 || limits.snapshotBytes > 16 * 1024 * 1024)
			throw std::invalid_argument("invalid authority limits");
	}
	bool Connect(PeerId id, std::unique_ptr<Connection> connection)
	{
		if (!id || !connection || !connection->Open() || m_peers.size() >= m_limits.players || m_peers.contains(id)) return false;
		m_peers.emplace(id, Peer{std::move(connection)});
		return true;
	}
	void Disconnect(PeerId id)
	{
		if (const auto found = m_peers.find(id); found != m_peers.end()) found->second.connection->Close();
		m_peers.erase(id);
	}
	std::vector<Input> BeginTick(std::uint64_t tick)
	{
		if (tick <= m_tick) throw std::invalid_argument("authority ticks must advance");
		m_tick = tick;
		std::erase_if(m_peers, [](const auto &entry) { return !entry.second.connection->Open(); });
		std::vector<Input> ready;
		for (auto &[id, peer] : m_peers)
		{
			std::erase_if(peer.pending, [=](const Input &input) { return input.tick < tick; });
			for (std::size_t i = 0; i < m_limits.inputsPerTick; ++i)
			{
				auto message = peer.connection->Receive();
				if (!message) break;
				if (message->size() > m_limits.payloadBytes + 25) continue;
				core::serialization::ByteReader reader(*message);
				const auto version = reader.U32(); const auto kind = reader.U8();
				const auto sequence = reader.U64(); const auto when = reader.U64(); const auto payload = reader.Blob();
				if (!version || *version != 1 || !kind || *kind != 1 || !sequence || !when || !payload || !reader.AtEnd() ||
					*sequence <= peer.lastSequence || *when < tick || *when - tick > m_limits.futureTicks || payload->size() > m_limits.payloadBytes)
					continue;
				// Bound retained future inputs as well as work per tick.
				if (peer.pending.size() >= m_limits.inputsPerTick) continue;
				peer.lastSequence = *sequence;
				peer.pending.push_back({id, *sequence, *when, std::move(*payload)});
			}
			for (auto &input : peer.pending) if (input.tick == tick) ready.push_back(std::move(input));
			std::erase_if(peer.pending, [=](const Input &input) { return input.tick == tick; });
		}
		std::ranges::sort(ready, {}, [](const Input &input) { return std::pair{input.peer, input.sequence}; });
		return ready;
	}
	void Publish(std::span<const std::byte> snapshot)
	{
		if (!m_tick || snapshot.size() > m_limits.snapshotBytes) throw std::invalid_argument("snapshot outside authority limits");
		core::serialization::ByteWriter writer;
		writer.U32(1); writer.U8(2); writer.U64(m_tick); writer.Blob(snapshot);
		for (auto &[id, peer] : m_peers) if (peer.connection->Open()) peer.connection->Send(writer.Bytes());
	}
	std::size_t Players() const noexcept { return m_peers.size(); }
private:
	struct Peer { std::unique_ptr<Connection> connection; std::uint64_t lastSequence{0}; std::vector<Input> pending; };
	Limits m_limits;
	std::uint64_t m_tick{0};
	std::map<PeerId, Peer> m_peers;
};
}
