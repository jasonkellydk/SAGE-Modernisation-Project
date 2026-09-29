# Levels: a format-neutral description of an authored playable space
# (terrain, placements, markers, regions, lighting, scenario, scripts) and
# format adapters that produce it.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/module_library.cmake")

engine_add_module_library(engine_level
	MODULES
		model/properties.cppm
		model/level.cppm
		adapters/generals_map/chunks/chunk_file.cppm
		adapters/generals_map/map_reader.cppm
	DEPENDS engine_core_math_fixed engine_compression
	TESTS adapters/generals_map/map_reader.test
	TESTS_OPTION ENGINE_LEVEL_BUILD_TESTS)

# Client-side presentation bridge: level terrain -> renderable data (floats allowed).
engine_add_module_library(engine_level_presentation
	MODULES
		presentation/terrain_mesh.cppm
		presentation/terrain_atlas.cppm
		presentation/terrain_textures.cppm
		presentation/water_mesh.cppm
		presentation/radar_terrain.cppm
	DEPENDS engine_level engine_core_math_fixed
	TESTS presentation/terrain_textures.test presentation/water_mesh.test presentation/radar_terrain.test
	TEST_DEPENDS engine_filesystem
	TESTS_OPTION ENGINE_LEVEL_BUILD_TESTS)
