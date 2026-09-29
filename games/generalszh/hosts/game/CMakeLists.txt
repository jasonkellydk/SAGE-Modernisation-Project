# The game executable: platform window, fixed-step loop, renderer, shell.
add_executable(generalszh main.cpp asset_source.cpp game_client.cpp world_scene.cpp crash_report.cpp front_end.cpp)
target_sources(generalszh PRIVATE FILE_SET CXX_MODULES FILES
	asset_source.cppm
	shell_menu.cppm
	game_client.cppm
	world_scene.cppm
	credits_view.cppm
	floating_text_view.cppm
	world_animation_view.cppm
	control_bar_layer.cppm
	match_end_layer.cppm
	message_view.cppm
	military_caption_view.cppm
	named_timers_view.cppm
	superweapon_timers_view.cppm
	radar_minimap.cppm
	movie_player.cppm
	setup_catalog_source.cppm
	window_transitions_source.cppm
	front_end.cppm)
set_property(TARGET generalszh PROPERTY CXX_SCAN_FOR_MODULES ON)
target_compile_features(generalszh PRIVATE cxx_std_23)
target_link_libraries(generalszh PRIVATE
	generalszh_content_install
	generalszh_content
	generalszh_shell_views
	generalszh_hud_views
	generalszh_presentation_hud
	generalszh_presentation_hud_messages
	generalszh_scripting
	generalszh_session
	generalszh_presentation_audio
	generalszh_presentation_effects
	generalszh_presentation_objects
	generalszh_presentation_sound
	generalszh_presentation_interaction
	generalszh_presentation_rendering
	engine_effects_particles
	engine_jobs
	engine_net
	engine_audio_output
	engine_camera
	engine_level
	engine_level_presentation
	engine_localization
	engine_time
	engine_platform_sdl3
	generals_graphics
	generals_video
	generals_ui_wnd
	generals_assets
	stb)
get_target_property(generalszh_shader_directory generals_graphics GRAPHICS_SHADER_ASSET_DIRECTORY)
target_compile_definitions(generalszh PRIVATE GENERALSZH_SHADER_DIRECTORY="${generalszh_shader_directory}")
# A linker map beside the executable resolves the crash report's module+offset frames.
if(WIN32)
	target_link_options(generalszh PRIVATE "LINKER:/MAP:$<TARGET_FILE_DIR:generalszh>/generalszh.map")
endif()

# Runtime DLLs next to the executable: linked shared targets (SDL3, SDL3_net,
# ...) and FFmpeg's DLLs, which are found as plain libraries.
if(WIN32)
	add_custom_command(TARGET generalszh POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:generalszh> $<TARGET_FILE_DIR:generalszh>
		COMMAND_EXPAND_LISTS)
	if(DEFINED FFMPEG_RUNTIME_DIRECTORY)
		file(GLOB generalszh_ffmpeg_dlls "${FFMPEG_RUNTIME_DIRECTORY}/av*.dll" "${FFMPEG_RUNTIME_DIRECTORY}/sw*.dll")
		if(generalszh_ffmpeg_dlls)
			add_custom_command(TARGET generalszh POST_BUILD
				COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${generalszh_ffmpeg_dlls} $<TARGET_FILE_DIR:generalszh>)
		endif()
	endif()
endif()
