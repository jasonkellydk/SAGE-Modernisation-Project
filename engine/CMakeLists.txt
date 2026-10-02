cmake_minimum_required(VERSION 4.4.3)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(engine_foundation LANGUAGES CXX)
    include(cmake/toolchain/validate.cmake)
endif()

# Asset-free entry point. Do not include the legacy game's platform/dependency
# configuration here: this build validates the modern engine in isolation.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/std_module.cmake)
engine_add_std_module()

if(BUILD_TESTING)
    add_test(NAME gameplay_architecture COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/.."
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/architecture/gameplay.cmake")
    add_test(NAME gameplay_architecture_checker COMMAND "${CMAKE_COMMAND}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/architecture-checker-fixtures"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/architecture/gameplay.test.cmake")
    add_test(NAME modern_code_policy COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/.."
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/architecture/modern_code.cmake")
    add_test(NAME modern_code_policy_checker COMMAND "${CMAKE_COMMAND}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/modern-code-checker-fixtures"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/architecture/modern_code.test.cmake")
    add_test(NAME engine_toolchain_validation COMMAND "${CMAKE_COMMAND}"
        "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain/validate.test.cmake")
    add_test(NAME engine_toolchain_bootstrap COMMAND "${CMAKE_COMMAND}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain/bootstrap.test.cmake")
endif()

option(ENGINE_MATH_BUILD_TESTS "Build deterministic fixed-point math tests" ${BUILD_TESTING})
option(ENGINE_CONFIG_BUILD_TESTS "Build config adapter and binding tests" ${BUILD_TESTING})
option(ENGINE_FILESYSTEM_BUILD_TESTS "Build virtual file system tests" ${BUILD_TESTING})
option(ENGINE_COMPRESSION_BUILD_TESTS "Build decompression codec tests" ${BUILD_TESTING})
option(ENGINE_LEVEL_BUILD_TESTS "Build level model and adapter tests" ${BUILD_TESTING})
option(ENGINE_LOCALIZATION_BUILD_TESTS "Build localization tests" ${BUILD_TESTING})
option(ENGINE_SCRIPTING_BUILD_TESTS "Build script runtime tests" ${BUILD_TESTING})
option(ENGINE_CORE_SERIALIZATION_BUILD_TESTS "Build binary serialization tests" ${BUILD_TESTING})
option(ENGINE_CORE_TEXT_BUILD_TESTS "Build text encoding tests" ${BUILD_TESTING})
option(ENGINE_EFFECTS_BUILD_TESTS "Build effects tests" ${BUILD_TESTING})
option(ENGINE_AUDIO_BUILD_TESTS "Build audio tests" ${BUILD_TESTING})
option(ENGINE_NET_BUILD_TESTS "Build lockstep networking tests" ${BUILD_TESTING})
option(ENGINE_CAMERA_BUILD_TESTS "Build camera tests" ${BUILD_TESTING})
option(ENGINE_JOBS_BUILD_TESTS "Build headless job-system tests" ${BUILD_TESTING})
option(ENGINE_EVENTS_BUILD_TESTS "Build headless event-batch tests" ${BUILD_TESTING})
option(ENGINE_TIME_BUILD_TESTS "Build headless simulation-time tests" ${BUILD_TESTING})
option(ENGINE_ECS_BUILD_TESTS "Build headless ECS tests" ${BUILD_TESTING})
option(ENGINE_GAMEPLAY_BUILD_TESTS "Build headless gameplay tests" ${BUILD_TESTING})

# Profiling and logging (the root build adds these; standalone builds add
# them here, with Tracy disabled unless the root enables it).
if(NOT TARGET engine_profiling)
	if(NOT TARGET tracy_runtime)
		add_library(tracy_runtime INTERFACE)
	endif()
	add_subdirectory(profiling)
endif()
if(NOT TARGET engine_debug)
	add_subdirectory(debug)
endif()
add_subdirectory(platform)
add_subdirectory(core/math)
add_subdirectory(core/serialization)
add_subdirectory(core/text)
add_subdirectory(jobs)
add_subdirectory(events)
add_subdirectory(time)
add_subdirectory(config)
add_subdirectory(filesystem)
add_subdirectory(compression)
add_subdirectory(level)
add_subdirectory(localization)
add_subdirectory(scripting)
add_subdirectory(net)
add_subdirectory(camera)
add_subdirectory(audio)
add_subdirectory(effects)
option(ENGINE_GUI_MVVM_BUILD_TESTS "Build MVVM tests" ${BUILD_TESTING})
add_subdirectory(gui/mvvm)
add_subdirectory(ecs)
add_subdirectory(gameplay)
