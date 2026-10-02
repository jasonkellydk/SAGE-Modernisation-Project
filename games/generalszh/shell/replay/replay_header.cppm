export module games.generalszh.shell.replay.replay_header;
import std;

// A replay file's header, this port's own (no compatibility with the original's GENREP files): "ZHREP", its format
// version, the start and end times, the frame count, whether it desynced or quit early, the replay's name (UTF-16), the
// date it was made, the game's version, the game options ("M=" names the map) and the local player's slot (-1: none).
// The replay menu lists files by it; what follows it is the game (see session::setup::ReplayBody).
export namespace generalszh::shell
{
struct ReplayDate
{
	std::uint16_t year{0}, month{0}, dayOfWeek{0}, day{0}, hour{0}, minute{0}, second{0}, milliseconds{0};
};

struct ReplayHeader
{
	static constexpr std::uint32_t FormatVersion = 1;

	std::uint32_t startTime{0};
	std::uint32_t endTime{0};
	std::uint32_t frameCount{0};
	bool desynced{false};
	bool quitEarly{false};
	std::u16string name;
	ReplayDate date;
	std::u16string version;
	std::string gameOptions;
	int localPlayer{-1};
	std::size_t size{0}; // its bytes in the file (the game follows)

	// The map the game options name (M=...), its file name only.
	std::string Map() const
	{
		const auto at = gameOptions.find("M=");
		if (at == std::string::npos)
			return {};
		std::string map = gameOptions.substr(at + 2, gameOptions.find(';', at) - at - 2);
		if (const auto slash = map.find_last_of("/\\"); slash != std::string::npos)
			map = map.substr(slash + 1);
		return map;
	}
};

inline constexpr std::string_view ReplayMagic = "ZHREP";

inline std::vector<std::byte> WriteReplayHeader(const ReplayHeader &header)
{
	std::vector<std::byte> bytes;
	const auto u8 = [&](std::uint32_t value) { bytes.push_back(static_cast<std::byte>(value & 0xFFu)); };
	const auto u16 = [&](std::uint32_t value) { u8(value), u8(value >> 8); };
	const auto u32 = [&](std::uint32_t value) { u16(value), u16(value >> 16); };
	const auto wide = [&](const std::u16string &text) {
		u32(static_cast<std::uint32_t>(text.size()));
		for (const char16_t c : text)
			u16(c);
	};
	const auto narrow = [&](const std::string &text) {
		u32(static_cast<std::uint32_t>(text.size()));
		for (const char c : text)
			u8(static_cast<unsigned char>(c));
	};
	for (const char c : ReplayMagic)
		u8(static_cast<unsigned char>(c));
	u32(ReplayHeader::FormatVersion);
	u32(header.startTime);
	u32(header.endTime);
	u32(header.frameCount);
	u8(header.desynced ? 1 : 0);
	u8(header.quitEarly ? 1 : 0);
	wide(header.name);
	for (const std::uint16_t field : {header.date.year, header.date.month, header.date.dayOfWeek, header.date.day, header.date.hour, header.date.minute,
			 header.date.second, header.date.milliseconds})
		u16(field);
	wide(header.version);
	narrow(header.gameOptions);
	u32(static_cast<std::uint32_t>(header.localPlayer));
	return bytes;
}

// None: not a replay of this format (another format version, the original's GENREP, a damaged file).
inline std::optional<ReplayHeader> ReadReplayHeader(std::span<const std::byte> bytes)
{
	std::size_t at = 0;
	bool ok = true;
	const auto u8 = [&]() -> std::uint32_t {
		if (at + 1 > bytes.size())
			return ok = false, 0;
		return std::to_integer<std::uint32_t>(bytes[at++]);
	};
	const auto u16 = [&]() { return u8() | (u8() << 8); };
	const auto u32 = [&]() { return u16() | (u16() << 16); };
	const auto wide = [&] {
		std::u16string text;
		const std::uint32_t length = u32();
		for (std::uint32_t index = 0; ok && index < length && index < 1024; ++index)
			text.push_back(static_cast<char16_t>(u16()));
		return text;
	};
	const auto narrow = [&] {
		std::string text;
		const std::uint32_t length = u32();
		for (std::uint32_t index = 0; ok && index < length && index < 4096; ++index)
			text.push_back(static_cast<char>(u8()));
		return text;
	};
	for (const char c : ReplayMagic)
		if (u8() != static_cast<unsigned char>(c))
			return std::nullopt;
	if (u32() != ReplayHeader::FormatVersion || !ok)
		return std::nullopt;
	ReplayHeader header;
	header.startTime = u32();
	header.endTime = u32();
	header.frameCount = u32();
	header.desynced = u8() != 0;
	header.quitEarly = u8() != 0;
	header.name = wide();
	header.date = {static_cast<std::uint16_t>(u16()), static_cast<std::uint16_t>(u16()), static_cast<std::uint16_t>(u16()), static_cast<std::uint16_t>(u16()),
		static_cast<std::uint16_t>(u16()), static_cast<std::uint16_t>(u16()), static_cast<std::uint16_t>(u16()), static_cast<std::uint16_t>(u16())};
	header.version = wide();
	header.gameOptions = narrow();
	header.localPlayer = static_cast<int>(static_cast<std::int32_t>(u32()));
	if (!ok || header.gameOptions.find("M=") == std::string::npos || header.localPlayer < -1 || header.localPlayer >= 8)
		return std::nullopt;
	header.size = at;
	return header;
}
}
