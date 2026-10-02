export module games.generalszh.session.setup.game_setup;
import std;

import engine.core.text.utf;

// A game's setup before it starts (the original's GameInfo and GameSlot): its
// eight slots (who plays, with which colour, faction, start spot and team),
// the map, the seed and the options. Skirmish and LAN games are set up the
// same way; the setup travels between machines, into replays and into
// Skirmish.ini as the original's options string (GameInfoToAsciiString):
//   US=1;M=03maps/tournament continent;MC=7D024EDA;MS=639753;SD=226989;C=100;
//   SR=0;SC=50000;O=N;S=HJason,0,0,TT,1,13,2,0,1:CH,-1,-1,-1,0:X:X:X:X:X:X:;
// Map names are the original's portable paths, lower case:
// "maps\<name>\<name>.map" for shipped maps, "userdata\maps\..." for the
// player's own.
export namespace generalszh::session::setup
{
inline constexpr int MaxSlots = 8;
inline constexpr int Random = -1;           // colour, faction, start spot or team: chosen at start
inline constexpr int ObserverTemplate = -2; // PLAYERTEMPLATE_OBSERVER
inline constexpr int DefaultCrcInterval = 100;
inline constexpr std::size_t MaxOptionsLength = 400; // m_lanMaxOptionsLength: 476-byte packets, less the header

// SlotState, in the original's order (the player-type boxes list them so).
enum class SlotState : std::uint8_t
{
	Open,
	Closed,
	EasyAI,
	MediumAI,
	HardAI, // SLOT_BRUTAL_AI
	Player,
};

struct GameSlot
{
	SlotState state{SlotState::Closed};
	std::u16string name; // a human's
	bool accepted{false};
	bool hasMap{true};
	int color{Random};
	int startPos{Random};
	int playerTemplate{Random};
	int team{Random};
	std::uint32_t ip{0};
	std::uint16_t port{0};
	int natBehavior{1}; // FIREWALL_TYPE_SIMPLE

	bool Human() const noexcept { return state == SlotState::Player; }
	bool AI() const noexcept { return state == SlotState::EasyAI || state == SlotState::MediumAI || state == SlotState::HardAI; }
	bool Occupied() const noexcept { return Human() || AI(); }
	bool Observer() const noexcept { return playerTemplate == ObserverTemplate; }

	// GameSlot::setState: a new occupant starts with random choices (an AI changing its
	// level keeps them); a human starts unaccepted, anything else accepted.
	void SetState(SlotState next, std::u16string playerName = {}, std::uint32_t address = 0)
	{
		if (!(AI() && (next == SlotState::EasyAI || next == SlotState::MediumAI || next == SlotState::HardAI)))
		{
			color = startPos = playerTemplate = team = Random;
		}
		if (next == SlotState::Player)
		{
			*this = GameSlot{};
			state = next;
			name = std::move(playerName);
		}
		else
		{
			state = next;
			name.clear();
			accepted = true;
			hasMap = true;
		}
		ip = address;
	}
};

struct GameSetup
{
	std::array<GameSlot, MaxSlots> slots{};
	std::string map{"NOMAP"};
	std::uint32_t mapCrc{0};
	std::uint32_t mapSize{0};
	int mapContentsMask{0};
	std::int32_t seed{0};
	int crcInterval{DefaultCrcInterval};
	bool useStats{true};
	std::uint16_t superweaponRestriction{0};
	std::uint32_t startingCash{10000}; // GlobalData m_defaultStartingCash
	bool oldFactionsOnly{false};

	int Players() const noexcept
	{
		int count = 0;
		for (const GameSlot &slot : slots)
			count += slot.Occupied() ? 1 : 0;
		return count;
	}
	bool ColorTaken(int color, int ignoredSlot) const noexcept
	{
		for (int index = 0; index < MaxSlots; ++index)
			if (index != ignoredSlot && slots[static_cast<std::size_t>(index)].color == color)
				return true;
		return false;
	}
	bool StartTaken(int position, int ignoredSlot) const noexcept
	{
		for (int index = 0; index < MaxSlots; ++index)
			if (index != ignoredSlot && slots[static_cast<std::size_t>(index)].startPos == position)
				return true;
		return false;
	}
	void ResetStartSpots() noexcept
	{
		for (GameSlot &slot : slots)
			slot.startPos = Random;
	}
	// GameInfo::setSlot: the host (slot 0) is always accepted and has the map.
	void SetSlot(int index, GameSlot slot)
	{
		if (index < 0 || index >= MaxSlots)
			return;
		if (index == 0)
			slot.accepted = slot.hasMap = true;
		slots[static_cast<std::size_t>(index)] = std::move(slot);
	}
	// GameInfo::adjustSlotsForMap: free slots open up to the map's player count, the rest close;
	// no one is removed.
	void AdjustSlotsForMap(int mapPlayers)
	{
		int taken = Players();
		for (int index = 0; index < MaxSlots; ++index)
		{
			if (slots[static_cast<std::size_t>(index)].Occupied())
				continue;
			GameSlot slot;
			if (mapPlayers > taken)
			{
				slot.SetState(SlotState::Open);
				++taken;
			}
			else
				slot.SetState(SlotState::Closed);
			SetSlot(index, slot);
		}
	}
	// GameInfo::closeOpenSlots.
	void CloseOpenSlots()
	{
		for (int index = 0; index < MaxSlots; ++index)
			if (!slots[static_cast<std::size_t>(index)].Occupied())
			{
				GameSlot slot;
				slot.SetState(SlotState::Closed);
				SetSlot(index, slot);
			}
	}
};

// The directory part of a portable map path, as the options string carries it
// ("maps\a b\a b.map" -> "maps/a b").
inline std::string MapDirectory(std::string_view map)
{
	std::string directory;
	std::size_t start = 0;
	for (;;)
	{
		const std::size_t slash = map.find_first_of("\\/", start);
		if (slash == std::string_view::npos)
			break;
		if (!directory.empty())
			directory.push_back('/');
		directory.append(map.substr(start, slash - start));
		start = slash + 1;
	}
	return directory;
}

// GameInfoToAsciiString (Zero Hour).
inline std::string ToOptionsString(const GameSetup &setup)
{
	char head[160];
	std::snprintf(head, sizeof(head), "US=%d;M=%2.2x%s;MC=%X;MS=%u;SD=%d;C=%d;SR=%u;SC=%u;O=%c;", setup.useStats ? 1 : 0, setup.mapContentsMask,
		MapDirectory(setup.map).c_str(), setup.mapCrc, setup.mapSize, setup.seed, setup.crcInterval, setup.superweaponRestriction, setup.startingCash,
		setup.oldFactionsOnly ? 'Y' : 'N');
	std::string options = head;
	options += "S=";
	for (int index = 0; index < MaxSlots; ++index)
	{
		const GameSlot &slot = setup.slots[static_cast<std::size_t>(index)];
		char buffer[96];
		if (slot.Human())
		{
			std::snprintf(buffer, sizeof(buffer), ",%X,%d,%c%c,%d,%d,%d,%d,%d:", slot.ip, slot.port, slot.accepted ? 'T' : 'F', slot.hasMap ? 'T' : 'F', slot.color,
				slot.playerTemplate, slot.startPos, slot.team, slot.natBehavior);
			// The name shares what is left of a LAN packet with the slots after it.
			const std::size_t used = std::char_traits<char>::length(buffer) + options.size() + 2;
			const std::size_t share = used < MaxOptionsLength ? (MaxOptionsLength - used) / static_cast<std::size_t>(MaxSlots - index) : 0;
			std::string name = engine::core::text::ToUtf8(slot.name);
			if (name.size() > share)
				name.resize(share);
			options += 'H' + name + buffer;
		}
		else if (slot.AI())
		{
			const char level = slot.state == SlotState::EasyAI ? 'E' : slot.state == SlotState::MediumAI ? 'M' : 'H';
			std::snprintf(buffer, sizeof(buffer), "C%c,%d,%d,%d,%d:", level, slot.color, slot.playerTemplate, slot.startPos, slot.team);
			options += buffer;
		}
		else
			options += slot.state == SlotState::Open ? "O:" : "X:";
	}
	options += ';';
	return options;
}

// What a parsed setup may name (ParseAsciiStringToGameInfo checks against the settings).
struct OptionsLimits
{
	int colors{8};
	int playerTemplates{15};
};

// ParseAsciiStringToGameInfo (Zero Hour): the setup, if the string is whole and every value
// is in range. The map comes back as its portable path ("maps\<dir>\<dir>.map").
inline std::optional<GameSetup> ParseOptionsString(std::string_view options, OptionsLimits limits = {})
{
	const auto integer = [](std::string_view text) {
		// atoi: leading digits (and sign); anything else reads as 0.
		int value = 0;
		std::size_t at = 0;
		while (at < text.size() && (text[at] == ' ' || text[at] == '\t'))
			++at;
		std::from_chars(text.data() + at, text.data() + text.size(), value);
		return value;
	};
	const auto hex = [](std::string_view text) {
		std::uint32_t value = 0;
		std::from_chars(text.data(), text.data() + text.size(), value, 16);
		return value;
	};
	const auto split = [](std::string_view text, char separator) {
		std::vector<std::string_view> parts;
		std::size_t start = 0;
		while (start <= text.size())
		{
			const std::size_t end = text.find(separator, start);
			parts.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
			if (end == std::string_view::npos)
				break;
			start = end + 1;
		}
		return parts;
	};
	GameSetup setup;
	std::array<GameSlot, MaxSlots> slots{};
	bool sawMap = false, sawCrc = false, sawSize = false, sawSeed = false, sawSlots = false, sawInterval = false;
	for (std::string_view pair : split(options, ';'))
	{
		if (pair.empty())
			continue; // strtok skips empty tokens
		const std::size_t equals = pair.find('=');
		const std::string_view key = pair.substr(0, equals);
		const std::string_view value = equals == std::string_view::npos ? std::string_view{} : pair.substr(equals + 1);
		if (value.empty())
			return std::nullopt;
		if (key == "US")
			setup.useStats = integer(value) != 0;
		else if (key == "M")
		{
			if (value.size() < 3)
				return std::nullopt;
			setup.mapContentsMask = static_cast<int>(hex(value.substr(0, 2)));
			// Each directory, then the last one again as the file: "maps/x" -> "maps\x\x.map".
			const std::vector<std::string_view> parts = split(value.substr(2), '/');
			std::string map;
			for (const std::string_view part : parts)
				map.append(part).push_back('\\');
			map.append(parts.back()).append(".map");
			for (char &c : map)
				if (c == '/')
					c = '\\';
				else if (c >= 'A' && c <= 'Z')
					c = static_cast<char>(c - 'A' + 'a');
			if (!(map.starts_with("maps\\") || map.starts_with("userdata\\maps\\") || map.starts_with("save\\")) || map.find("..") != std::string::npos)
				return std::nullopt; // portableMapPathToRealMapPath
			setup.map = std::move(map);
			sawMap = true;
		}
		else if (key == "MC")
			setup.mapCrc = hex(value), sawCrc = true;
		else if (key == "MS")
			setup.mapSize = static_cast<std::uint32_t>(integer(value)), sawSize = true;
		else if (key == "SD")
			setup.seed = integer(value), sawSeed = true;
		else if (key == "C")
			setup.crcInterval = integer(value), sawInterval = true;
		else if (key == "SR")
			setup.superweaponRestriction = static_cast<std::uint16_t>(integer(value));
		else if (key == "SC")
			setup.startingCash = static_cast<std::uint32_t>(std::strtoul(std::string(value).c_str(), nullptr, 10));
		else if (key == "O")
			setup.oldFactionsOnly = value == "Y" || value == "y";
		else if (key == "S")
		{
			sawSlots = true;
			const std::vector<std::string_view> raw = split(value, ':');
			for (int index = 0; index < MaxSlots; ++index)
			{
				const std::string_view text = static_cast<std::size_t>(index) < raw.size() ? raw[static_cast<std::size_t>(index)] : std::string_view{};
				GameSlot &slot = slots[static_cast<std::size_t>(index)];
				const std::vector<std::string_view> fields = split(text, ',');
				const auto inRange = [](int value, int low, int high) { return value >= low && value < high; };
				switch (text.empty() ? '\0' : text.front())
				{
				case 'H':
				{
					if (fields.size() < 9 || fields[0].size() < 1 || fields[3].size() != 2)
						return std::nullopt;
					slot.SetState(SlotState::Player, engine::core::text::FromUtf8(fields[0].substr(1)), hex(fields[1]));
					slot.port = static_cast<std::uint16_t>(integer(fields[2]));
					slot.accepted = fields[3][0] == 'T';
					slot.hasMap = fields[3][1] == 'T';
					slot.color = integer(fields[4]);
					slot.playerTemplate = integer(fields[5]);
					slot.startPos = integer(fields[6]);
					slot.team = integer(fields[7]);
					slot.natBehavior = integer(fields[8]);
					if (!inRange(slot.color, -1, limits.colors) || !inRange(slot.playerTemplate, ObserverTemplate, limits.playerTemplates) ||
						!inRange(slot.startPos, -1, MaxSlots) || !inRange(slot.team, -1, MaxSlots / 2) || !inRange(slot.natBehavior, 0, 32))
						return std::nullopt;
					break;
				}
				case 'C':
				{
					if (fields.size() < 5 || fields[0].size() < 2)
						return std::nullopt;
					const char level = fields[0][1];
					if (level != 'E' && level != 'M' && level != 'H')
						return std::nullopt;
					slot.SetState(level == 'E' ? SlotState::EasyAI : level == 'M' ? SlotState::MediumAI : SlotState::HardAI);
					slot.color = integer(fields[1]);
					slot.playerTemplate = integer(fields[2]);
					slot.startPos = integer(fields[3]);
					slot.team = integer(fields[4]);
					if (!inRange(slot.color, -1, limits.colors) || !inRange(slot.playerTemplate, ObserverTemplate, limits.playerTemplates) ||
						!inRange(slot.startPos, -1, MaxSlots) || !inRange(slot.team, -1, MaxSlots / 2))
						return std::nullopt;
					break;
				}
				case 'O': slot.SetState(SlotState::Open); break;
				case 'X': slot.SetState(SlotState::Closed); break;
				default: return std::nullopt;
				}
			}
		}
		else
			return std::nullopt;
	}
	if (!(sawMap && sawCrc && sawSize && sawSeed && sawSlots && sawInterval))
		return std::nullopt;
	for (int index = 0; index < MaxSlots; ++index)
		setup.SetSlot(index, slots[static_cast<std::size_t>(index)]);
	return setup;
}
}
