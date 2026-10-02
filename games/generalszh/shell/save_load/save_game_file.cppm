export module games.generalszh.shell.save_load.save_game_file;
import std;

export import games.generalszh.shell.save_load.save_game_info;

// A save file as this game writes it, in the original's layout (GameState::saveGame: blocks of an Xfer ASCII string
// token, a 32-bit size and the block, ended by "SG_EOF"), so the load menu lists it as it lists the original's: the
// info block "CHUNK_GameState" (version 2: the save type, the mission's map, the date, the description, the map's
// label) first, then "CHUNK_ZeroHour" holding the game itself (its format the host's; ECS-native, not the original's).
export namespace generalszh::shell
{
inline constexpr std::string_view SaveGameBlock = "CHUNK_ZeroHour";

inline std::vector<std::byte> WriteSaveGame(const SaveGameInfo &info, std::span<const std::byte> game)
{
	std::vector<std::byte> out;
	const auto u8 = [&](std::uint32_t value) { out.push_back(static_cast<std::byte>(value & 0xFF)); };
	const auto u16 = [&](std::uint32_t value) {
		u8(value);
		u8(value >> 8);
	};
	const auto u32 = [&](std::uint32_t value) {
		u16(value & 0xFFFF);
		u16(value >> 16);
	};
	const auto ascii = [&](std::string_view text) {
		const std::size_t length = std::min<std::size_t>(text.size(), 255);
		u8(static_cast<std::uint32_t>(length));
		for (std::size_t index = 0; index < length; ++index)
			u8(static_cast<unsigned char>(text[index]));
	};
	const auto block = [&](std::string_view token, auto &&write) {
		ascii(token);
		const std::size_t sizeAt = out.size();
		u32(0);
		const std::size_t start = out.size();
		write();
		const auto size = static_cast<std::uint32_t>(out.size() - start);
		for (int index = 0; index < 4; ++index)
			out[sizeAt + static_cast<std::size_t>(index)] = static_cast<std::byte>((size >> (8 * index)) & 0xFF);
	};
	block("CHUNK_GameState", [&] {
		u8(2);
		u32(static_cast<std::uint32_t>(info.type));
		ascii(info.missionMap);
		for (const std::uint16_t part : {info.date.year, info.date.month, info.date.day, info.date.dayOfWeek, info.date.hour, info.date.minute,
				 info.date.second, info.date.milliseconds})
			u16(part);
		const std::size_t length = std::min<std::size_t>(info.description.size(), 255);
		u8(static_cast<std::uint32_t>(length));
		for (std::size_t index = 0; index < length; ++index)
			u16(info.description[index]);
		ascii(info.mapLabel);
	});
	block(SaveGameBlock, [&] { out.insert(out.end(), game.begin(), game.end()); });
	ascii("SG_EOF");
	return out;
}

// The game a save holds (its "CHUNK_ZeroHour" block); none for the original's saves or a damaged file.
inline std::optional<std::vector<std::byte>> ReadSaveGame(std::span<const std::byte> bytes)
{
	std::size_t at = 0;
	while (at < bytes.size())
	{
		const std::size_t length = std::to_integer<std::size_t>(bytes[at++]);
		if (at + length > bytes.size())
			return std::nullopt;
		std::string token;
		for (std::size_t index = 0; index < length; ++index)
			token.push_back(static_cast<char>(bytes[at + index]));
		at += length;
		if (token == "SG_EOF" || at + 4 > bytes.size())
			return std::nullopt;
		std::uint32_t size = 0;
		for (int index = 0; index < 4; ++index)
			size |= std::to_integer<std::uint32_t>(bytes[at + static_cast<std::size_t>(index)]) << (8 * index);
		at += 4;
		if (at + size > bytes.size())
			return std::nullopt;
		if (token == SaveGameBlock)
			return std::vector<std::byte>(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin() + static_cast<std::ptrdiff_t>(at + size));
		at += size;
	}
	return std::nullopt;
}

// GameState::missionSave's description: "GUI:MissionSave" (UnicodeString::format) with the campaign's name for its
// first %s / %ls and the mission's number (1 on) for its first %d.
inline std::u16string MissionSaveDescription(std::u16string_view format, std::u16string_view campaign, int mission)
{
	std::u16string text(format);
	for (const std::u16string_view marker : {std::u16string_view(u"%ls"), std::u16string_view(u"%s")})
		if (const auto at = text.find(marker); at != std::u16string::npos)
		{
			text.replace(at, marker.size(), campaign);
			break;
		}
	if (const auto at = text.find(u"%d"); at != std::u16string::npos)
	{
		const std::string number = std::to_string(mission);
		text.replace(at, 2, std::u16string(number.begin(), number.end()));
	}
	return text;
}
}
