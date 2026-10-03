include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/module_library.cmake")
if(NOT TARGET engine_gui_images)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../images" "${CMAKE_CURRENT_BINARY_DIR}/images")
endif()
engine_add_module_library(engine_gui_w3d_rendering
    MODULES text_view.cppm value_view.cppm button_view.cppm popup_view.cppm marquee_view.cppm list_view.cppm input_view.cppm text_edit_view.cppm scrollbar_view.cppm model_backdrop_view.cppm
    DEPENDS engine_gui_text engine_gui_w3d engine_gui_images)

if(BUILD_TESTING AND RTS_BUILD_GRAPHICS_TESTS AND WIN32)
    find_package(Boost CONFIG REQUIRED)
    get_target_property(w3d_shader_directory generals_graphics GRAPHICS_SHADER_ASSET_DIRECTORY)
    add_executable(engine_gui_w3d_text_drawing_tests)
    target_sources(engine_gui_w3d_text_drawing_tests PRIVATE FILE_SET CXX_MODULES
        FILES text_view.test.cppm)
    target_compile_features(engine_gui_w3d_text_drawing_tests PRIVATE cxx_std_23)
    target_include_directories(engine_gui_w3d_text_drawing_tests PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(engine_gui_w3d_text_drawing_tests PRIVATE
        engine_gui_w3d_rendering generals_graphics_test_support engine_platform_sdl3)
    target_compile_definitions(engine_gui_w3d_text_drawing_tests PRIVATE
        GRAPHICS_TERRAIN_SHADER_DIRECTORY="${w3d_shader_directory}")
    add_dependencies(engine_gui_w3d_text_drawing_tests generals_graphics_shaders)
    add_custom_command(TARGET engine_gui_w3d_text_drawing_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:engine_gui_w3d_text_drawing_tests>
            $<TARGET_FILE_DIR:engine_gui_w3d_text_drawing_tests>
        COMMAND_EXPAND_LISTS)
    set_property(GLOBAL APPEND PROPERTY GRAPHICS_TEST_TARGETS engine_gui_w3d_text_drawing_tests)
    foreach(backend IN ITEMS dx11 dx12)
        foreach(driver IN ITEMS hardware warp)
            set(instance "engine_gui_w3d_text_drawing_tests.${backend}.${driver}")
            add_test(NAME ${instance} COMMAND engine_gui_w3d_text_drawing_tests)
            set_tests_properties(${instance} PROPERTIES LABELS "graphics;${backend};${driver}"
                ENVIRONMENT "GRAPHICS_BACKEND=${backend};GRAPHICS_TEST_BACKEND=${backend};GRAPHICS_TEST_DRIVER=${driver};GRAPHICS_TEST_FAILURE_SUFFIX=${backend}-${driver}")
        endforeach()
    endforeach()
endif()
