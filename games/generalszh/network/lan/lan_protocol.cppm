export module games.generalszh.network.lan.lan_protocol;
import std;

// The LAN lobby's datagrams as the original writes them (LANAPI.h's packed
// LANMessage, Transport.cpp's framing), so the lobby behaves as the original's
// and sees original games: UDP port 8086; a 6-byte header (a CRC of the rest
// and the magic 0xF00D) before the 471-byte message; the whole 477 bytes
// obfuscated in 4-byte words. Unused bytes are zero (the original leaves them
// as they were on its stack).
export namespace generalszh::network::lan
{
inline constexpr std::uint16_t LobbyPort = 8086;
inline constexpr std::uint16_t GamePort = 8088; // NETWORK_BASE_PORT_NUMBER, written into every human slot
inline constexpr std::size_t MessageSize = 471;
inline constexpr std::size_t HeaderSize = 6;
inline constexpr std::size_t DatagramSize = HeaderSize + MessageSize;
inline constexpr std::uint16_t Magic = 0xF00D;
inline constexpr std::size_t PlayerNameLength = 12, GameNameLength = 16, ChatLength = 100, OptionsLength = 400;

enum class MessageType : std::uint32_t
{
	RequestLocations,
	GameAnnounce,
	LobbyAnnounce,
	RequestJoin,
	JoinAccept,
	JoinDeny,
	RequestGameLeave,
	RequestLobbyLeave,
	SetAccept,
	MapAvailability,
	Chat,
	GameStart,
	GameStartTimer,
	GameOptions,
	Inactive,
	RequestGameInfo,
};

// LANAPIInterface::ReturnType.
enum class Result : std::uint32_t
{
	Ok,
	Timeout,
	GameFull,
	DuplicateName,
	CrcMismatch,
	SerialDupe,
	GameStarted,
	GameExists,
	GameGone,
	Busy,
	Unknown,
};

enum class ChatType : std::uint32_t
{
	Normal,
	Emote,
	System,
};

// One message; which fields travel depends on its type (the original's union).
struct Message
{
	MessageType type{MessageType::RequestLocations};
	std::u16string name; // the sender's
	std::string userName, hostName;
	// GameAnnounce, JoinAccept/Deny, SetAccept, MapAvailability, Chat, RequestGameLeave
	std::u16string gameName;
	// GameAnnounce
	bool inProgress{false};
	std::string options; // also GameOptions
	bool directConnect{false};
	// RequestJoin, JoinAccept/Deny
	std::uint32_t gameIp{0};
	std::uint32_t exeCrc{0}, iniCrc{0};
	std::uint32_t playerIp{0};
	std::int32_t slot{0};
	Result reason{Result::Ok};
	// SetAccept
	bool accepted{false};
	// MapAvailability
	std::uint32_t mapCrc{0};
	bool hasMap{false};
	// Chat
	ChatType chatType{ChatType::Normal};
	std::u16string chat;
	// GameStartTimer
	std::int32_t seconds{0};
	// RequestGameInfo
	std::uint32_t ip{0};
	std::u16string playerName;
};

// CRC::computeCRC: each byte added to the value shifted left, its top bit carried round.
inline std::uint32_t Crc(std::span<const std::byte> bytes, std::uint32_t crc = 0) noexcept
{
	for (const std::byte b : bytes)
	{
		const std::uint32_t high = crc >> 31;
		crc = (crc << 1) + std::to_integer<std::uint32_t>(b) + high;
	}
	return crc;
}
inline std::uint32_t Crc(std::string_view text) noexcept { return Crc(std::as_bytes(std::span(text.data(), text.size()))); }

namespace detail
{
struct Writer
{
	std::array<std::byte, MessageSize> bytes{};
	void U32(std::size_t at, std::uint32_t value)
	{
		for (int b = 0; b < 4; ++b)
			bytes[at + static_cast<std::size_t>(b)] = static_cast<std::byte>((value >> (8 * b)) & 0xFF);
	}
	void Flag(std::size_t at, bool value) { bytes[at] = static_cast<std::byte>(value ? 1 : 0); }
	// A wide string of `count` characters, the last kept for the terminator.
	void Wide(std::size_t at, std::u16string_view text, std::size_t count)
	{
		for (std::size_t c = 0; c + 1 < count && c < text.size(); ++c)
		{
			bytes[at + 2 * c] = static_cast<std::byte>(text[c] & 0xFF);
			bytes[at + 2 * c + 1] = static_cast<std::byte>(text[c] >> 8);
		}
	}
	void Narrow(std::size_t at, std::string_view text, std::size_t count)
	{
		for (std::size_t c = 0; c + 1 < count && c < text.size(); ++c)
			bytes[at + c] = static_cast<std::byte>(text[c]);
	}
};

struct Reader
{
	std::span<const std::byte> bytes;
	std::uint32_t U32(std::size_t at) const
	{
		std::uint32_t value = 0;
		for (int b = 3; b >= 0; --b)
			value = (value << 8) | std::to_integer<std::uint32_t>(bytes[at + static_cast<std::size_t>(b)]);
		return value;
	}
	bool Flag(std::size_t at) const { return bytes[at] != std::byte{0}; }
	std::u16string Wide(std::size_t at, std::size_t count) const
	{
		std::u16string text;
		for (std::size_t c = 0; c < count; ++c)
		{
			const auto ch = static_cast<char16_t>(std::to_integer<unsigned>(bytes[at + 2 * c]) | (std::to_integer<unsigned>(bytes[at + 2 * c + 1]) << 8));
			if (ch == 0)
				break;
			text.push_back(ch);
		}
		return text;
	}
	std::string Narrow(std::size_t at, std::size_t count) const
	{
		std::string text;
		for (std::size_t c = 0; c < count && bytes[at + c] != std::byte{0}; ++c)
			text.push_back(static_cast<char>(bytes[at + c]));
		return text;
	}
};

// Offsets in the packed message (LANAPI.h): the header, then the union at 34.
inline constexpr std::size_t Union = 34;
inline constexpr std::size_t NameChars = PlayerNameLength + 1, GameNameChars = GameNameLength + 1, ChatChars = ChatLength + 1, OptionsChars = OptionsLength + 1;
}

inline std::array<std::byte, MessageSize> Encode(const Message &message)
{
	using namespace detail;
	Writer out;
	out.U32(0, static_cast<std::uint32_t>(message.type));
	out.Wide(4, message.name, NameChars);
	out.Narrow(30, message.userName, 2);
	out.Narrow(32, message.hostName, 2);
	switch (message.type)
	{
	case MessageType::GameAnnounce:
		out.Wide(Union, message.gameName, GameNameChars);
		out.Flag(Union + 34, message.inProgress);
		out.Narrow(Union + 35, message.options, OptionsChars);
		out.Flag(Union + 436, message.directConnect);
		break;
	case MessageType::RequestJoin:
		out.U32(Union, message.gameIp);
		out.U32(Union + 4, message.exeCrc);
		out.U32(Union + 8, message.iniCrc);
		break; // the serial (23 characters at +12) travels empty
	case MessageType::JoinAccept:
	case MessageType::JoinDeny:
		out.Wide(Union, message.gameName, GameNameChars);
		out.U32(Union + 34, message.gameIp);
		out.U32(Union + 38, message.playerIp);
		out.U32(Union + 42, message.type == MessageType::JoinAccept ? static_cast<std::uint32_t>(message.slot) : static_cast<std::uint32_t>(message.reason));
		break;
	case MessageType::RequestGameLeave:
		// The original writes PlayerInfo.playerName (at +4) and reads GameToLeave.gameName (at +0), so a lobby never
		// saw a game go until it timed out; the game's name goes where it is read.
		out.Wide(Union, message.gameName, GameNameChars);
		break;
	case MessageType::SetAccept:
		out.Wide(Union, message.gameName, GameNameChars);
		out.Flag(Union + 34, message.accepted);
		break;
	case MessageType::MapAvailability:
		out.Wide(Union, message.gameName, GameNameChars);
		out.U32(Union + 34, message.mapCrc);
		out.Flag(Union + 38, message.hasMap);
		break;
	case MessageType::Chat:
		out.Wide(Union, message.gameName, GameNameChars);
		out.U32(Union + 34, static_cast<std::uint32_t>(message.chatType));
		out.Wide(Union + 38, message.chat, ChatChars);
		break;
	case MessageType::GameStartTimer:
		out.U32(Union, static_cast<std::uint32_t>(message.seconds));
		break;
	case MessageType::GameOptions:
		out.Narrow(Union, message.options, OptionsChars);
		break;
	case MessageType::RequestGameInfo:
		out.U32(Union, message.ip);
		out.Wide(Union + 4, message.playerName, NameChars);
		break;
	default:
		break; // RequestLocations, LobbyAnnounce, RequestLobbyLeave, GameStart, Inactive carry nothing more
	}
	return out.bytes;
}

inline std::optional<Message> Decode(std::span<const std::byte> bytes)
{
	using namespace detail;
	if (bytes.size() < MessageSize)
		return std::nullopt;
	const Reader in{bytes};
	Message message;
	const std::uint32_t type = in.U32(0);
	if (type > static_cast<std::uint32_t>(MessageType::RequestGameInfo))
		return std::nullopt;
	message.type = static_cast<MessageType>(type);
	message.name = in.Wide(4, NameChars);
	message.userName = in.Narrow(30, 2);
	message.hostName = in.Narrow(32, 2);
	switch (message.type)
	{
	case MessageType::GameAnnounce:
		message.gameName = in.Wide(Union, GameNameChars);
		message.inProgress = in.Flag(Union + 34);
		message.options = in.Narrow(Union + 35, OptionsChars);
		message.directConnect = in.Flag(Union + 436);
		break;
	case MessageType::RequestJoin:
		message.gameIp = in.U32(Union);
		message.exeCrc = in.U32(Union + 4);
		message.iniCrc = in.U32(Union + 8);
		break;
	case MessageType::JoinAccept:
	case MessageType::JoinDeny:
		message.gameName = in.Wide(Union, GameNameChars);
		message.gameIp = in.U32(Union + 34);
		message.playerIp = in.U32(Union + 38);
		if (message.type == MessageType::JoinAccept)
			message.slot = static_cast<std::int32_t>(in.U32(Union + 42));
		else
			message.reason = static_cast<Result>(in.U32(Union + 42));
		break;
	case MessageType::RequestGameLeave:
		message.gameName = in.Wide(Union, GameNameChars);
		break;
	case MessageType::SetAccept:
		message.gameName = in.Wide(Union, GameNameChars);
		message.accepted = in.Flag(Union + 34);
		break;
	case MessageType::MapAvailability:
		message.gameName = in.Wide(Union, GameNameChars);
		message.mapCrc = in.U32(Union + 34);
		message.hasMap = in.Flag(Union + 38);
		break;
	case MessageType::Chat:
		message.gameName = in.Wide(Union, GameNameChars);
		message.chatType = static_cast<ChatType>(in.U32(Union + 34));
		message.chat = in.Wide(Union + 38, ChatChars);
		break;
	case MessageType::GameStartTimer:
		message.seconds = static_cast<std::int32_t>(in.U32(Union));
		break;
	case MessageType::GameOptions:
		message.options = in.Narrow(Union, OptionsChars);
		break;
	case MessageType::RequestGameInfo:
		message.ip = in.U32(Union);
		message.playerName = in.Wide(Union + 4, NameChars);
		break;
	default:
		break;
	}
	return message;
}

namespace detail
{
// Transport.cpp's obfuscation: each whole 4-byte word xored with a mask that starts at 0xFADE and
// grows by 0x321 a word, stored byte-swapped; a last partial word stays as it is.
inline void Obfuscate(std::span<std::byte> bytes, bool encrypt)
{
	std::uint32_t mask = 0x0000FADE;
	for (std::size_t at = 0; at + 4 <= bytes.size(); at += 4, mask += 0x321)
	{
		std::uint32_t word = 0;
		if (encrypt)
		{
			for (int b = 3; b >= 0; --b)
				word = (word << 8) | std::to_integer<std::uint32_t>(bytes[at + static_cast<std::size_t>(b)]); // little-endian
			word ^= mask;
			for (int b = 0; b < 4; ++b)
				bytes[at + static_cast<std::size_t>(b)] = static_cast<std::byte>((word >> (8 * (3 - b))) & 0xFF); // stored swapped
		}
		else
		{
			for (int b = 0; b < 4; ++b)
				word = (word << 8) | std::to_integer<std::uint32_t>(bytes[at + static_cast<std::size_t>(b)]); // read swapped
			word ^= mask;
			for (int b = 0; b < 4; ++b)
				bytes[at + static_cast<std::size_t>(b)] = static_cast<std::byte>((word >> (8 * b)) & 0xFF);
		}
	}
}
}

// A message as a datagram: the CRC (of the magic and the message), the magic, the message; obfuscated.
inline std::vector<std::byte> Frame(const Message &message)
{
	const auto payload = Encode(message);
	std::vector<std::byte> datagram(DatagramSize);
	datagram[4] = static_cast<std::byte>(Magic & 0xFF);
	datagram[5] = static_cast<std::byte>(Magic >> 8);
	for (std::size_t at = 0; at < payload.size(); ++at)
		datagram[HeaderSize + at] = payload[at];
	const std::uint32_t crc = Crc(std::span(datagram).subspan(4));
	for (int b = 0; b < 4; ++b)
		datagram[static_cast<std::size_t>(b)] = static_cast<std::byte>((crc >> (8 * b)) & 0xFF);
	detail::Obfuscate(datagram, true);
	return datagram;
}

// A datagram's message, if it is whole: its CRC and magic check out.
inline std::optional<Message> Unframe(std::span<const std::byte> received)
{
	if (received.size() <= HeaderSize)
		return std::nullopt;
	std::vector<std::byte> datagram(received.begin(), received.end());
	detail::Obfuscate(datagram, false);
	const detail::Reader in{datagram};
	const std::uint32_t crc = in.U32(0);
	const std::uint32_t magic = std::to_integer<std::uint32_t>(datagram[4]) | (std::to_integer<std::uint32_t>(datagram[5]) << 8);
	if (magic != Magic || crc != Crc(std::span(datagram).subspan(4)))
		return std::nullopt;
	std::vector<std::byte> payload(datagram.begin() + HeaderSize, datagram.end());
	payload.resize(std::max(payload.size(), MessageSize));
	return Decode(payload);
}
}
