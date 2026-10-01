export module games.generalszh.session.lockstep_session;
import std;

export import games.generalszh.session.session;
export import engine.net.lockstep.lockstep_peer;
export import engine.net.lockstep.command_recording;

// A Zero Hour session as the lockstep peer's simulation: bundles run as
// ticks, and a checkpoint from the relay replaces the session with one
// restored from it. Presentation holds on to Game() only for a frame:
// Generation() changes whenever the session was replaced.
export namespace generalszh::session
{
class LockstepSession final : public engine::net::LockstepSimulation
{
public:
	LockstepSession(const engine::level::Level &level, const content::GameContent &content, SessionOptions options) :
		m_level(level), m_content(content), m_options(std::move(options)), m_session(std::make_unique<Session>(level, content, m_options))
	{
	}
	// A session restored from `checkpoint` (a saved game); Game() is null when it does not fit.
	LockstepSession(const engine::level::Level &level, const content::GameContent &content, SessionOptions options, std::span<const std::byte> checkpoint) :
		m_level(level), m_content(content), m_options(std::move(options)), m_session(Session::Restore(level, content, m_options, checkpoint))
	{
	}
	bool Valid() const noexcept { return m_session != nullptr; }

	void Step(std::span<const engine::net::CommandEnvelope> commands) override
	{
		const std::uint64_t tick = m_session->CurrentTick();
		m_session->Tick(commands);
		if (m_recording)
		{
			engine::net::RecordTick(*m_recording, tick, commands);
			engine::net::RecordHash(*m_recording, tick, m_session->StateHash());
		}
	}
	// From now on every tick stepped is recorded (a replay's; RecorderClass::startRecording at the game's start).
	void Record() { m_recording.emplace(); }
	const engine::net::CommandRecording *Recording() const noexcept { return m_recording ? &*m_recording : nullptr; }
	std::uint64_t CurrentTick() const override { return m_session->CurrentTick(); }
	std::uint64_t StateHash() const override { return m_session->StateHash(); }
	std::vector<std::byte> SaveCheckpoint() const override { return m_session->Checkpoint(); }

	bool LoadCheckpoint(std::span<const std::byte> state) override
	{
		auto restored = Session::Restore(m_level, m_content, m_options, state);
		if (restored == nullptr)
			return false;
		m_session = std::move(restored);
		++m_generation;
		return true;
	}

	Session &Game() noexcept { return *m_session; }
	const Session &Game() const noexcept { return *m_session; }
	std::uint64_t Generation() const noexcept { return m_generation; }

private:
	const engine::level::Level &m_level;
	const content::GameContent &m_content;
	SessionOptions m_options;
	std::unique_ptr<Session> m_session;
	std::uint64_t m_generation{0};
	std::optional<engine::net::CommandRecording> m_recording;
};
}
