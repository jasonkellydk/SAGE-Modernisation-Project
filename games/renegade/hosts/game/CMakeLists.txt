add_executable(renegade main.cppm)
set(RENEGADE_HOST_OUTPUT_DIRECTORY "" CACHE PATH "Optional game output directory for an isolated development build")
if(RENEGADE_HOST_OUTPUT_DIRECTORY)
    set_target_properties(renegade PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${RENEGADE_HOST_OUTPUT_DIRECTORY}")
endif()
set_source_files_properties(main.cppm PROPERTIES LANGUAGE CXX)
target_link_libraries(renegade PRIVATE renegade_content renegade_menu renegade_menu_assets
    engine_platform_sdl3 generals_graphics generals_video engine_gui_text engine_gui_w3d_rendering engine_audio_video engine_audio_ffmpeg engine_audio_output stb)
get_target_property(renegade_shader_directory generals_graphics GRAPHICS_SHADER_ASSET_DIRECTORY)
target_compile_definitions(renegade PRIVATE RENEGADE_SHADER_DIRECTORY="${renegade_shader_directory}")
if(WIN32)
    add_custom_command(TARGET renegade POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:renegade> $<TARGET_FILE_DIR:renegade>
        COMMAND_EXPAND_LISTS)
    file(GLOB renegade_ffmpeg_dlls "${FFMPEG_RUNTIME_DIRECTORY}/av*.dll" "${FFMPEG_RUNTIME_DIRECTORY}/sw*.dll")
    if(renegade_ffmpeg_dlls)
        add_custom_command(TARGET renegade POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${renegade_ffmpeg_dlls} $<TARGET_FILE_DIR:renegade>)
    endif()
endif()
if(BUILD_TESTING AND RENEGADE_INSTALL_DIR)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    foreach(backend IN ITEMS dx11 dx12)
        set(scrollbars_instance "renegade_retail_scrollbars.${backend}")
        add_test(NAME ${scrollbars_instance} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scrollbars_runtime_test.py"
            "$<TARGET_FILE:renegade>" "${RENEGADE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/scrollbars-${backend}.png")
        set_tests_properties(${scrollbars_instance} PROPERTIES LABELS "retail;runtime;menu;input;${backend}"
            TIMEOUT 65 RESOURCE_LOCK renegade_retail_menu ENVIRONMENT "GRAPHICS_BACKEND=${backend}")
        set(multiplayer_options_instance "renegade_retail_multiplayer_options.${backend}")
        add_test(NAME ${multiplayer_options_instance} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/multiplayer_options_runtime_test.py"
            "$<TARGET_FILE:renegade>" "${RENEGADE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/multiplayer-options-${backend}.png")
        set_tests_properties(${multiplayer_options_instance} PROPERTIES LABELS "retail;runtime;menu;input;${backend}"
            TIMEOUT 65 RESOURCE_LOCK renegade_retail_menu
            ENVIRONMENT "GRAPHICS_BACKEND=${backend}")
        set(profiles_instance "renegade_retail_profiles.${backend}")
        add_test(NAME ${profiles_instance} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/profiles_runtime_test.py"
            "$<TARGET_FILE:renegade>" "${RENEGADE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/profiles-${backend}.png")
        set_tests_properties(${profiles_instance} PROPERTIES LABELS "retail;runtime;menu;input;${backend}"
            TIMEOUT 75 RESOURCE_LOCK renegade_retail_menu
            ENVIRONMENT "GRAPHICS_BACKEND=${backend}")
        set(controls_instance "renegade_retail_controls.${backend}")
        add_test(NAME ${controls_instance} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/controls_runtime_test.py"
            "$<TARGET_FILE:renegade>" "${RENEGADE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/controls-${backend}.png")
        set_tests_properties(${controls_instance} PROPERTIES LABELS "retail;runtime;menu;input;${backend}"
            TIMEOUT 75 RESOURCE_LOCK renegade_retail_menu
            ENVIRONMENT "GRAPHICS_BACKEND=${backend}")
        set(performance_instance "renegade_retail_performance.${backend}")
        add_test(NAME ${performance_instance} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/performance_runtime_test.py"
            "$<TARGET_FILE:renegade>" "${RENEGADE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/performance-${backend}.png")
        set_tests_properties(${performance_instance} PROPERTIES LABELS "retail;runtime;menu;${backend}"
            TIMEOUT 75 RESOURCE_LOCK renegade_retail_menu
            ENVIRONMENT "GRAPHICS_BACKEND=${backend}")
        set(video_instance "renegade_retail_video.${backend}")
        add_test(NAME ${video_instance} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/video_runtime_test.py"
            "$<TARGET_FILE:renegade>" "${RENEGADE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/video-${backend}.png")
        set_tests_properties(${video_instance} PROPERTIES LABELS "retail;runtime;menu;video;${backend}"
            TIMEOUT 75 RESOURCE_LOCK renegade_retail_menu
            ENVIRONMENT "GRAPHICS_BACKEND=${backend}")
        set(gallery_instance "renegade_retail_gallery.${backend}")
        add_test(NAME ${gallery_instance} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/gallery_runtime_test.py"
            "$<TARGET_FILE:renegade>" "${RENEGADE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/gallery-${backend}.png")
        set_tests_properties(${gallery_instance} PROPERTIES LABELS "retail;runtime;menu;video;${backend}"
            TIMEOUT 90 RESOURCE_LOCK renegade_retail_menu
            ENVIRONMENT "GRAPHICS_BACKEND=${backend}")
        set(credits_instance "renegade_retail_credits.${backend}")
        add_test(NAME ${credits_instance} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/credits_runtime_test.py"
            "$<TARGET_FILE:renegade>" "${RENEGADE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/credits-${backend}.png")
        set_tests_properties(${credits_instance} PROPERTIES LABELS "retail;runtime;menu;${backend}"
            TIMEOUT 90 RESOURCE_LOCK renegade_retail_menu
            ENVIRONMENT "GRAPHICS_BACKEND=${backend}")
        set(instance "renegade_retail_menu.${backend}")
        add_test(NAME ${instance} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/menu_runtime_test.py"
            "$<TARGET_FILE:renegade>" "${RENEGADE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/menu-${backend}.png")
        set_tests_properties(${instance} PROPERTIES LABELS "retail;runtime;menu;${backend}"
            TIMEOUT 60 RESOURCE_LOCK renegade_retail_menu
            ENVIRONMENT "GRAPHICS_BACKEND=${backend}")
        set(intro_instance "renegade_retail_intro.${backend}")
        add_test(NAME ${intro_instance} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/menu_runtime_test.py"
            "$<TARGET_FILE:renegade>" "${RENEGADE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/intro-${backend}.png" --with-intro)
        set_tests_properties(${intro_instance} PROPERTIES LABELS "retail;runtime;video;${backend}"
            TIMEOUT 120 RESOURCE_LOCK renegade_retail_menu
            ENVIRONMENT "GRAPHICS_BACKEND=${backend}")
        set(skip_instance "renegade_retail_intro_skip.${backend}")
        add_test(NAME ${skip_instance} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/intro_skip_runtime_test.py"
            "$<TARGET_FILE:renegade>" "${RENEGADE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/intro-skip-${backend}-user-data")
        set_tests_properties(${skip_instance} PROPERTIES LABELS "retail;runtime;video;${backend}"
            TIMEOUT 90 RESOURCE_LOCK renegade_retail_menu
            ENVIRONMENT "GRAPHICS_BACKEND=${backend}")
    endforeach()
endif()
