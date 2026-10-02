export module games.generalszh.presentation.audio.audio_content;
import std;

export import engine.audio.definitions.sound_event;
export import engine.config.binding.schema;
export import games.generalszh.content.loading.content_loader;

// Zero Hour's sounds: "AudioEvent", "MusicTrack" and "DialogEvent" blocks
// (Data\INI\SoundEffects, Voice, Speech, Music; each first a copy of its
// DefaultSoundEffect / DefaultMusicTrack / DefaultDialog), bound onto the
// engine's sound event definition, plus AudioSettings (folders and levels).
export namespace generalszh::presentation
{
struct AudioSettings
{
	std::string audioRoot{"Data\\Audio"};
	std::string soundsFolder{"Sounds"};
	// How many flat (2D) and world (3D) sounds may play at once (streams apart): the original's sample pools.
	std::uint32_t sampleCount2D{6};
	std::uint32_t sampleCount3D{24};
	std::string musicFolder{"Tracks"};
	std::string streamingFolder{"Speech"};
	std::string soundsExtension{"wav"};
	float minSampleVolume{0.0f};
	float relative2DVolume{0.0f};
	float defaultSoundVolume{1.0f};
	float default3DSoundVolume{1.0f};
	float defaultSpeechVolume{1.0f};
	float defaultMusicVolume{1.0f};
	float globalMinRange{0.0f};
	float globalMaxRange{0.0f};
	float microphoneHeightAboveTerrain{0.0f};
	float microphoneMaxBetweenGroundAndCamera{0.0f};
	float zoomMinDistance{0.0f};
	float zoomMaxDistance{0.0f};
	float zoomVolumeAmount{0.0f};
	// TimeToFadeAudio in whole logic frames (parseDurationUnsignedInt: milliseconds, rounded up): how long a faded
	// music track takes to fall silent (MilesAudioManager::processFadingList).
	std::uint64_t fadeAudioFrames{0};
};

struct AudioContent
{
	AudioSettings settings;
	engine::config::DefinitionTable<engine::audio::SoundEventDefinition> events;
	// Every MusicTrack name, in definition order (the jukebox's list).
	std::vector<std::string> tracks;

	const engine::audio::SoundEventDefinition *Find(std::string_view name) const { return events.Find(name); }
};

namespace detail
{
using engine::audio::SoundEventDefinition;
using engine::config::BindContext;
using engine::config::Node;
using engine::config::Schema;

float ToFloat(Engine::Math::Fixed value) noexcept { return static_cast<float>(value.Raw()) / 65536.0f; }

std::optional<float> Number(const Node &node, BindContext &context, std::size_t index = 0)
{
	const auto value = engine::config::ReadFixed(node, context, index);
	return value ? std::optional(ToFloat(*value)) : std::nullopt;
}

std::optional<float> Percent(const Node &node, BindContext &context)
{
	const auto value = engine::config::ReadPercent(node, context);
	return value ? std::optional(ToFloat(*value)) : std::nullopt;
}

constexpr std::array<std::string_view, 5> PriorityNames{"LOWEST", "LOW", "NORMAL", "HIGH", "CRITICAL"};
constexpr std::array<std::string_view, 9> TypeNames{"UI", "WORLD", "SHROUDED", "GLOBAL", "VOICE", "PLAYER", "ALLIES", "ENEMIES", "EVERYONE"};
constexpr std::array<std::string_view, 5> ControlNames{"LOOP", "RANDOM", "ALL", "POSTDELAY", "INTERRUPT"};

std::uint32_t Flags(const Node &node, BindContext &context, std::span<const std::string_view> names, std::uint32_t current)
{
	const auto flags = engine::config::ReadFlags<1>(node, context, names, {current});
	return flags ? static_cast<std::uint32_t>((*flags)[0]) : current;
}

Schema<SoundEventDefinition> SoundSchema()
{
	using S = SoundEventDefinition;
	Schema<S> schema;
	schema.String("Filename", &S::filename)
		.StringList("Sounds", &S::sounds)
		.StringList("SoundsMorning", &S::soundsMorning)
		.StringList("SoundsEvening", &S::soundsEvening)
		.StringList("SoundsNight", &S::soundsNight)
		.StringList("Attack", &S::attack)
		.StringList("Decay", &S::decay)
		.On("Volume", [](const Node &n, S &s, BindContext &c) { s.volume = Percent(n, c).value_or(s.volume); })
		.On("VolumeShift", [](const Node &n, S &s, BindContext &c) { s.volumeShift = Percent(n, c).value_or(s.volumeShift); })
		.On("MinVolume", [](const Node &n, S &s, BindContext &c) { s.minVolume = Percent(n, c).value_or(s.minVolume); })
		.On("LowPassCutoff", [](const Node &n, S &s, BindContext &c) { s.lowPassCutoff = Percent(n, c).value_or(s.lowPassCutoff); })
		.On("MinRange", [](const Node &n, S &s, BindContext &c) { s.minRange = Number(n, c).value_or(s.minRange); })
		.On("MaxRange", [](const Node &n, S &s, BindContext &c) { s.maxRange = Number(n, c).value_or(s.maxRange); })
		// "PitchShift = -5 5": percent either way.
		.On("PitchShift", [](const Node &n, S &s, BindContext &c) {
			const auto low = Number(n, c, 0);
			const auto high = Number(n, c, 1);
			if (low && high)
			{
				s.pitchMin = 1.0f + *low / 100.0f;
				s.pitchMax = 1.0f + *high / 100.0f;
			}
		})
		.On("Delay", [](const Node &n, S &s, BindContext &c) {
			const auto low = Number(n, c, 0);
			const auto high = Number(n, c, 1);
			if (low && high)
			{
				s.delayMinMs = static_cast<std::uint32_t>(*low);
				s.delayMaxMs = static_cast<std::uint32_t>(*high);
			}
		})
		.On("Limit", [](const Node &n, S &s, BindContext &c) {
			if (const auto value = engine::config::ReadInt(n, c))
				s.limit = static_cast<std::uint32_t>(*value);
		})
		.On("LoopCount", [](const Node &n, S &s, BindContext &c) {
			if (const auto value = engine::config::ReadInt(n, c))
				s.loopCount = static_cast<std::uint32_t>(*value);
		})
		.On("Priority", [](const Node &n, S &s, BindContext &c) {
			for (std::size_t index = 0; index < PriorityNames.size(); ++index)
				if (n.Value() == PriorityNames[index])
					return void(s.priority = static_cast<engine::audio::SoundPriority>(index));
			c.diagnostics.Warning(n.location, "unknown audio priority '" + std::string(n.Value()) + "'");
		})
		.On("Type", [](const Node &n, S &s, BindContext &c) { s.type = Flags(n, c, TypeNames, s.type); })
		.On("Control", [](const Node &n, S &s, BindContext &c) { s.control = Flags(n, c, ControlNames, s.control); })
		// The original's own notes; nothing to play.
		.Ignore("SubmixSlider");
	return schema;
}

Schema<AudioSettings> SettingsSchema()
{
	using A = AudioSettings;
	Schema<A> schema;
	const auto percent = [](float A::*member) {
		return [member](const Node &n, A &a, BindContext &c) { a.*member = Percent(n, c).value_or(a.*member); };
	};
	const auto number = [](float A::*member) {
		return [member](const Node &n, A &a, BindContext &c) { a.*member = Number(n, c).value_or(a.*member); };
	};
	schema.String("AudioRoot", &A::audioRoot)
		.String("SoundsFolder", &A::soundsFolder)
		.String("MusicFolder", &A::musicFolder)
		.String("StreamingFolder", &A::streamingFolder)
		.String("SoundsExtension", &A::soundsExtension)
		.On("MinSampleVolume", percent(&A::minSampleVolume))
		.On("Relative2DVolume", percent(&A::relative2DVolume))
		.On("DefaultSoundVolume", percent(&A::defaultSoundVolume))
		.On("Default3DSoundVolume", percent(&A::default3DSoundVolume))
		.On("DefaultSpeechVolume", percent(&A::defaultSpeechVolume))
		.On("DefaultMusicVolume", percent(&A::defaultMusicVolume))
		.On("GlobalMinRange", number(&A::globalMinRange))
		.On("GlobalMaxRange", number(&A::globalMaxRange))
		.On("MicrophoneDesiredHeightAboveTerrain", number(&A::microphoneHeightAboveTerrain))
		.On("MicrophoneMaxPercentageBetweenGroundAndCamera", percent(&A::microphoneMaxBetweenGroundAndCamera))
		.On("ZoomMinDistance", number(&A::zoomMinDistance))
		.On("ZoomMaxDistance", number(&A::zoomMaxDistance))
		.On("ZoomSoundVolumePercentageAmount", percent(&A::zoomVolumeAmount))
		.On("SampleCount2D", [](const Node &n, A &a, BindContext &c) { a.sampleCount2D = static_cast<std::uint32_t>(std::max(0.0f, Number(n, c).value_or(6.0f))); })
		.On("SampleCount3D", [](const Node &n, A &a, BindContext &c) { a.sampleCount3D = static_cast<std::uint32_t>(std::max(0.0f, Number(n, c).value_or(24.0f))); })
		.Duration("TimeToFadeAudio", &A::fadeAudioFrames);
	// Nothing audible to port: the Miles driver, provider and speaker set-up (AIL_quick_startup, the 3D provider and
	// speaker type), its file cache's memory budget, a stream count only ever reported (getNumStreams), a value
	// nothing reads (TimeBetweenDrawableSounds), and the SuperHackers fork's own additions (the 3D range fade, the
	// money volume) that EA's original does not have.
	for (const char *key : {"UseDigital", "UseMidi", "OutputRate", "OutputBits", "OutputChannels",
			 "StreamCount", "Preferred3DHW1", "Preferred3DHW2", "Preferred3DHW3", "Preferred3DHW4", "Preferred3DSW", "Default2DSpeakerType",
			 "Default3DSpeakerType", "Use3DSoundRangeVolumeFade", "3DSoundRangeVolumeFadeExponent", "TimeBetweenDrawableSounds",
			 "AudioFootprintInBytes", "DefaultMoneyTransactionVolume"})
		schema.Ignore(key);
	return schema;
}

struct Kind
{
	std::string_view block;
	std::string_view defaults;
	engine::audio::Bus bus;
};
constexpr std::array<Kind, 3> Kinds{{
	{"AudioEvent", "DefaultSoundEffect", engine::audio::Bus::Effects},
	{"MusicTrack", "DefaultMusicTrack", engine::audio::Bus::Music},
	{"DialogEvent", "DefaultDialog", engine::audio::Bus::Speech},
}};
}

// Binds the audio blocks of `documents` (in load order) into `content`.
void BindAudio(std::span<const engine::config::Document *const> documents, AudioContent &content, engine::config::BindContext &context)
{
	const auto sounds = detail::SoundSchema();
	const auto settings = detail::SettingsSchema();
	for (const engine::config::Document *document : documents)
		for (const engine::config::Node &root : document->Roots())
		{
			if (root.key == "AudioSettings")
			{
				settings.Bind(root, content.settings, context);
				continue;
			}
			const auto kind = std::find_if(detail::Kinds.begin(), detail::Kinds.end(), [&](const detail::Kind &k) { return k.block == root.key; });
			if (kind == detail::Kinds.end())
				continue;
			const std::string name(root.Value());
			if (name.empty())
				continue;
			engine::audio::SoundEventDefinition definition;
			if (const auto *defaults = content.events.Find(kind->defaults); defaults != nullptr && name != kind->defaults)
				definition = *defaults;
			definition.name = name;
			sounds.Bind(root, definition, context);
			definition.bus = kind->bus;
			if (kind->bus == engine::audio::Bus::Effects && !definition.Positional())
				definition.bus = engine::audio::Bus::Interface;
			if (kind->bus == engine::audio::Bus::Music && !content.events.Contains(name) && name != kind->defaults)
				content.tracks.push_back(name);
			content.events.Define(name) = std::move(definition);
		}
}

// Loads the sets the original's audio manager reads, in its order.
AudioContent LoadAudioContent(content::ContentLoader &loader)
{
	AudioContent content;
	std::vector<const engine::config::Document *> documents{&loader.Load({"Data\\INI\\AudioSettings"})};
	for (const std::string_view set : {"Music", "SoundEffects", "Speech", "Voice"})
	{
		const std::string defaults = "Data\\INI\\Default\\" + std::string(set);
		const std::string main = "Data\\INI\\" + std::string(set);
		documents.push_back(&loader.Load({defaults, main}));
	}
	for (const engine::config::Document *document : documents)
	{
		engine::config::BindContext bind{loader.DiagnosticsFor(*document), engine::time::FixedStep{30}};
		const engine::config::Document *one[] = {document};
		BindAudio(one, content, bind);
	}
	return content;
}
}
