export module games.generalszh.shell.options.user_options;
import std;

export import engine.config.adapters.preferences.preferences_file;

// The player's options (the original's OptionPreferences over Options.ini, GeneralsMD OptionsMenu.cpp): volumes in
// percent, the scroll speed, gamma, mouse behaviour and the display details, read as its getters read them (with the
// original's defaults when a key is absent) and written as saveOptions writes them.
export namespace generalszh::shell
{
// The defaults the options fall back to (AudioSettings.ini, GameData.ini).
struct OptionDefaults
{
	int musicVolume{55};    // DefaultMusicVolume
	int soundVolume{80};    // DefaultSoundVolume (2D)
	int sound3DVolume{80};  // Default3DSoundVolume
	int speechVolume{70};   // DefaultSpeechVolume
	int relative2DVolume{-10}; // Relative2DVolume: below 0 lowers 2D sounds, above 0 3D ones
	int scrollFactor{50};   // GlobalData m_keyboardDefaultScrollFactor (0.5), percent
	int maxParticleCount{2500}; // GameData.ini MaxParticleCount (getParticleCap without the key)
};

// The original's StaticGameLODNames (StaticGameLODLevel: Low, Medium, High, Custom), numbered as the presentation's
// detail levels (presentation::detail_level: Low 0, Medium 1, High 2, Custom 4; the dynamic VeryHigh 3 is no static
// level the player can pick).
inline constexpr std::string_view StaticLodNames[] = {"Low", "Medium", "High", "VeryHigh", "Custom"};
inline constexpr int CustomLod = 4;
// OptionsMenuInit's detail box: GUI:High, GUI:Medium, GUI:Low, GUI:Custom (HIGHDETAIL 0 ... CUSTOMDETAIL 3), each item's
// level.
inline constexpr std::array<int, 4> DetailBoxLevels{2, 1, 0, CustomLod};
inline constexpr int CustomDetailBox = 3;

// The box item showing a level (-1: none does).
inline int DetailBoxIndex(int level) noexcept
{
	for (std::size_t index = 0; index < DetailBoxLevels.size(); ++index)
		if (DetailBoxLevels[index] == level)
			return static_cast<int>(index);
	return -1;
}

// saveOptions' split of the one sound-effects slider: both volumes at the slider's, the relative volume (a percent
// read as a Real fraction, clamped to [-1, 1]) lowering the 2D one when below 0, else the 3D one; each written as
// REAL_TO_INT of its percent (truncated, in float as the original computes it).
inline std::pair<int, int> SplitSoundSlider(int value, int relative2DVolume) noexcept
{
	float sound2D = static_cast<float>(value) / 100.0f;
	float sound3D = static_cast<float>(value) / 100.0f;
	float relative = static_cast<float>(relative2DVolume) / 100.0f;
	relative = (std::min)(1.0f, (std::max)(-1.0f, relative));
	if (relative < 0.0f)
		sound2D *= 1.0f + relative;
	else
		sound3D *= 1.0f - relative;
	return {static_cast<int>(sound2D * 100.0f), static_cast<int>(sound3D * 100.0f)};
}

// saveOptions' display gamma from the gamma slider: 1 at 50; below, darker down to 0.6 (at 0 or less); above, up to 2.
inline float DisplayGamma(int slider) noexcept
{
	float gamma = 1.0f;
	if (slider < 50)
	{
		if (slider <= 0)
			gamma = 0.6f;
		else
			gamma = 1.0f - 0.4f * static_cast<float>(50 - slider) / 50.0f;
	}
	else if (slider > 50)
		gamma = 1.0f + 1.0f * static_cast<float>(slider - 50) / 50.0f;
	return gamma;
}

struct UserOptions
{
	int musicVolume{55};
	int sound2DVolume{72};
	int sound3DVolume{80};
	int speechVolume{70};
	int scrollFactor{50};
	int gamma{50};
	bool alternateMouse{false};
	bool retaliation{true};
	bool doubleClickAttackMove{false};
	bool shadowVolumes{true};  // 3D shadows
	bool shadowDecals{true};   // 2D shadows
	bool cloudShadows{true};
	bool groundLighting{true};
	bool smoothWater{true};
	bool extraAnimations{true};
	bool dynamicLod{true};     // the check box is "no dynamic LOD"
	bool heatEffects{true};
	bool trees{true};          // "show props"
	bool buildingOcclusion{true};
	int maxParticleCount{2500};
	int textureReduction{0};
	// The detail level (StaticGameLOD, numbered as StaticLodNames) and the display resolution.
	int staticLod{2};
	int resolutionWidth{800};
	int resolutionHeight{600};
	// The addresses LAN and online games use (IPAddress, GameSpyIPAddress; empty: none chosen yet).
	std::string lanAddress;
	std::string onlineAddress;

	bool operator==(const UserOptions &) const = default;

	// The single sound-effects slider: the louder of the 2D and 3D volumes (OptionsMenuInit).
	int SoundSlider() const noexcept { return (std::max)(sound2DVolume, sound3DVolume); }

	// The slider applied (saveOptions, every accept): split into 2D and 3D by the relative volume.
	void SetSoundSlider(int value, int relative2DVolume) noexcept
	{
		std::tie(sound2DVolume, sound3DVolume) = SplitSoundSlider(value, relative2DVolume);
	}
};

namespace user_options_detail
{
// atof: the leading number (0 when there is none).
inline float LeadingReal(std::string_view text) noexcept
{
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
		text.remove_prefix(1);
	if (!text.empty() && text.front() == '+')
		text.remove_prefix(1);
	float value = 0.0f;
	std::from_chars(text.data(), text.data() + text.size(), value);
	return value;
}

// OptionPreferences' flags: "yes" in any case is true, anything else false; absent, the fallback.
inline bool YesFlag(const engine::config::Preferences &preferences, std::string_view key, bool fallback)
{
	const auto value = preferences.Find(key);
	if (!value)
		return fallback;
	return value->size() == 3 && (((*value)[0] | 0x20) == 'y') && (((*value)[1] | 0x20) == 'e') && (((*value)[2] | 0x20) == 's');
}

inline bool SameNoCase(std::string_view a, std::string_view b) noexcept
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return (x | 0x20) == (y | 0x20); });
}
}

// The original's defaults for everything (OptionPreferences' getters without their keys: getSoundVolume's 2D volume
// lowered by a negative relative volume, get3DSoundVolume's 3D one by a positive one, as Reals truncated to the slider).
inline UserOptions DefaultOptions(const OptionDefaults &defaults)
{
	UserOptions options;
	const float relative = static_cast<float>(defaults.relative2DVolume) / 100.0f;
	const float sound = static_cast<float>(defaults.soundVolume) / 100.0f, sound3D = static_cast<float>(defaults.sound3DVolume) / 100.0f;
	options.musicVolume = defaults.musicVolume;
	options.sound2DVolume = static_cast<int>(relative < 0.0f ? sound * 100.0f * (1.0f + relative) : sound * 100.0f);
	options.sound3DVolume = static_cast<int>(relative > 0.0f ? sound3D * 100.0f * (1.0f - relative) : sound3D * 100.0f);
	options.speechVolume = defaults.speechVolume;
	options.scrollFactor = defaults.scrollFactor;
	options.maxParticleCount = defaults.maxParticleCount;
	return options;
}

// setDefaults' sound slider: the louder of the two default volumes, before any relative lowering.
inline int DefaultSoundSlider(const OptionDefaults &defaults) noexcept
{
	return static_cast<int>((std::max)(static_cast<float>(defaults.soundVolume) / 100.0f, static_cast<float>(defaults.sound3DVolume) / 100.0f) * 100.0f);
}

inline UserOptions ReadUserOptions(const engine::config::Preferences &preferences, const OptionDefaults &defaults)
{
	using namespace user_options_detail;
	const UserOptions fallback = DefaultOptions(defaults);
	UserOptions options = fallback;
	// getMusicVolume / getSoundVolume / get3DSoundVolume / getSpeechVolume: atof, below 0 read as 0 (the slider shows
	// REAL_TO_INT of it).
	const auto volume = [&](std::string_view key, int otherwise) {
		const auto value = preferences.Find(key);
		return value ? static_cast<int>((std::max)(LeadingReal(*value), 0.0f)) : otherwise;
	};
	options.musicVolume = volume("MusicVolume", fallback.musicVolume);
	options.sound2DVolume = volume("SFXVolume", fallback.sound2DVolume);
	options.sound3DVolume = volume("SFX3DVolume", fallback.sound3DVolume);
	options.speechVolume = volume("VoiceVolume", fallback.speechVolume);
	// getScrollFactor: atoi clamped to [0, 100].
	if (preferences.Find("ScrollFactor"))
		options.scrollFactor = static_cast<int>(std::clamp<std::int64_t>(preferences.Number("ScrollFactor", 0), 0, 100));
	options.gamma = static_cast<int>(preferences.Number("Gamma", 50)); // getGammaValue: atoi, 50 without it
	options.alternateMouse = YesFlag(preferences, "UseAlternateMouse", fallback.alternateMouse);
	options.retaliation = YesFlag(preferences, "Retaliation", fallback.retaliation);
	options.doubleClickAttackMove = YesFlag(preferences, "UseDoubleClickAttackMove", fallback.doubleClickAttackMove);
	options.shadowVolumes = YesFlag(preferences, "UseShadowVolumes", fallback.shadowVolumes);
	options.shadowDecals = YesFlag(preferences, "UseShadowDecals", fallback.shadowDecals);
	options.cloudShadows = YesFlag(preferences, "UseCloudMap", fallback.cloudShadows);
	options.groundLighting = YesFlag(preferences, "UseLightMap", fallback.groundLighting);
	options.smoothWater = YesFlag(preferences, "ShowSoftWaterEdge", fallback.smoothWater);
	options.extraAnimations = YesFlag(preferences, "ExtraAnimations", fallback.extraAnimations);
	options.dynamicLod = YesFlag(preferences, "DynamicLOD", fallback.dynamicLod);
	options.heatEffects = YesFlag(preferences, "HeatEffects", fallback.heatEffects);
	options.trees = YesFlag(preferences, "ShowTrees", fallback.trees);
	options.buildingOcclusion = YesFlag(preferences, "BuildingOcclusion", fallback.buildingOcclusion);
	// getParticleCap: atoi, at least 100.
	if (preferences.Find("MaxParticleCount"))
		options.maxParticleCount = static_cast<int>((std::max)(preferences.Number("MaxParticleCount", 0), std::int64_t{100}));
	// getTextureReduction: atoi, at most 2 (without the key the original asks the hardware; here none).
	if (preferences.Find("TextureReduction"))
		options.textureReduction = static_cast<int>((std::min)(preferences.Number("TextureReduction", 0), std::int64_t{2}));
	// getStaticGameDetail: getStaticGameLODIndex, case blind over StaticGameLODNames (Low, Medium, High, VeryHigh,
	// Custom; an unknown name keeps the level the game has).
	if (const auto lod = preferences.Find("StaticGameLOD"))
		for (int level = 0; level <= CustomLod; ++level)
			if (SameNoCase(*lod, StaticLodNames[level]))
				options.staticLod = level;
	if (const auto resolution = preferences.Find("Resolution"))
	{
		// getResolution: "%d%d", two integers.
		int width = 0, height = 0;
		const std::string text(*resolution);
		if (std::sscanf(text.c_str(), "%d%d", &width, &height) == 2)
			options.resolutionWidth = width, options.resolutionHeight = height;
	}
	options.lanAddress = std::string(preferences.Find("IPAddress").value_or(""));
	options.onlineAddress = std::string(preferences.Find("GameSpyIPAddress").value_or(""));
	return options;
}

// What an accept changed that saveOptions writes only on a change: the static detail level
// (GameLODManager::setStaticLODLevel's levelChanged) and the display mode (setDisplayMode, from the running one).
struct OptionChanges
{
	bool level{true};
	bool resolution{true};
};

// Into the preferences as the original's saveOptions writes them (other keys kept): the custom detail settings only
// while the detail is Custom, StaticGameLOD and Resolution only when changed, the rest always.
inline void WriteUserOptions(engine::config::Preferences &preferences, const UserOptions &options, OptionChanges changed = {})
{
	if (options.staticLod == CustomLod)
	{
		preferences.Set("TextureReduction", options.textureReduction);
		preferences.SetBool("UseShadowVolumes", options.shadowVolumes);
		preferences.SetBool("UseShadowDecals", options.shadowDecals);
		preferences.SetBool("UseCloudMap", options.cloudShadows);
		preferences.SetBool("UseLightMap", options.groundLighting);
		preferences.SetBool("ShowSoftWaterEdge", options.smoothWater);
		preferences.SetBool("ExtraAnimations", options.extraAnimations);
		preferences.SetBool("DynamicLOD", options.dynamicLod);
		preferences.SetBool("HeatEffects", options.heatEffects);
		preferences.SetBool("BuildingOcclusion", options.buildingOcclusion);
		preferences.SetBool("ShowTrees", options.trees);
		preferences.Set("MaxParticleCount", options.maxParticleCount);
	}
	if (changed.level)
		preferences.Set("StaticGameLOD", StaticLodNames[std::clamp(options.staticLod, 0, CustomLod)]);
	if (changed.resolution)
		preferences.Set("Resolution", std::to_string(options.resolutionWidth) + " " + std::to_string(options.resolutionHeight));
	if (!options.lanAddress.empty())
		preferences.Set("IPAddress", options.lanAddress);
	if (!options.onlineAddress.empty())
		preferences.Set("GameSpyIPAddress", options.onlineAddress);
	preferences.SetBool("UseAlternateMouse", options.alternateMouse);
	preferences.SetBool("Retaliation", options.retaliation);
	preferences.SetBool("UseDoubleClickAttackMove", options.doubleClickAttackMove);
	preferences.Set("ScrollFactor", options.scrollFactor);
	preferences.Set("MusicVolume", options.musicVolume);
	preferences.Set("SFXVolume", options.sound2DVolume);
	preferences.Set("SFX3DVolume", options.sound3DVolume);
	preferences.Set("VoiceVolume", options.speechVolume);
	preferences.Set("Gamma", options.gamma);
}
}
