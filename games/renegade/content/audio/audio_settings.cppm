export module games.renegade.content.audio.audio_settings;
import std;
import engine.config.adapters.ini.section_reader;
import engine.config.binding.schema;
import engine.filesystem.core.virtual_file_system;

export namespace renegade::content
{
struct DefaultAudioSettings
{
	int effects{43}, music{31}, dialog{50}, cinematic{100};
	std::array<int, 4> Volumes() const noexcept { return {effects, music, dialog, cinematic}; }
};

inline std::expected<DefaultAudioSettings, std::string> ReadDefaultAudioSettings(std::string text)
{
	using namespace engine::config;
	DefaultAudioSettings settings;
	if (text.empty()) return settings;
	const auto document = ini::ReadSections("WWAudio.ini", std::move(text));
	if (!document) return std::unexpected(document.error());
	const auto *section = ini::FindSection(*document, "Default Volume");
	if (!section) return settings;
	Schema<DefaultAudioSettings> schema;
	const auto volume = [](int DefaultAudioSettings::*member) {
		return [member](const Node &node, DefaultAudioSettings &out, BindContext &context) {
			if (const auto value = ReadInt(node, context)) {
				if (*value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max())
					context.diagnostics.Error(node.location, "default audio volume exceeds int32 range");
				else out.*member = static_cast<int>(*value);
			}
		};
	};
	schema.On("sound_volume", volume(&DefaultAudioSettings::effects)).On("music_volume", volume(&DefaultAudioSettings::music))
		.On("dialog_volume", volume(&DefaultAudioSettings::dialog)).On("cinematic_volume", volume(&DefaultAudioSettings::cinematic));
	Diagnostics diagnostics; BindContext context{diagnostics, engine::time::FixedStep{60}};
	schema.Bind(*section, settings, context);
	if (diagnostics.HasErrors()) return std::unexpected(diagnostics.Format(*document));
	settings.effects = std::clamp(settings.effects, 0, 100); settings.music = std::clamp(settings.music, 0, 100);
	settings.dialog = std::clamp(settings.dialog, 0, 100); settings.cinematic = std::clamp(settings.cinematic, 0, 100);
	return settings;
}

inline std::expected<DefaultAudioSettings, std::string> LoadDefaultAudioSettings(const engine::filesystem::VirtualFileSystem &files)
{
	const auto text = files.ReadText("WWAudio.ini");
	return ReadDefaultAudioSettings(text ? *text : std::string{});
}

// Compatibility for callers/tests of the existing menu API. Binding and
// source schema belong exclusively to the content adapter above.
inline std::expected<std::array<int, 4>, std::string> ReadAudioDefaults(std::string text)
{
	const auto settings = ReadDefaultAudioSettings(std::move(text));
	if (!settings) return std::unexpected(settings.error());
	return settings->Volumes();
}
}
