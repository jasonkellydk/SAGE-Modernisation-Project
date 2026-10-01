export module games.generalszh.session.setup.replay_body;
import std;

export import games.generalszh.session.setup.match_plan;
export import engine.net.lockstep.command_recording;

// What a replay file holds after its header (RecorderClass::startRecording): the difficulty, the original game mode,
// the rank points to add at the game's start and the maximum frame rate, as the original writes them; then, in place of
// the original's per-frame GameMessage stream, the game this port starts from (its match plan) and its recorded ticks.
export namespace generalszh::session::setup
{
struct ReplayBody
{
	static constexpr std::uint32_t Version = 2; // 2: the plan's scenery

	std::int32_t difficulty{1};
	std::int32_t originalGameMode{0}; // GAME_SKIRMISH / GAME_LAN
	std::int32_t rankPoints{0};
	std::int32_t maxFps{0};
	MatchPlan plan;
	engine::net::CommandRecording recording;
};

inline void WriteReplayBody(engine::core::serialization::ByteWriter &writer, const ReplayBody &body)
{
	for (const std::int32_t value : {body.difficulty, body.originalGameMode, body.rankPoints, body.maxFps})
		writer.U32(static_cast<std::uint32_t>(value));
	writer.Text("ZHREPLAY");
	writer.U32(ReplayBody::Version);
	WriteMatchPlan(writer, body.plan);
	engine::net::WriteRecording(writer, body.recording);
}

// None: not a replay of this port's (the original's GameMessage stream, another version, a damaged file).
inline std::optional<ReplayBody> ReadReplayBody(engine::core::serialization::ByteReader &reader)
{
	ReplayBody body;
	std::array<std::int32_t *, 4> fields{&body.difficulty, &body.originalGameMode, &body.rankPoints, &body.maxFps};
	for (std::int32_t *field : fields)
	{
		const auto value = reader.U32();
		if (!value)
			return std::nullopt;
		*field = static_cast<std::int32_t>(*value);
	}
	const auto magic = reader.Text();
	const auto version = reader.U32();
	if (!magic || *magic != "ZHREPLAY" || !version || *version != ReplayBody::Version)
		return std::nullopt;
	auto plan = ReadMatchPlan(reader);
	if (!plan)
		return std::nullopt;
	body.plan = std::move(*plan);
	auto recording = engine::net::ReadRecording(reader);
	if (!recording)
		return std::nullopt;
	body.recording = std::move(*recording);
	return body;
}
}
