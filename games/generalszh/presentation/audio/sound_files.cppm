export module games.generalszh.presentation.audio.sound_files;
import std;

export import engine.audio.playback.sound_player;
export import engine.filesystem.core.virtual_file_system;
export import games.generalszh.presentation.audio.audio_content;
import engine.audio.decoders.ffmpeg.ffmpeg_decoder;
import engine.audio.definitions.wave_length;

// Zero Hour's sound files, as the original names them: a sound list entry
// "vgenlo2a" is AudioRoot\SoundsFolder\vgenlo2a.<ext>, music files are
// under the music folder and speech files under the streaming folder; each language's own
// recording (…\<Language>\file) is preferred when there is one. Sounds
// decode once and stay cached; music and speech stream.
export namespace generalszh::presentation
{
class SoundFiles final : public engine::audio::SoundLibrary
{
public:
	SoundFiles(const engine::filesystem::VirtualFileSystem &files, const AudioSettings &settings, std::uint32_t sampleRate,
		std::string language = "English") :
		m_files(files), m_settings(settings), m_rate(sampleRate), m_language(std::move(language))
	{
	}

	std::shared_ptr<const engine::audio::PcmBuffer> Sound(std::string_view name) override
	{
		{
			const std::lock_guard lock(m_mutex);
			if (const auto found = m_cache.find(name); found != m_cache.end())
				return found->second;
		}
		std::shared_ptr<const engine::audio::PcmBuffer> decoded;
		if (auto bytes = Find(m_settings.soundsFolder, std::string(name) + "." + m_settings.soundsExtension))
			if (auto buffer = engine::audio::DecodeAll(std::move(*bytes), m_rate))
				decoded = std::make_shared<const engine::audio::PcmBuffer>(std::move(*buffer));
		const std::lock_guard lock(m_mutex);
		return m_cache.emplace(std::string(name), std::move(decoded)).first->second;
	}

	// AudioEventRTS::generateFilenamePrefix: music from MusicFolder, every other streamed file (speech) from
	// StreamingFolder.
	std::shared_ptr<engine::audio::StreamFeed> Stream(std::string_view filename, engine::audio::Bus bus) override
	{
		if (auto bytes = Find(StreamFolder(bus), std::string(filename)))
			if (auto decoder = engine::audio::AudioDecoder::Open(std::move(*bytes), m_rate))
				return std::make_shared<engine::audio::DecoderFeed>(std::move(decoder), m_rate);
		return nullptr;
	}

	// Decodes these sounds ahead (e.g. everything a map's objects may play) so
	// the first play does not wait on the decoder. Safe from worker threads.
	void Preload(const std::vector<std::string> &names)
	{
		for (const std::string &name : names)
			Sound(name);
	}

	// AudioManager::getAudioLengthMS over MSEC_PER_LOGICFRAME_REAL: the attack, main and decay files' lengths (from their
	// wave headers; a file not found or read: none), in whole logic ticks. Its first file of each (the original picks
	// among several at random; the scripted speech has one).
	std::uint64_t LengthTicks(const engine::audio::SoundEventDefinition &sound) const
	{
		std::uint64_t microseconds = 0;
		const auto add = [&](std::optional<std::vector<std::byte>> bytes) {
			if (bytes)
				if (const auto length = engine::audio::ReadWaveLength(*bytes))
					microseconds += length->Microseconds();
		};
		const auto effect = [&](const std::string &name) {
			return name.empty() ? std::nullopt : Find(m_settings.soundsFolder, name + "." + m_settings.soundsExtension);
		};
		if (!sound.attack.empty())
			add(effect(sound.attack.front()));
		if (!sound.filename.empty())
			add(Find(StreamFolder(sound.bus), sound.filename));
		else if (!sound.sounds.empty())
			add(effect(sound.sounds.front()));
		if (!sound.decay.empty())
			add(effect(sound.decay.front()));
		return microseconds * 30 / 1'000'000;
	}

	std::size_t CachedSounds() const
	{
		const std::lock_guard lock(m_mutex);
		return m_cache.size();
	}

private:
	const std::string &StreamFolder(engine::audio::Bus bus) const
	{
		return bus == engine::audio::Bus::Music ? m_settings.musicFolder : m_settings.streamingFolder;
	}

	std::optional<std::vector<std::byte>> Find(const std::string &folder, const std::string &file) const
	{
		const std::string base = m_settings.audioRoot + "\\" + folder + "\\";
		if (auto localized = m_files.Read(base + m_language + "\\" + file))
			return localized;
		return m_files.Read(base + file);
	}

	const engine::filesystem::VirtualFileSystem &m_files;
	const AudioSettings &m_settings;
	std::uint32_t m_rate;
	std::string m_language;
	mutable std::mutex m_mutex;
	std::map<std::string, std::shared_ptr<const engine::audio::PcmBuffer>, std::less<>> m_cache;
};
}
