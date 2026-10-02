# Generic trigger-script runtime over level scripts; games supply vocabularies.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/module_library.cmake")

engine_add_module_library(engine_scripting
	MODULES runtime/script_runtime.cppm
	DEPENDS engine_level engine_core_math_fixed engine_core_serialization
	TESTS runtime/script_runtime.test
	TESTS_OPTION ENGINE_SCRIPTING_BUILD_TESTS)

option(ENGINE_SCRIPTING_BUILD_LUA "Build the shared Lua mission adapter" OFF)
if(ENGINE_SCRIPTING_BUILD_LUA)
	add_subdirectory(lua)
endif()
