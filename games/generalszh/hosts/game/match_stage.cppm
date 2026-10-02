export module games.generalszh.hosts.game.match_stage;
import std;

export import engine.level.model.level;
export import engine.level.presentation.terrain_mesh;
export import engine.filesystem.core.virtual_file_system;
export import games.generalszh.content.loading.content_loader;
export import games.generalszh.hosts.game.world_scene;
export import games.generalszh.presentation.effects.weather_settings;

// What a match plays on (the host's stage): its level, the lighting of the level's time of day, the terrain mesh made
// from it and the world's renderers loaded for it. A new match (or the shell map again) replaces it whole: the old
// renderers let go of their GPU resources first, then the level, its mesh and the renderers are made anew.
export namespace generalszh::host
{
// The level's current lighting set (its time of day); none: the level has no lighting.
inline const engine::level::LightingSet *TimeOfDayOf(const engine::level::Level &level) noexcept
{
	const auto &lighting = level.lighting;
	return lighting.sets.empty() ? nullptr : &lighting.sets[lighting.current];
}

struct MatchStage
{
	std::unique_ptr<engine::level::Level> level;
	const engine::level::LightingSet *timeOfDay{nullptr};
	std::unique_ptr<engine::level::presentation::TerrainMesh> mesh;
	std::unique_ptr<WorldScene> scene;
	std::string terrainError;
	std::string waterError;
	// The map's weather: Weather.ini with the map's map.ini override (the game applies it to its snow).
	presentation::WeatherSetting weather;
	// The map's map.ini as read (the loader keeps it), for its Object overrides.
	const engine::config::Document *mapIni{nullptr};

	// The ground's height under a point of the stage's terrain.
	std::function<float(float, float)> Height() const
	{
		return [terrain = mesh.get()](float x, float y) { return terrain->HeightAt(x, y); };
	}
};

// The stage's level and terrain mesh for `level` (before any renderer exists).
inline void SetStageLevel(MatchStage &stage, engine::level::Level level)
{
	stage.level = std::make_unique<engine::level::Level>(std::move(level));
	stage.timeOfDay = TimeOfDayOf(*stage.level);
	stage.mesh = std::make_unique<engine::level::presentation::TerrainMesh>(*stage.level,
		engine::level::presentation::TerrainLightingInput{stage.timeOfDay, 3});
}

// The stage's renderers loaded for its level (terrain, water: the map's `mapPath` for its water settings). False: the
// terrain could not be loaded (its error in `terrainError`).
inline bool LoadStageScene(MatchStage &stage, const engine::filesystem::VirtualFileSystem &files, content::ContentLoader &loader, std::string_view mapPath)
{
	stage.scene = std::make_unique<WorldScene>();
	stage.weather = presentation::LoadWeatherSetting(loader, mapPath);
	// GameLogic::startNewGame: the map's map.ini (its Object overrides; none without a map).
	stage.mapIni = nullptr;
	if (!mapPath.empty())
	{
		const auto slash = mapPath.find_last_of("/\\");
		const std::string set = (slash == std::string_view::npos ? std::string{} : std::string(mapPath.substr(0, slash + 1))) + "map";
		stage.mapIni = &loader.Load({set});
	}
	return stage.scene->Load(files, loader, *stage.level, *stage.mesh, mapPath, stage.terrainError, stage.waterError);
}

// The stage replaced by `level`: the old renderers freed first, then the level, its mesh and new renderers.
inline bool RestageLevel(MatchStage &stage, engine::level::Level level, const engine::filesystem::VirtualFileSystem &files, content::ContentLoader &loader,
	std::string_view mapPath)
{
	stage.scene.reset();
	SetStageLevel(stage, std::move(level));
	return LoadStageScene(stage, files, loader, mapPath);
}
}
