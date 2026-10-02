export module engine.net.lockstep.protocol;
import std;

export import engine.core.serialization.byte_stream;

// The lockstep wire protocol between peers and the relay.
//
//   join        Hello (role, seat to rejoin) -> Welcome (seat, match settings)
//   play        InputFrame per tick and player -> TickBundle per tick to all
//   verify      StateHash reports; the relay votes and sends Desync to the
//               peers that diverged
//   repair      CheckpointRequest to a healthy peer -> Checkpoint -> Resync
//               (state plus the ticks after it) to whoever needs it: a
//               diverged peer, a rejoining player, a late observer
//   liveliness  Ping / Pong (round trips for the input delay), InputDelay
export namespace engine::net
{
using core::serialization::ByteReader;
using core::serialization::ByteWriter;

struct CommandEnvelope
{
	std::uint64_t type{0};       // the command's stable message key
	std::uint32_t player{0};     // filled in by the relay from the sender's seat
	std::vector<std::byte> payload;

	bool operator==(const CommandEnvelope &) const = default;
};

enum class MessageType : std::uint8_t
{
	Hello = 1,
	Welcome = 2,
	InputFrame = 3,
	TickBundle = 4,
	StateHash = 5,
	Desync = 6,
	CheckpointRequest = 7,
	Checkpoint = 8,
	Resync = 9,
	Ping = 10,
	Pong = 11,
	InputDelay = 12,
};

enum class Role : std::uint8_t
{
	Player = 0,
	Observer = 1,
};

inline constexpr std::uint32_t NoSeat = 0xFFFFFFFFu;

struct Hello
{
	Role role{Role::Player};
	// A player's seat to take back after a disconnect (NoSeat: any free seat).
	std::uint32_t seat{NoSeat};
};

struct Welcome
{
	Role role{Role::Player};
	std::uint32_t seat{0}; // player index, or observer number
	std::uint32_t players{1};
	std::uint64_t seed{0};
	std::uint32_t inputDelay{0};
	// Peers report their state hash on every tick divisible by this.
	std::uint32_t hashInterval{0};
	// The first tick this seat's input may still go into (later joins).
	std::uint64_t firstInputTick{1};
	// A checkpoint follows (Resync) before the joiner may run: the match is under way.
	bool awaitResync{false};
};

struct InputFrame
{
	std::uint64_t tick{0};
	std::vector<CommandEnvelope> commands;
};

struct TickBundle
{
	std::uint64_t tick{0};
	std::vector<CommandEnvelope> commands;
};

struct StateHashReport
{
	std::uint64_t tick{0};
	std::uint64_t hash{0};
};

// A peer's full simulation state after `tick` (Checkpoint, and Resync).
struct CheckpointData
{
	std::uint64_t tick{0};
	std::uint64_t hash{0};
	std::vector<std::byte> state;
};

namespace wire
{
void Commands(ByteWriter &writer, const std::vector<CommandEnvelope> &commands)
{
	writer.U32(static_cast<std::uint32_t>(commands.size()));
	for (const CommandEnvelope &command : commands)
	{
		writer.U64(command.type);
		writer.U32(command.player);
		writer.Blob(command.payload);
	}
}

std::optional<std::vector<CommandEnvelope>> Commands(ByteReader &reader)
{
	const auto count = reader.U32();
	if (!count)
		return std::nullopt;
	std::vector<CommandEnvelope> commands;
	for (std::uint32_t index = 0; index < *count; ++index)
	{
		const auto type = reader.U64();
		const auto player = reader.U32();
		auto payload = reader.Blob();
		if (!type || !player || !payload)
			return std::nullopt;
		commands.push_back({*type, *player, std::move(*payload)});
	}
	return commands;
}

ByteWriter Start(MessageType type)
{
	ByteWriter writer;
	writer.U8(static_cast<std::uint8_t>(type));
	return writer;
}
}

std::optional<MessageType> TypeOf(std::span<const std::byte> message)
{
	if (message.empty() || static_cast<std::uint8_t>(message.front()) < 1 || static_cast<std::uint8_t>(message.front()) > 12)
		return std::nullopt;
	return static_cast<MessageType>(message.front());
}

std::vector<std::byte> Encode(const Hello &hello)
{
	ByteWriter writer = wire::Start(MessageType::Hello);
	writer.U8(static_cast<std::uint8_t>(hello.role));
	writer.U32(hello.seat);
	return writer.Take();
}

std::vector<std::byte> Encode(const Welcome &welcome)
{
	ByteWriter writer = wire::Start(MessageType::Welcome);
	writer.U8(static_cast<std::uint8_t>(welcome.role));
	writer.U32(welcome.seat);
	writer.U32(welcome.players);
	writer.U64(welcome.seed);
	writer.U32(welcome.inputDelay);
	writer.U32(welcome.hashInterval);
	writer.U64(welcome.firstInputTick);
	writer.Flag(welcome.awaitResync);
	return writer.Take();
}

// InputFrame or TickBundle (same layout).
std::vector<std::byte> Encode(MessageType type, std::uint64_t tick, const std::vector<CommandEnvelope> &commands)
{
	ByteWriter writer = wire::Start(type);
	writer.U64(tick);
	wire::Commands(writer, commands);
	return writer.Take();
}

// StateHash or Desync.
std::vector<std::byte> Encode(MessageType type, const StateHashReport &report)
{
	ByteWriter writer = wire::Start(type);
	writer.U64(report.tick);
	writer.U64(report.hash);
	return writer.Take();
}

// Checkpoint or Resync.
std::vector<std::byte> Encode(MessageType type, const CheckpointData &checkpoint)
{
	ByteWriter writer = wire::Start(type);
	writer.U64(checkpoint.tick);
	writer.U64(checkpoint.hash);
	writer.Blob(checkpoint.state);
	return writer.Take();
}

// CheckpointRequest, Ping, Pong or InputDelay: one number.
std::vector<std::byte> Encode(MessageType type, std::uint64_t value)
{
	ByteWriter writer = wire::Start(type);
	writer.U64(value);
	return writer.Take();
}

std::optional<Hello> DecodeHello(std::span<const std::byte> message)
{
	ByteReader reader(message.subspan(1));
	const auto role = reader.U8();
	const auto seat = reader.U32();
	if (!role || *role > 1 || !seat || !reader.AtEnd())
		return std::nullopt;
	return Hello{static_cast<Role>(*role), *seat};
}

std::optional<Welcome> DecodeWelcome(std::span<const std::byte> message)
{
	ByteReader reader(message.subspan(1));
	const auto role = reader.U8();
	const auto seat = reader.U32();
	const auto players = reader.U32();
	const auto seed = reader.U64();
	const auto delay = reader.U32();
	const auto hashInterval = reader.U32();
	const auto firstInputTick = reader.U64();
	const auto awaitResync = reader.Flag();
	if (!role || *role > 1 || !seat || !players || !seed || !delay || !hashInterval || !firstInputTick || !awaitResync || !reader.AtEnd())
		return std::nullopt;
	return Welcome{static_cast<Role>(*role), *seat, *players, *seed, *delay, *hashInterval, *firstInputTick, *awaitResync};
}

std::optional<TickBundle> DecodeFrame(std::span<const std::byte> message)
{
	ByteReader reader(message.subspan(1));
	const auto tick = reader.U64();
	auto commands = wire::Commands(reader);
	if (!tick || !commands || !reader.AtEnd())
		return std::nullopt;
	return TickBundle{*tick, std::move(*commands)};
}

std::optional<StateHashReport> DecodeHash(std::span<const std::byte> message)
{
	ByteReader reader(message.subspan(1));
	const auto tick = reader.U64();
	const auto hash = reader.U64();
	if (!tick || !hash || !reader.AtEnd())
		return std::nullopt;
	return StateHashReport{*tick, *hash};
}

std::optional<CheckpointData> DecodeCheckpoint(std::span<const std::byte> message)
{
	ByteReader reader(message.subspan(1));
	const auto tick = reader.U64();
	const auto hash = reader.U64();
	auto state = reader.Blob();
	if (!tick || !hash || !state || !reader.AtEnd())
		return std::nullopt;
	return CheckpointData{*tick, *hash, std::move(*state)};
}

std::optional<std::uint64_t> DecodeValue(std::span<const std::byte> message)
{
	ByteReader reader(message.subspan(1));
	const auto value = reader.U64();
	if (!value || !reader.AtEnd())
		return std::nullopt;
	return value;
}
}
