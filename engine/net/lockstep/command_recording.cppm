export module engine.net.lockstep.command_recording;
import std;

export import engine.net.lockstep.protocol;

// A match as a replay records it: every tick's commands as the simulation stepped through them (the ticks with any,
// in order), the simulation's state hash every so often (to catch a playback going its own way), and how many ticks it
// ran. Whatever ran the ticks (a relay's player, a joined peer, single player) records the same thing.
export namespace engine::net
{
struct RecordedTick
{
	std::uint64_t tick{0};
	std::vector<CommandEnvelope> commands;
};

struct CommandRecording
{
	std::vector<RecordedTick> ticks;
	std::vector<std::pair<std::uint64_t, std::uint64_t>> hashes; // (the tick just run, the state hash after it)
	std::uint64_t endTick{0};                                     // the tick the next step would run
	std::uint64_t hashInterval{100};                              // REPLAY_CRC_INTERVAL
};

// The tick `tick` ran with `commands` (none: nothing kept but the count).
inline void RecordTick(CommandRecording &recording, std::uint64_t tick, std::span<const CommandEnvelope> commands)
{
	if (!commands.empty())
		recording.ticks.push_back({tick, {commands.begin(), commands.end()}});
	recording.endTick = tick + 1;
}

// After tick `tick` ran: its state hash when the interval says so.
inline void RecordHash(CommandRecording &recording, std::uint64_t tick, std::uint64_t hash)
{
	if (recording.hashInterval != 0 && (tick + 1) % recording.hashInterval == 0)
		recording.hashes.emplace_back(tick, hash);
}

inline void WriteRecording(core::serialization::ByteWriter &writer, const CommandRecording &recording)
{
	writer.U64(recording.endTick);
	writer.U64(recording.hashInterval);
	writer.U32(static_cast<std::uint32_t>(recording.ticks.size()));
	for (const RecordedTick &tick : recording.ticks)
	{
		writer.U64(tick.tick);
		writer.U32(static_cast<std::uint32_t>(tick.commands.size()));
		for (const CommandEnvelope &command : tick.commands)
		{
			writer.U64(command.type);
			writer.U32(command.player);
			writer.Blob(command.payload);
		}
	}
	writer.U32(static_cast<std::uint32_t>(recording.hashes.size()));
	for (const auto &[tick, hash] : recording.hashes)
	{
		writer.U64(tick);
		writer.U64(hash);
	}
}

inline std::optional<CommandRecording> ReadRecording(core::serialization::ByteReader &reader)
{
	CommandRecording recording;
	const auto endTick = reader.U64();
	const auto interval = reader.U64();
	const auto count = reader.U32();
	if (!endTick || !interval || !count)
		return std::nullopt;
	recording.endTick = *endTick;
	recording.hashInterval = *interval;
	for (std::uint32_t index = 0; index < *count; ++index)
	{
		RecordedTick tick;
		const auto at = reader.U64();
		const auto commands = reader.U32();
		if (!at || !commands || (!recording.ticks.empty() && *at <= recording.ticks.back().tick) || *at >= recording.endTick)
			return std::nullopt;
		tick.tick = *at;
		for (std::uint32_t command = 0; command < *commands; ++command)
		{
			const auto type = reader.U64();
			const auto player = reader.U32();
			auto payload = reader.Blob();
			if (!type || !player || !payload)
				return std::nullopt;
			tick.commands.push_back({*type, *player, std::move(*payload)});
		}
		recording.ticks.push_back(std::move(tick));
	}
	const auto hashes = reader.U32();
	if (!hashes)
		return std::nullopt;
	for (std::uint32_t index = 0; index < *hashes; ++index)
	{
		const auto tick = reader.U64();
		const auto hash = reader.U64();
		if (!tick || !hash)
			return std::nullopt;
		recording.hashes.emplace_back(*tick, *hash);
	}
	return recording;
}
}
