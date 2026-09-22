find_package(spdlog CONFIG REQUIRED)

add_library(engine_debug STATIC)
target_sources(engine_debug PUBLIC
	FILE_SET CXX_MODULES
	BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
	FILES Debug.cppm
)
target_sources(engine_debug PRIVATE Debug-impl.cpp)
target_compile_features(engine_debug PUBLIC cxx_std_23)
target_link_libraries(engine_debug PUBLIC spdlog::spdlog)
set_property(TARGET engine_debug PROPERTY CXX_SCAN_FOR_MODULES ON)
