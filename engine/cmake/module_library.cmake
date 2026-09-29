include_guard(GLOBAL)

# One C++23 module library per domain, plus its Boost.Test executable.
#
#   engine_add_module_library(<target>
#       MODULES <.cppm files>
#       [DEPENDS <targets>]              # public link dependencies
#       [TESTS <.test files>]            # the first test file defines BOOST_TEST_MODULE
#       [TEST_DEPENDS <targets>]
#       [TESTS_OPTION <option variable>]) # tests build when this is ON (default BUILD_TESTING)
function(engine_add_module_library target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "TESTS_OPTION" "MODULES;DEPENDS;TESTS;TEST_DEPENDS")
    if(NOT arg_MODULES)
        message(FATAL_ERROR "engine_add_module_library(${target}) needs MODULES")
    endif()

    add_library(${target} STATIC)
    # Generated modules live in the build tree, so both trees are base directories.
    target_sources(${target} PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" FILES ${arg_MODULES})
    target_compile_features(${target} PUBLIC cxx_std_23)
    set_property(TARGET ${target} PROPERTY CXX_SCAN_FOR_MODULES ON)
    if(arg_DEPENDS)
        target_link_libraries(${target} PUBLIC ${arg_DEPENDS})
    endif()
    if(TARGET core_config)
        target_link_libraries(${target} PUBLIC core_config)
    endif()

    set(tests_enabled ${BUILD_TESTING})
    if(arg_TESTS_OPTION)
        set(tests_enabled ${${arg_TESTS_OPTION}})
    endif()
    if(arg_TESTS AND tests_enabled)
        find_package(boost_included_unit_test_framework CONFIG REQUIRED)
        set_source_files_properties(${arg_TESTS} PROPERTIES LANGUAGE CXX)
        add_executable(${target}_tests ${arg_TESTS})
        set_property(TARGET ${target}_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
        target_compile_definitions(${target}_tests PRIVATE BOOST_TEST_INCLUDED)
        target_link_libraries(${target}_tests PRIVATE ${target} ${arg_TEST_DEPENDS} Boost::included_unit_test_framework)
        enable_testing()
        add_test(NAME ${target}_tests COMMAND ${target}_tests)
    endif()
endfunction()
