export module games.generalszh.content.terrain.terrain_type;
import std;

export import engine.config.binding.schema;

export namespace generalszh::content
{
// "Terrain <name>" blocks (Data/INI/Terrain.ini): the texture a terrain
// material name refers to. Terrain textures live under Art/Terrain/.
struct TerrainTypeDefinition
{
	std::string texture;
	std::string terrainClass;
	bool blendEdges{false};
	bool restrictConstruction{false};
};

inline engine::config::Schema<TerrainTypeDefinition> TerrainTypeSchema()
{
	return engine::config::Schema<TerrainTypeDefinition>{}
		.String("Texture", &TerrainTypeDefinition::texture)
		.String("Class", &TerrainTypeDefinition::terrainClass)
		.Boolean("BlendEdges", &TerrainTypeDefinition::blendEdges)
		.Boolean("RestrictConstruction", &TerrainTypeDefinition::restrictConstruction);
}

inline std::string TerrainTexturePath(const TerrainTypeDefinition &terrain) { return "Art/Terrain/" + terrain.texture; }
}
