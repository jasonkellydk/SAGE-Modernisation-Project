add_library(engine_profiling STATIC)
target_sources(engine_profiling PUBLIC
	FILE_SET CXX_MODULES
	BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
	FILES Profiling.cppm
)
target_sources(engine_profiling PRIVATE Profiling-impl.cpp)
target_compile_features(engine_profiling PUBLIC cxx_std_23)
target_link_libraries(engine_profiling PUBLIC tracy_runtime)
if(RTS_BUILD_OPTION_PROFILE_TRACY)
	target_compile_definitions(engine_profiling PUBLIC RTS_PROFILE_TRACY TRACY_ENABLE)
	target_include_directories(engine_profiling SYSTEM PUBLIC
		"${PROJECT_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include"
	)
endif()
set_property(TARGET engine_profiling PROPERTY CXX_SCAN_FOR_MODULES ON)
