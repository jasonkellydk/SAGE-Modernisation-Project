module;
#include <cstdio>
#include <ctime>

export module games.generalszh.hosts.game.match_files;
import std;

export import engine.config.adapters.preferences.preferences_file;
export import engine.core.serialization.byte_stream;
export import engine.net.lockstep.command_recording;
export import games.generalszh.session.setup.match_plan;
export import games.generalszh.shell.save_load.save_game_info;
export import games.generalszh.session.setup.replay_body;
import games.generalszh.shell.save_load.save_game_file;
import games.generalszh.shell.replay.replay_header;
import games.generalszh.shell.replay.replay_menu_view_model;

// The files a match leaves in the player's data folder: saved games (GameState::saveGame, missionSave), the last replay
// (RecorderClass), and SkirmishStats.ini (SkirmishBattleHonors). This port's own formats (checkpoints and command logs:
// no compatibility with the original's .sav and .rep).
export namespace generalszh::host
{
// A saved game's own block: this version, the match's plan and its checkpoint.
inline constexpr std::uint32_t SaveGameVersion = 3; // 2: the plan's campaign rank points; 3: its scenery

// A file's bytes (none: it could not be read).
inline std::vector<std::byte> ReadFileBytes(const std::filesystem::path &file)
{
	std::ifstream in(file, std::ios::binary);
	std::vector<std::byte> bytes;
	for (char c; in.get(c);)
		bytes.push_back(static_cast<std::byte>(c));
	return bytes;
}

// SkirmishStats.ini: read, changed and written back at once, as the score screen does.
inline void UpdateSkirmishStats(const std::filesystem::path &userData, const std::function<void(engine::config::Preferences &)> &change)
{
	const std::filesystem::path file = userData / "SkirmishStats.ini";
	std::string text;
	if (std::ifstream in{file, std::ios::binary})
		text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	engine::config::Preferences stats = engine::config::Preferences::Parse(text);
	change(stats);
	std::error_code error;
	std::filesystem::create_directories(userData, error);
	std::ofstream(file, std::ios::binary) << stats.Write();
}

// The date now, as a save records it (SYSTEMTIME fields).
inline shell::SaveDate SaveDateNow()
{
	const std::time_t now = std::time(nullptr);
	std::tm local{};
	localtime_s(&local, &now);
	return shell::SaveDate{static_cast<std::uint16_t>(local.tm_year + 1900), static_cast<std::uint16_t>(local.tm_mon + 1), static_cast<std::uint16_t>(local.tm_mday),
		static_cast<std::uint16_t>(local.tm_wday), static_cast<std::uint16_t>(local.tm_hour), static_cast<std::uint16_t>(local.tm_min),
		static_cast<std::uint16_t>(local.tm_sec), 0};
}

// A save file written in the save folder: over `overwrite`, else under the first free number (findNextSaveFilename:
// "00000000.sav" on). The file; none when it could not be written.
inline std::optional<std::filesystem::path> WriteSaveFile(const std::filesystem::path &folder, const shell::SaveGameInfo &info, std::span<const std::byte> body,
	const std::optional<std::string> &overwrite)
{
	const std::vector<std::byte> file = shell::WriteSaveGame(info, body);
	std::error_code error;
	std::filesystem::create_directories(folder, error);
	if (overwrite)
	{
		std::ofstream out(folder / *overwrite, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
		return out ? std::optional(folder / *overwrite) : std::nullopt;
	}
	for (int number = 0; number < 100000000; ++number)
	{
		char name[16];
		std::snprintf(name, sizeof(name), "%08d.sav", number);
		const std::filesystem::path target = folder / name;
		if (std::filesystem::exists(target, error))
			continue;
		std::ofstream out(target, std::ios::binary);
		out.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
		if (!out)
			return std::nullopt;
		return target;
	}
	return std::nullopt;
}

// A saved game's block: the version, the plan, the checkpoint (empty for a mission save), then what `client` writes
// (the client's view of the game).
inline std::vector<std::byte> SavedMatchBody(const session::setup::MatchPlan &plan, std::span<const std::byte> checkpoint,
	const std::function<void(engine::core::serialization::ByteWriter &)> &client = {})
{
	engine::core::serialization::ByteWriter writer;
	writer.U32(SaveGameVersion);
	session::setup::WriteMatchPlan(writer, plan);
	writer.Blob(checkpoint);
	if (client)
		client(writer);
	return writer.Take();
}

// A saved game read back: its plan and checkpoint, the rest left in `reader` (the client's view). None: not a saved game
// of this version.
struct SavedMatch
{
	session::setup::MatchPlan plan;
	std::vector<std::byte> checkpoint;
};

inline std::optional<SavedMatch> ReadSavedMatch(engine::core::serialization::ByteReader &reader)
{
	const auto version = reader.U32();
	auto plan = version && *version == SaveGameVersion ? session::setup::ReadMatchPlan(reader) : std::nullopt;
	auto checkpoint = plan ? reader.Blob() : std::nullopt;
	if (!checkpoint)
		return std::nullopt;
	return SavedMatch{std::move(*plan), std::vector<std::byte>(checkpoint->begin(), checkpoint->end())};
}

// RecorderClass::startRecording / stopRecording: the game just played as the Last Replay ("00000000.rep" in the Replays
// folder, written over each time): this port's own header (its start and end times, frames, name, date, version, game
// options and local slot: no compatibility with GENREP), the difficulty, game mode, rank points and frame rate, then the
// plan it started from and its recorded ticks.
inline void WriteLastReplay(const std::filesystem::path &userData, const session::setup::MatchPlan &plan, const engine::net::CommandRecording &recording,
	std::uint32_t startTime, std::u16string name)
{
	shell::ReplayHeader header;
	header.startTime = startTime;
	header.endTime = static_cast<std::uint32_t>(std::time(nullptr));
	header.frameCount = static_cast<std::uint32_t>(recording.endTick);
	header.name = std::move(name);
	const std::time_t now = std::time(nullptr);
	std::tm local{};
	localtime_s(&local, &now);
	header.date = {static_cast<std::uint16_t>(local.tm_year + 1900), static_cast<std::uint16_t>(local.tm_mon + 1), static_cast<std::uint16_t>(local.tm_wday),
		static_cast<std::uint16_t>(local.tm_mday), static_cast<std::uint16_t>(local.tm_hour), static_cast<std::uint16_t>(local.tm_min),
		static_cast<std::uint16_t>(local.tm_sec), 0};
	header.version = u"Version 1.04";
	header.gameOptions = session::setup::ToOptionsString(plan.setup);
	header.localPlayer = plan.localSlot;
	session::setup::ReplayBody body;
	body.difficulty = 1;
	body.originalGameMode = plan.kind == session::setup::MatchKind::Lan ? 1 : 2; // GAME_LAN, GAME_SKIRMISH
	body.plan = plan;
	body.recording = recording;
	engine::core::serialization::ByteWriter writer;
	session::setup::WriteReplayBody(writer, body);
	std::vector<std::byte> file = shell::WriteReplayHeader(header);
	const std::vector<std::byte> rest = writer.Take();
	file.insert(file.end(), rest.begin(), rest.end());
	const std::filesystem::path folder = userData / "Replays";
	std::error_code error;
	std::filesystem::create_directories(folder, error);
	std::ofstream out(folder / shell::LastReplayFile, std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
	if (!out)
		std::fprintf(stderr, "replay: the last replay could not be written\n");
}

// A replay file read back (RecorderClass::playbackFile): its plan and recorded ticks. None: not one of this port's.
inline std::optional<session::setup::ReplayBody> ReadReplayFile(const std::filesystem::path &file)
{
	const std::vector<std::byte> bytes = ReadFileBytes(file);
	const auto header = shell::ReadReplayHeader(bytes);
	engine::core::serialization::ByteReader reader(header ? std::span<const std::byte>(bytes).subspan(header->size) : std::span<const std::byte>{});
	return header ? session::setup::ReadReplayBody(reader) : std::nullopt;
}
}
