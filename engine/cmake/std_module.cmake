include_guard(GLOBAL)
set(ENGINE_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# The C++ standard library as one module (`import std;`), built once from the
# standard library's own std.ixx. Every module then imports it instead of
# including the standard headers in its global module fragment: a translation
# unit loading hundreds of modules no longer loads hundreds of copies of the
# standard headers (which ran clang out of source locations).
#
# The std.ixx is the one beside the headers the compiler itself uses (asked of
# the compiler, so the module always matches them); ENGINE_STD_MODULE_SOURCE
# overrides it.
function(engine_find_std_module out)
    if(ENGINE_STD_MODULE_SOURCE)
        set(${out} "${ENGINE_STD_MODULE_SOURCE}" PARENT_SCOPE)
        return()
    endif()
    # libc++ and libstdc++ (Linux, macOS, MinGW) describe their std module in a modules.json the compiler can locate.
    foreach(manifest libc++.modules.json libstdc++.modules.json)
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" "-print-file-name=${manifest}" OUTPUT_VARIABLE found OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(found AND IS_ABSOLUTE "${found}" AND EXISTS "${found}")
            file(READ "${found}" json)
            string(JSON count LENGTH "${json}" modules)
            math(EXPR last "${count} - 1")
            foreach(index RANGE ${last})
                string(JSON name GET "${json}" modules ${index} logical-name)
                if(name STREQUAL "std")
                    string(JSON relative GET "${json}" modules ${index} source-path)
                    get_filename_component(base "${found}" DIRECTORY)
                    get_filename_component(source "${base}/${relative}" ABSOLUTE)
                    set(${out} "${source}" PARENT_SCOPE)
                    return()
                endif()
            endforeach()
        endif()
    endforeach()
    # Microsoft's STL (clang targeting windows-msvc): std.ixx beside the headers the compiler uses.
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/std_probe.cpp" "#include <vector>\n")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -x c++ -E -v "${CMAKE_CURRENT_BINARY_DIR}/std_probe.cpp"
        OUTPUT_QUIET ERROR_VARIABLE probe RESULT_VARIABLE result)
    string(REGEX MATCH "[^\n\"]*[/\\\\]VC[/\\\\]Tools[/\\\\]MSVC[/\\\\][^/\\\\\n]+[/\\\\]include" include_dir "${probe}")
    string(STRIP "${include_dir}" include_dir)
    file(TO_CMAKE_PATH "${include_dir}" include_dir)
    get_filename_component(msvc_root "${include_dir}" DIRECTORY)
    set(source "${msvc_root}/modules/std.ixx")
    if(NOT include_dir OR NOT EXISTS "${source}")
        message(FATAL_ERROR "import std: the compiler's standard library has no std module source; set ENGINE_STD_MODULE_SOURCE")
    endif()
    set(${out} "${source}" PARENT_SCOPE)
endfunction()

function(engine_add_std_module)
    if(TARGET engine_std)
        return()
    endif()
    engine_find_std_module(source)
    get_filename_component(source_dir "${source}" DIRECTORY)
    add_library(engine_std STATIC)
    target_sources(engine_std PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES BASE_DIRS "${source_dir}" FILES "${source}")
    target_compile_features(engine_std PUBLIC cxx_std_23)
    set_property(TARGET engine_std PROPERTY CXX_SCAN_FOR_MODULES ON)
    # The standard library's own module source: its style is not ours to warn about.
    target_compile_options(engine_std PRIVATE -Wno-include-angled-in-module-purview -Wno-reserved-module-identifier -w)
    message(STATUS "import std: ${source}")
    # Debug checks as functions (engine.core.contracts), for every target alike.
    add_library(engine_core_contracts STATIC)
    target_sources(engine_core_contracts PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${ENGINE_CMAKE_DIR}/../core/contracts" FILES "${ENGINE_CMAKE_DIR}/../core/contracts/contracts.cppm")
    target_compile_features(engine_core_contracts PUBLIC cxx_std_23)
    set_property(TARGET engine_core_contracts PROPERTY CXX_SCAN_FOR_MODULES ON)
    target_link_libraries(engine_core_contracts PUBLIC engine_std)
endfunction()

# Links engine_std into every C++ target under `directory` (recursively), so any of them may `import std;`.
function(engine_link_std_everywhere directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        if(target STREQUAL "engine_std" OR target STREQUAL "engine_core_contracts")
            continue()
        endif()
        get_target_property(type ${target} TYPE)
        if(type STREQUAL "STATIC_LIBRARY" OR type STREQUAL "SHARED_LIBRARY" OR type STREQUAL "OBJECT_LIBRARY" OR type STREQUAL "EXECUTABLE")
            target_link_libraries(${target} PUBLIC engine_std engine_core_contracts)
        endif()
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        engine_link_std_everywhere("${child}")
    endforeach()
endfunction()
