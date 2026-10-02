include(FetchContent)
FetchContent_Declare(engine_lua_source
	URL https://www.lua.org/ftp/lua-5.4.9.tar.gz
	URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6)
FetchContent_MakeAvailable(engine_lua_source)
enable_language(C)
# The third-party interpreter remains upstream C. All engine/game adapters
# are C++23 module interfaces; neither legacy DLLs nor Lua executables link.
file(GLOB lua_sources "${engine_lua_source_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM lua_sources "${engine_lua_source_SOURCE_DIR}/src/lua.c" "${engine_lua_source_SOURCE_DIR}/src/luac.c")
add_library(engine_lua_vendor STATIC ${lua_sources})
target_include_directories(engine_lua_vendor PUBLIC "${engine_lua_source_SOURCE_DIR}/src")
if(UNIX)
	target_link_libraries(engine_lua_vendor PUBLIC m)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/module_library.cmake")
engine_add_module_library(engine_scripting_lua
	MODULES lua_program.cppm
	DEPENDS engine_lua_vendor
	TESTS lua_program.test
	TESTS_OPTION ENGINE_SCRIPTING_BUILD_TESTS)
