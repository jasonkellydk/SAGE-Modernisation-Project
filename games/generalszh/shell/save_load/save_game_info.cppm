export module games.generalszh.shell.save_load.save_game_info;
import std;

// A save file's game info (the original's GameState::getSaveGameInfoFromFile):
// the file is a run of blocks, each an Xfer ASCII string token (a length byte
// and its characters) and a 32-bit size, ended by "SG_EOF". The block
// "CHUNK_GameState" holds the info: a version byte, then (version 2 on) the
// save type and the mission's map, the date saved (year, month, day, day of
// week, hour, minute, second, milliseconds), the player's description (a
// length byte and UTF-16 characters) and the map's label.
export namespace generalszh::shell
{
enum class SaveFileType : std::uint32_t
{
	Normal,  // SAVE_FILE_TYPE_NORMAL: saved at any point in a game
	Mission, // SAVE_FILE_TYPE_MISSION: saved between missions
};

struct SaveDate
{
	std::uint16_t year{0}, month{0}, day{0}, dayOfWeek{0}, hour{0}, minute{0}, second{0}, milliseconds{0};

	// SaveDate::isNewerThan: year down to the millisecond (not the day of the week).
	bool NewerThan(const SaveDate &other) const noexcept
	{
		return std::tie(year, month, day, hour, minute, second, milliseconds) >
			std::tie(other.year, other.month, other.day, other.hour, other.minute, other.second, other.milliseconds);
	}
};

struct SaveGameInfo
{
	SaveFileType type{SaveFileType::Normal};
	std::string missionMap;
	SaveDate date;
	std::u16string description;
	std::string mapLabel;
};

inline constexpr std::string_view SaveGameExtension = ".sav";

// The info of the save in `bytes` (its start is enough: the info block comes first), if readable.
inline std::optional<SaveGameInfo> ReadSaveGameInfo(std::span<const std::byte> bytes)
{
	std::size_t at = 0;
	bool ok = true;
	const auto u8 = [&]() -> std::uint32_t {
		if (at >= bytes.size())
			return ok = false, 0;
		return std::to_integer<std::uint32_t>(bytes[at++]);
	};
	const auto u16 = [&]() { return u8() | (u8() << 8); };
	const auto u32 = [&]() { return u16() | (u16() << 16); };
	const auto ascii = [&] {
		std::string text;
		for (std::uint32_t length = u8(); ok && length > 0; --length)
			text.push_back(static_cast<char>(u8()));
		return text;
	};
	const auto unicode = [&] {
		std::u16string text;
		for (std::uint32_t length = u8(); ok && length > 0; --length)
			text.push_back(static_cast<char16_t>(u16()));
		return text;
	};
	const auto caseBlind = [](std::string_view a, std::string_view b) {
		if (a.size() != b.size())
			return false;
		for (std::size_t index = 0; index < a.size(); ++index)
			if ((a[index] | 0x20) != (b[index] | 0x20))
				return false;
		return true;
	};
	while (ok)
	{
		const std::string token = ascii();
		if (!ok || token.empty() || caseBlind(token, "SG_EOF"))
			return std::nullopt; // no game info in the file
		const std::uint32_t size = u32();
		if (!caseBlind(token, "CHUNK_GameState"))
		{
			at += size; // a block the menu does not need
			continue;
		}
		SaveGameInfo info;
		const std::uint32_t version = u8();
		if (version >= 2)
		{
			info.type = static_cast<SaveFileType>(u32());
			info.missionMap = ascii();
		}
		info.date.year = static_cast<std::uint16_t>(u16());
		info.date.month = static_cast<std::uint16_t>(u16());
		info.date.day = static_cast<std::uint16_t>(u16());
		info.date.dayOfWeek = static_cast<std::uint16_t>(u16());
		info.date.hour = static_cast<std::uint16_t>(u16());
		info.date.minute = static_cast<std::uint16_t>(u16());
		info.date.second = static_cast<std::uint16_t>(u16());
		info.date.milliseconds = static_cast<std::uint16_t>(u16());
		info.description = unicode();
		info.mapLabel = ascii();
		if (!ok)
			return std::nullopt;
		return info;
	}
	return std::nullopt;
}
}
