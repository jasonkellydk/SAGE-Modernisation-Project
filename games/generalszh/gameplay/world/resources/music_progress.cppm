export module games.generalszh.gameplay.world.resources.music_progress;
import std;

export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// How far the music has played, as the presentation reported it (a MusicProgress command): the track playing and how
// often it has played through since it was set (AudioManager::hasMusicTrackCompleted, which a campaign mission's
// MUSIC_TRACK_HAS_COMPLETED asks). The audio itself runs on real time, so the simulation only learns of it through the
// command log, keeping it deterministic on replay. Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct MusicProgress
{
	std::string track;
	std::uint32_t completions{0};

	[[nodiscard]] bool Completed(std::string_view name, std::int64_t times) const noexcept
	{
		return track == name && static_cast<std::int64_t>(completions) >= times;
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.Text(track);
		writer.U32(completions);
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		auto name = reader.Text();
		const auto count = reader.U32();
		if (!name || !count)
			return false;
		track = std::move(*name);
		completions = *count;
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::MusicProgress>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.music_progress";
};
}
