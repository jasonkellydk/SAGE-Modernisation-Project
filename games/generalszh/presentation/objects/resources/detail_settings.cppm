export module games.generalszh.presentation.objects.resources.detail_settings;
import std;

import engine.ecs.system.system;
export import games.generalszh.content.loading.game_content;

// The game's detail as GameLODManager::applyStaticLODLevel leaves it in GlobalData: the presentation's settings for the
// static detail level the player chose (StaticGameLOD: Low, Medium, High, VeryHigh, Custom). A preset is GameLOD.ini's
// StaticGameLOD block of that name; its texture reduction the recommended one (here: its own: no lack of memory) and
// trees on (the memory check passed). Custom is the player's own options (GameLODManager::init's userSetDetail ==
// STATIC_GAME_LOD_CUSTOM: OptionPreferences; tree sway borrows "extra animations", as the original).
export namespace generalszh::presentation
{
// StaticGameLODLevel.
namespace detail_level
{
inline constexpr std::int32_t Low = 0;
inline constexpr std::int32_t Medium = 1;
inline constexpr std::int32_t High = 2;
inline constexpr std::int32_t VeryHigh = 3;
inline constexpr std::int32_t Custom = 4;
}

// The player's own detail (the options' Custom settings: OptionPreferences).
struct CustomDetail
{
	bool shadowVolumes{true};
	bool shadowDecals{true};
	bool cloudShadows{true};
	bool lightMap{true};
	bool softWaterEdge{true};
	bool extraAnimations{true};
	bool dynamicLod{true};
	bool heatEffects{true};
	bool trees{true};
	bool buildingOcclusion{true};
	bool fpsLimit{true};
	std::int32_t maxParticleCount{5000};
	std::int32_t textureReduction{0};
};

struct DetailSettings
{
	std::int32_t level{detail_level::High}; // getStaticLODLevel
	std::uint32_t maxParticleCount{2500};
	bool useShadowVolumes{true};
	bool useShadowDecals{true};
	bool useCloudMap{true};
	bool useLightMap{true};
	bool showSoftWaterEdge{true};
	std::uint32_t maxTankTrackEdges{100};
	std::uint32_t maxTankTrackOpaqueEdges{25};
	std::uint32_t maxTankTrackFadeDelay{300000};
	bool useDrawModuleLod{false}; // m_useDrawModuleLOD: draw modules needing a higher level are left out
	bool useTreeSway{true};
	bool useHeatEffects{true};
	std::int32_t textureReduction{0};
	bool useFpsLimit{true};
	bool enableDynamicLod{true};
	bool useTrees{true};
	bool behindBuildingMarkers{true}; // m_enableBehindBuildingMarkers (Custom only; else as it was: on)

	// Drawable's constructor: a draw module whose MinLODRequired is above the level is not made while draw module LOD
	// is on (Custom ranks above every level, so never there).
	bool DrawsModule(std::int32_t minimumLevel) const noexcept { return !useDrawModuleLod || minimumLevel <= level; }
};

// The level's index by its name (getStaticGameLODIndex: case blind); none: High.
inline std::int32_t DetailLevelNamed(std::string_view name) noexcept
{
	constexpr std::array<std::string_view, 5> names{"Low", "Medium", "High", "VeryHigh", "Custom"};
	for (std::size_t index = 0; index < names.size(); ++index)
		if (name.size() == names[index].size() &&
			std::equal(name.begin(), name.end(), names[index].begin(), [](char a, char b) { return (a | 0x20) == (b | 0x20); }))
			return static_cast<std::int32_t>(index);
	return detail_level::High;
}

inline DetailSettings ApplyDetailLevel(std::span<const content::GameContent::GameLod> presets, std::int32_t level, const CustomDetail &custom)
{
	DetailSettings detail;
	detail.level = level;
	if (level == detail_level::Custom)
	{
		detail.textureReduction = custom.textureReduction;
		detail.useShadowVolumes = custom.shadowVolumes;
		detail.useShadowDecals = custom.shadowDecals;
		detail.behindBuildingMarkers = custom.buildingOcclusion;
		detail.maxParticleCount = static_cast<std::uint32_t>(std::max(custom.maxParticleCount, 0));
		detail.enableDynamicLod = custom.dynamicLod;
		detail.useFpsLimit = custom.fpsLimit;
		detail.useLightMap = custom.lightMap;
		detail.useCloudMap = custom.cloudShadows;
		detail.showSoftWaterEdge = custom.softWaterEdge;
		detail.useHeatEffects = custom.heatEffects;
		detail.useDrawModuleLod = !custom.extraAnimations;
		detail.useTreeSway = !detail.useDrawModuleLod;
		detail.useTrees = custom.trees;
		// The track limits stay as GlobalData had them (refreshCustomStaticLODLevel copies them back): GameData's.
		return detail;
	}
	constexpr std::array<std::string_view, 4> names{"Low", "Medium", "High", "VeryHigh"};
	const content::GameContent::GameLod *preset = nullptr;
	if (level >= 0 && level < static_cast<std::int32_t>(names.size()))
		for (const auto &each : presets)
			if (each.name == names[static_cast<std::size_t>(level)])
				preset = &each;
	const content::GameContent::GameLod fallback{};
	const content::GameContent::GameLod &lod = preset != nullptr ? *preset : fallback;
	detail.maxParticleCount = lod.maxParticleCount;
	detail.useShadowVolumes = lod.useShadowVolumes;
	detail.useShadowDecals = lod.useShadowDecals;
	detail.textureReduction = lod.textureReduction;
	detail.useCloudMap = lod.useCloudMap;
	detail.useLightMap = lod.useLightMap;
	detail.showSoftWaterEdge = lod.showSoftWaterEdge;
	detail.maxTankTrackEdges = lod.maxTankTrackEdges;
	detail.maxTankTrackOpaqueEdges = lod.maxTankTrackOpaqueEdges;
	detail.maxTankTrackFadeDelay = lod.maxTankTrackFadeDelay;
	detail.useTreeSway = lod.useTreeSway;
	detail.useDrawModuleLod = !lod.useBuildupScaffolds;
	detail.useHeatEffects = lod.useHeatEffects;
	detail.enableDynamicLod = lod.enableDynamicLod;
	detail.useFpsLimit = lod.useFpsLimit;
	detail.useTrees = true;
	return detail;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::DetailSettings>
{
	static constexpr std::string_view StableName = "generalszh.presentation.detail_settings";
};
}
