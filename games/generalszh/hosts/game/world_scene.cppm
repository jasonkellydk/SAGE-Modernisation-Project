export module games.generalszh.hosts.game.world_scene;
import std;

import engine.level.model.level;
import engine.level.presentation.terrain_mesh;
export import engine.level.presentation.radar_terrain;
import engine.filesystem.core.virtual_file_system;
import games.generalszh.content.loading.content_loader;
import games.generalszh.hosts.game.game_client;
import Graphics.Scene.Views.CameraState;

// The world's renderers on the shared frame device: terrain, the objects
// presentation draws up each frame, water, and the particles over them. The
// interface stays light (the renderers' imports live in the implementation)
// so the host's main file stays small.
export namespace generalszh::host
{
class WorldScene
{
public:
	WorldScene();
	~WorldScene();
	WorldScene(const WorldScene &) = delete;
	WorldScene &operator=(const WorldScene &) = delete;

	// Terrain and water for the level (water errors are reported, not fatal).
	bool Load(const engine::filesystem::VirtualFileSystem &files, content::ContentLoader &loader, const engine::level::Level &level,
		const engine::level::presentation::TerrainMesh &mesh, std::string_view mapName, std::string &error, std::string &waterError);
	// Starts loading the models of what the game shows now.
	void Preload(GameClient &game);
	// The world as the camera sees it; `waterSeconds` moves the water.
	void Draw(GameClient &game, Graphics::CameraState &camera, const engine::level::LightingSet *lighting, float waterSeconds);

	// The radar's picture of the level's terrain (its tiles' colours, the game's ground and water heights).
	engine::level::presentation::RadarTerrain BuildRadarTerrain(const engine::level::Level &level, GameClient &game) const;

	std::string Summary() const;
	std::string Failures() const;

private:
	// The shadow maps for this frame, from the objects that cast shadows (before the terrain samples them).
	void RenderShadowMaps(GameClient &game, Graphics::CameraState &camera, const engine::level::LightingSet *lighting);
	struct Renderers;
	std::unique_ptr<Renderers> m_renderers;
};
}
