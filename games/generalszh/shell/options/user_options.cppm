export module games.generalszh.shell.options.user_options;
import std;

export import engine.config.adapters.preferences.preferences_file;

// The player's options (the original's OptionPreferences over Options.ini):
// volumes in percent, the scroll speed, gamma, mouse behaviour and the
// display details, with the original's defaults when a key is absent.
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
};

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
	int maxParticleCount{5000};
	int textureReduction{0};
	// The detail level (StaticGameLOD: Low, Medium, High, VeryHigh, Custom) and the display resolution.
	int staticLod{2};
	int resolutionWidth{800};
	int resolutionHeight{600};
	// The addresses LAN and online games use (IPAddress, GameSpyIPAddress; empty: none chosen yet).
	std::string lanAddress;
	std::string onlineAddress;

	bool operator==(const UserOptions &) const = default;

	// The single sound-effects slider: the louder of the 2D and 3D volumes (OptionsMenuInit).
	int SoundSlider() const noexcept { return std::max(sound2DVolume, sound3DVolume); }

	// The slider applied (saveOptions): one of 2D and 3D lowered by the relative volume.
	void SetSoundSlider(int value, int relative2DVolume) noexcept
	{
		const int relative = std::clamp(relative2DVolume, -100, 100);
		sound2DVolume = relative < 0 ? value * (100 + relative) / 100 : value;
		sound3DVolume = relative > 0 ? value * (100 - relative) / 100 : value;
	}
};

// The original's defaults for everything (DefaultMusicVolume and so on; the
// 2D sounds lowered by the relative volume when it is negative).
inline UserOptions DefaultOptions(const OptionDefaults &defaults)
{
	UserOptions options;
	options.musicVolume = defaults.musicVolume;
	options.sound2DVolume = defaults.relative2DVolume < 0 ? defaults.soundVolume * (100 + defaults.relative2DVolume) / 100 : defaults.soundVolume;
	options.sound3DVolume = defaults.relative2DVolume > 0 ? defaults.sound3DVolume * (100 - defaults.relative2DVolume) / 100 : defaults.sound3DVolume;
	options.speechVolume = defaults.speechVolume;
	options.scrollFactor = defaults.scrollFactor;
	return options;
}

// The original's StaticGameLODNames, in the detail box's order.
inline constexpr std::string_view StaticLodNames[] = {"Low", "Medium", "High", "VeryHigh", "Custom"};
inline constexpr int CustomLod = 4;

inline UserOptions ReadUserOptions(const engine::config::Preferences &preferences, const OptionDefaults &defaults)
{
	const UserOptions fallback = DefaultOptions(defaults);
	UserOptions options = fallback;
	const auto volume = [&](std::string_view key, int otherwise) { return static_cast<int>(std::max<std::int64_t>(preferences.Number(key, otherwise), 0)); };
	options.musicVolume = volume("MusicVolume", fallback.musicVolume);
	options.sound2DVolume = volume("SFXVolume", fallback.sound2DVolume);
	options.sound3DVolume = volume("SFX3DVolume", fallback.sound3DVolume);
	options.speechVolume = volume("VoiceVolume", fallback.speechVolume);
	options.scrollFactor = static_cast<int>(std::max<std::int64_t>(preferences.Number("ScrollFactor", fallback.scrollFactor), 1));
	options.gamma = static_cast<int>(preferences.Number("Gamma", 50));
	options.alternateMouse = preferences.Flag("UseAlternateMouse", false);
	options.retaliation = preferences.Flag("Retaliation", true);
	options.doubleClickAttackMove = preferences.Flag("UseDoubleClickAttackMove", false);
	options.shadowVolumes = preferences.Flag("UseShadowVolumes", fallback.shadowVolumes);
	options.shadowDecals = preferences.Flag("UseShadowDecals", fallback.shadowDecals);
	options.cloudShadows = preferences.Flag("UseCloudMap", fallback.cloudShadows);
	options.groundLighting = preferences.Flag("UseLightMap", fallback.groundLighting);
	options.smoothWater = preferences.Flag("ShowSoftWaterEdge", fallback.smoothWater);
	options.extraAnimations = preferences.Flag("ExtraAnimations", fallback.extraAnimations);
	options.dynamicLod = preferences.Flag("DynamicLOD", fallback.dynamicLod);
	options.heatEffects = preferences.Flag("HeatEffects", fallback.heatEffects);
	options.trees = preferences.Flag("ShowTrees", fallback.trees);
	options.buildingOcclusion = preferences.Flag("BuildingOcclusion", fallback.buildingOcclusion);
	options.maxParticleCount = static_cast<int>(preferences.Number("MaxParticleCount", fallback.maxParticleCount));
	options.textureReduction = static_cast<int>(preferences.Number("TextureReduction", fallback.textureReduction));
	if (const auto lod = preferences.Find("StaticGameLOD"))
		for (int index = 0; index < static_cast<int>(std::size(StaticLodNames)); ++index)
			if (lod->size() == StaticLodNames[index].size() &&
				std::equal(lod->begin(), lod->end(), StaticLodNames[index].begin(), [](char a, char b) { return (a | 0x20) == (b | 0x20); }))
				options.staticLod = index; // getStaticGameLODIndex: case blind
	if (const auto resolution = preferences.Find("Resolution"))
	{
		// "%d%d": two integers.
		int width = 0, height = 0;
		const std::string text(*resolution);
		if (std::sscanf(text.c_str(), "%d%d", &width, &height) == 2 && width > 0 && height > 0)
			options.resolutionWidth = width, options.resolutionHeight = height;
	}
	options.lanAddress = std::string(preferences.Find("IPAddress").value_or(""));
	options.onlineAddress = std::string(preferences.Find("GameSpyIPAddress").value_or(""));
	return options;
}

// Into the preferences as the original's saveOptions writes them (other keys kept).
inline void WriteUserOptions(engine::config::Preferences &preferences, const UserOptions &options)
{
	preferences.Set("MusicVolume", options.musicVolume);
	preferences.Set("SFXVolume", options.sound2DVolume);
	preferences.Set("SFX3DVolume", options.sound3DVolume);
	preferences.Set("VoiceVolume", options.speechVolume);
	preferences.Set("ScrollFactor", options.scrollFactor);
	preferences.Set("Gamma", options.gamma);
	preferences.SetBool("UseAlternateMouse", options.alternateMouse);
	preferences.SetBool("Retaliation", options.retaliation);
	preferences.SetBool("UseDoubleClickAttackMove", options.doubleClickAttackMove);
	preferences.SetBool("UseShadowVolumes", options.shadowVolumes);
	preferences.SetBool("UseShadowDecals", options.shadowDecals);
	preferences.SetBool("UseCloudMap", options.cloudShadows);
	preferences.SetBool("UseLightMap", options.groundLighting);
	preferences.SetBool("ShowSoftWaterEdge", options.smoothWater);
	preferences.SetBool("ExtraAnimations", options.extraAnimations);
	preferences.SetBool("DynamicLOD", options.dynamicLod);
	preferences.SetBool("HeatEffects", options.heatEffects);
	preferences.SetBool("ShowTrees", options.trees);
	preferences.SetBool("BuildingOcclusion", options.buildingOcclusion);
	preferences.Set("MaxParticleCount", options.maxParticleCount);
	preferences.Set("TextureReduction", options.textureReduction);
	preferences.Set("StaticGameLOD", StaticLodNames[std::clamp(options.staticLod, 0, CustomLod)]);
	preferences.Set("Resolution", std::to_string(options.resolutionWidth) + " " + std::to_string(options.resolutionHeight));
	if (!options.lanAddress.empty())
		preferences.Set("IPAddress", options.lanAddress);
	if (!options.onlineAddress.empty())
		preferences.Set("GameSpyIPAddress", options.onlineAddress);
}
}
