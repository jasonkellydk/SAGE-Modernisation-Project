# Format-agnostic configuration: adapters parse a format into a document
# tree; bindings map documents onto plain structs through declared schemas.
# Games supply the grammar and schemas; nothing here knows about gameplay.
if(NOT TARGET engine_time)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../time" "${CMAKE_CURRENT_BINARY_DIR}/../engine_time")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/module_library.cmake")

engine_add_module_library(engine_config
	MODULES
		document/document.cppm
		document/diagnostics.cppm
		adapters/ini/ini_reader.cppm
		adapters/json/json_reader.cppm
		adapters/preferences/preferences_file.cppm
		binding/values.cppm
		binding/schema.cppm
	DEPENDS engine_core_math_fixed engine_time
	TESTS
		adapters/ini/ini_reader.test
		adapters/json/json_reader.test
		adapters/preferences/preferences_file.test
		binding/schema.test
	TESTS_OPTION ENGINE_CONFIG_BUILD_TESTS)
