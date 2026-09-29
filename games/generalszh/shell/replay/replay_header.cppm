export module games.generalszh.shell.replay.replay_header;
import std;

// A replay file's header (the original's RecorderClass::readReplayHeader):
// "GENREP", the start and end times, the frame count, whether it desynced
// or quit early, each slot's disconnect, the replay's name (UTF-16), the
// date it was made (a SYSTEMTIME), the game's version strings and number,
// the executable's and INI's CRCs, the game options ("M=" names the map)
// and the local player's slot (-1: a single-player game).
export namespace generalszh::shell
{
struct ReplayDate
{
	std::uint16_t year{0}, month{0}, dayOfWeek{0}, day{0}, hour{0}, minute{0}, second{0}, milliseconds{0};
};

struct ReplayHeader
{
	std::uint32_t startTime{0};
	std::uint32_t endTime{0};
	std::uint32_t frameCount{0};
	bool desynced{false};
	bool quitEarly{false};
	std::u16string name;
	ReplayDate date;
	std::u16string version;
	std::u16string versionTime;
	std::uint32_t versionNumber{0};
	std::uint32_t exeCrc{0};
	std::uint32_t iniCrc{0};
	std::string gameOptions;
	int localPlayer{-1};

	// The map the game options name (M=...), its file name only (createMapName without map data).
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
		for (std::uint32_t c = u16(); ok && c != 0 && text.size() < 1023; c = u16())
			text.push_back(static_cast<char16_t>(c));
		return text;
	};
	const auto narrow = [&] {
		std::string text;
		for (std::uint32_t c = u8(); ok && c != 0; c = u8())
			text.push_back(static_cast<char>(c));
		return text;
	};
	constexpr std::string_view Magic = "GENREP";
	for (const char c : Magic)
		if (u8() != static_cast<unsigned char>(c))
			return std::nullopt;
	ReplayHeader header;
	header.startTime = u32();
	header.endTime = u32();
	header.frameCount = u32();
	header.desynced = u8() != 0;
	header.quitEarly = u8() != 0;
	for (int slot = 0; slot < 8; ++slot)
		u8(); // MAX_SLOTS disconnects
	header.name = wide();
	header.date = {static_cast<std::uint16_t>(u16()), static_cast<std::uint16_t>(u16()), static_cast<std::uint16_t>(u16()), static_cast<std::uint16_t>(u16()),
		static_cast<std::uint16_t>(u16()), static_cast<std::uint16_t>(u16()), static_cast<std::uint16_t>(u16()), static_cast<std::uint16_t>(u16())};
	header.version = wide();
	header.versionTime = wide();
	header.versionNumber = u32();
	header.exeCrc = u32();
	header.iniCrc = u32();
	header.gameOptions = narrow();
	const std::string player = narrow();
	if (!ok || header.gameOptions.find("M=") == std::string::npos)
		return std::nullopt; // ParseAsciiStringToGameInfo needs a game
	int slot = -1;
	if (!player.empty() && std::from_chars(player.data(), player.data() + player.size(), slot).ec != std::errc{})
		slot = -1;
	header.localPlayer = slot; // atoi
	if (header.localPlayer < -1 || header.localPlayer >= 8)
		return std::nullopt;
	return header;
}
}
