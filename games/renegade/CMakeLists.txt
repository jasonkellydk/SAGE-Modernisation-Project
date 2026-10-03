cmake_minimum_required(VERSION 4.4.3)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(renegade LANGUAGES C CXX)
	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)
	include(CTest)
	include("../../engine/cmake/toolchain/validate.cmake")
	set(ENGINE_SCRIPTING_BUILD_LUA ON CACHE BOOL "Build the shared Lua mission adapter")
	add_subdirectory("../../engine" "${CMAKE_CURRENT_BINARY_DIR}/engine")
endif()

include("../../engine/cmake/module_library.cmake")
get_filename_component(renegade_repo_root "../.." ABSOLUTE)
if(NOT TARGET generals_assets)
	if(NOT TARGET stb)
		include("${renegade_repo_root}/cmake/stb.cmake")
	endif()
	add_subdirectory("${renegade_repo_root}/engine/assets" "${CMAKE_CURRENT_BINARY_DIR}/engine/assets")
endif()
option(RENEGADE_BUILD_GAME "Build the graphical retail Renegade host" ${WIN32})
if(NOT TARGET engine_level_assets)
	add_subdirectory("${renegade_repo_root}/engine/level/presentation" "${CMAKE_CURRENT_BINARY_DIR}/engine/level/assets")
endif()
if(RENEGADE_BUILD_GAME)
	get_filename_component(renegade_repo_root "../.." ABSOLUTE)
	if(NOT TARGET stb)
		include("${renegade_repo_root}/cmake/stb.cmake")
	endif()
	if(NOT TARGET generals_assets)
		add_subdirectory("${renegade_repo_root}/engine/assets" "${CMAKE_CURRENT_BINARY_DIR}/engine/assets")
	endif()
	if(NOT TARGET generals_video)
		include("${renegade_repo_root}/cmake/ffmpeg.cmake")
		add_subdirectory("${renegade_repo_root}/engine/video" "${CMAKE_CURRENT_BINARY_DIR}/engine/video")
	endif()
	if(NOT TARGET generals_graphics)
		include("${renegade_repo_root}/cmake/slang.cmake")
		add_subdirectory("${renegade_repo_root}/engine/graphics" "${CMAKE_CURRENT_BINARY_DIR}/engine/graphics")
	endif()
	if(NOT TARGET engine_assets_filesystem)
		add_subdirectory("${renegade_repo_root}/engine/assets/adapters/filesystem" "${CMAKE_CURRENT_BINARY_DIR}/engine/assets/filesystem")
	endif()
	if(NOT TARGET engine_audio_video)
		add_subdirectory("${renegade_repo_root}/engine/audio/adapters/video" "${CMAKE_CURRENT_BINARY_DIR}/engine/audio/video")
	endif()
	if(NOT TARGET engine_gui_text)
		add_subdirectory("${renegade_repo_root}/engine/gui/text" "${CMAKE_CURRENT_BINARY_DIR}/engine/gui/text")
	endif()
	if(NOT TARGET engine_gui_w3d_rendering)
		add_subdirectory("${renegade_repo_root}/engine/gui/w3d/rendering" "${CMAKE_CURRENT_BINARY_DIR}/engine/gui/w3d/rendering")
	endif()
endif()
if(NOT TARGET engine_scripting_lua)
	add_subdirectory("../../engine/scripting/lua" "${CMAKE_CURRENT_BINARY_DIR}/engine/scripting/lua")
endif()
engine_add_module_library(renegade_defense
	MODULES gameplay/defense/components/defense.cppm gameplay/defense/resources/damage_rules.cppm
		gameplay/defense/algorithms/apply_damage.cppm gameplay/defense/systems/defense_system.cppm
	DEPENDS engine_gameplay_health_components engine_ecs)
engine_add_module_library(renegade_levels
	MODULES content/levels/persist_records.cppm content/levels/transition_definitions.cppm content/levels/definition_catalog.cppm content/levels/physics_placement.cppm content/levels/dynamic_level.cppm content/levels/navigation_paths.cppm content/levels/traversal_portals.cppm content/levels/level_scene.cppm content/levels/render_assets.cppm content/levels/background.cppm content/presentation/background_colors.cppm content/levels/sounds.cppm content/levels/vehicle_presentation.cppm content/levels/weapon_presentation.cppm content/campaign/conversations.cppm content/audio/sound_preset.cppm
	DEPENDS generals_assets engine_filesystem engine_core_math_fixed engine_level
	TESTS content/levels/level_scene.test content/campaign/conversations.test
	TEST_DEPENDS renegade_content)
engine_add_module_library(renegade_weapons_content
	MODULES content/weapons/weapon_catalog.cppm content/weapons/armed_loadout.cppm
	DEPENDS renegade_levels
	TESTS content/weapons/weapon_catalog.test
	TEST_DEPENDS renegade_content)
engine_add_module_library(renegade_humans
	MODULES gameplay/humans/components/motion.cppm gameplay/humans/components/animation.cppm gameplay/humans/components/goto_action.cppm gameplay/humans/resources/animations.cppm gameplay/humans/resources/ladders.cppm gameplay/humans/systems/motion_system.cppm gameplay/humans/systems/ladder_transition_system.cppm gameplay/humans/systems/animation_system.cppm
	DEPENDS renegade_content engine_gameplay_swept_motion engine_gameplay_timeline engine_gameplay_suspension engine_gameplay_route_traversal engine_gameplay_input_permission
	TESTS gameplay/humans/motion.test gameplay/humans/animation.test gameplay/humans/ladder.test)
engine_add_module_library(renegade_weapons
	MODULES gameplay/weapons/components/weapon_state.cppm gameplay/weapons/systems/weapon_state_system.cppm gameplay/weapons/systems/weapon_control_system.cppm
	DEPENDS renegade_weapons_content engine_gameplay_inventory engine_gameplay_input_permission engine_gameplay_health_components
	TESTS gameplay/weapons/weapon_state.test)
engine_add_module_library(renegade_cinematics_content
	MODULES content/cinematics/cinematic.cppm content/cinematics/startup.cppm content/cinematics/attack.cppm
	DEPENDS engine_level engine_scripting_lua engine_filesystem
	TESTS content/cinematics/cinematic.test
	TEST_DEPENDS renegade_content)
engine_add_module_library(renegade_cinematics
	MODULES gameplay/cinematics/components/cinematic_state.cppm gameplay/cinematics/resources/cinematic_library.cppm gameplay/cinematics/systems/cinematic_system.cppm
	DEPENDS renegade_cinematics_content renegade_humans engine_gameplay_timeline engine_gameplay_appearance engine_gameplay_fire_sequence)
engine_add_module_library(renegade_session
	MODULES session/scene_state.cppm session/session.cppm gameplay/humans/systems/goto_system.cppm gameplay/missions/resources/routes.cppm gameplay/missions/components/conversation_state.cppm gameplay/missions/resources/conversation_library.cppm gameplay/missions/resources/animation_library.cppm gameplay/missions/resources/mission_requests.cppm gameplay/missions/resources/camera_requests.cppm gameplay/missions/systems/mission_command_system.cppm gameplay/missions/systems/conversation_system.cppm
		gameplay/missions/components/objective.cppm gameplay/missions/systems/objective_system.cppm
		gameplay/missions/components/speech_animation.cppm gameplay/missions/resources/speech_animations.cppm gameplay/missions/systems/speech_animation_system.cppm
	DEPENDS renegade_defense renegade_humans renegade_cinematics renegade_weapons engine_net_authority engine_gameplay_spatial engine_gameplay_areas engine_gameplay_swept_motion engine_gameplay_kinematic_collision engine_gameplay_audio engine_gameplay_script_events renegade_levels renegade_content
	TESTS gameplay/defense/defense.test gameplay/humans/goto.test session/scene.test session/conversation.test session/objective.test session/speech_animation.test
	TEST_DEPENDS renegade_content)
engine_add_module_library(renegade_player_input
	MODULES presentation/player/input.cppm
	DEPENDS renegade_humans renegade_weapons engine_platform renegade_content
	TESTS presentation/player/input.test)
engine_add_module_library(renegade_content
	# Source viseme rules are game content, independently of renderer mechanisms.
	MODULES content/install/install_mount.cppm content/armor/armor_catalog.cppm content/armor/defense_preset.cppm content/cameras/camera_profiles.cppm content/characters/movement_definition.cppm content/characters/actor_animation.cppm content/campaign/campaign_content.cppm content/campaign/campaign_flow.cppm content/campaign/behavior_programs.cppm content/audio/audio_settings.cppm content/input/schema.cppm content/input/profile_catalog.cppm content/presentation/input_configuration.cppm content/presentation/input_profile_store.cppm content/presentation/hud_settings.cppm
	MODULES content/characters/visemes.cppm
	DEPENDS engine_filesystem engine_config engine_platform engine_core_serialization engine_gameplay_script_events renegade_defense renegade_levels renegade_weapons_content
	TESTS content/install/install_mount.test content/armor/armor_catalog.test content/armor/defense_preset.test content/cameras/camera_profiles.test content/characters/movement_definition.test content/characters/actor_animation.test content/campaign/campaign_content.test content/campaign/behavior_programs.test content/input/configuration.test content/input/profile_catalog.test content/presentation/input_profile_store.test content/presentation/hud_settings.test
	TEST_DEPENDS engine_core_serialization)
engine_add_module_library(renegade_hud
	MODULES hud/vitals_view_model.cppm hud/help_text_view_model.cppm hud/objective_view_model.cppm hud/player_hud_view_model.cppm
	DEPENDS engine_gui_w3d engine_gameplay_health_components engine_gameplay_inventory engine_time renegade_session
	TESTS hud/vitals_view_model.test hud/help_text_view_model.test hud/objective_view_model.test hud/player_hud_view_model.test)
engine_add_module_library(renegade_menu
	MODULES presentation/menu/main_menu.cppm presentation/menu/intro.cppm presentation/menu/navigation.cppm presentation/menu/single_player.cppm presentation/menu/pause.cppm presentation/menu/audio_settings.cppm presentation/menu/audio_preview.cppm presentation/menu/audio_preferences.cppm presentation/menu/menu_entry.cppm presentation/menu/movie_gallery.cppm presentation/menu/video_settings.cppm presentation/menu/performance_settings.cppm presentation/menu/controls_settings.cppm presentation/menu/control_profiles.cppm presentation/menu/multiplayer_options.cppm
	DEPENDS engine_gui_w3d engine_config renegade_content
	TESTS presentation/menu/main_menu.test presentation/menu/audio_settings.test presentation/menu/audio_preview.test presentation/menu/audio_preferences.test presentation/menu/menu_entry.test presentation/menu/movie_gallery.test presentation/menu/video_settings.test presentation/menu/performance_settings.test presentation/menu/controls_settings.test presentation/menu/control_profiles.test presentation/menu/multiplayer_options.test)
add_executable(renegade_headless hosts/headless/main.cppm)
set_source_files_properties(hosts/headless/main.cppm PROPERTIES LANGUAGE CXX)
target_link_libraries(renegade_headless PRIVATE renegade_session renegade_content)

if(RENEGADE_BUILD_GAME)
	set(renegade_retail_intro_tests)
	if(RENEGADE_INSTALL_DIR)
		list(APPEND renegade_retail_intro_tests content/presentation/intro_assets.test)
	endif()
	engine_add_module_library(renegade_menu_assets
		MODULES content/presentation/asset_paths.cppm content/presentation/strings.cppm content/presentation/style.cppm content/presentation/credits.cppm content/presentation/loading_screen.cppm presentation/menu/movie_audio.cppm
		DEPENDS engine_assets_filesystem engine_config engine_audio_video
		TESTS content/presentation/strings.test content/presentation/style.test content/presentation/credits.test content/presentation/loading_screen.test presentation/menu/movie_audio.test ${renegade_retail_intro_tests}
		TEST_DEPENDS renegade_content engine_gui_w3d generals_video)
	if(TARGET renegade_menu_assets_tests)
		set_tests_properties(renegade_menu_assets_tests PROPERTIES ENVIRONMENT "RENEGADE_INSTALL_DIR=${RENEGADE_INSTALL_DIR}")
		if(WIN32 AND FFMPEG_RUNTIME_DIRECTORY)
			set_tests_properties(renegade_menu_assets_tests PROPERTIES
				ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${FFMPEG_RUNTIME_DIRECTORY}")
		endif()
	endif()
	engine_add_module_library(renegade_level_view
		MODULES presentation/scene/level_view.cppm presentation/scene/cinematic_assets.cppm presentation/scene/speech_assets.cppm
		DEPENDS renegade_session renegade_levels renegade_menu_assets renegade_hud renegade_player_input engine_config engine_camera generals_graphics engine_gui_w3d_rendering engine_level_assets engine_audio_scene engine_audio_ffmpeg
		TESTS presentation/scene/cinematic_assets.test
		TEST_DEPENDS engine_assets_filesystem)
	add_subdirectory(hosts/game)
	# Exercise the existing GeneralsZH text/control renderer against the shared
	# font implementation whenever its graphics regression suite is requested.
	if(BUILD_TESTING AND RTS_BUILD_GRAPHICS_TESTS AND NOT TARGET generals_ui_wnd)
		add_subdirectory("${renegade_repo_root}/engine/gui/wnd" "${CMAKE_CURRENT_BINARY_DIR}/engine/gui/wnd")
	endif()
endif()

if(BUILD_TESTING)
	find_package(Python3 COMPONENTS Interpreter REQUIRED)
	add_test(NAME renegade_ledger COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/migration/tools/ledger.py" check)
	add_test(NAME renegade_ledger_checker COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/migration/tools/ledger_test.py")
	add_executable(renegade_mission_tests scripting/mission.test)
	set_source_files_properties(scripting/mission.test PROPERTIES LANGUAGE CXX)
	target_link_libraries(renegade_mission_tests PRIVATE engine_scripting_lua Boost::included_unit_test_framework)
	target_compile_definitions(renegade_mission_tests PRIVATE
		BOOST_TEST_INCLUDED RENEGADE_SCRIPT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/content/scripts")
	add_test(NAME renegade_mission_tests COMMAND renegade_mission_tests)
	set(RENEGADE_INSTALL_DIR "" CACHE PATH "Optional retail install for read-only asset tests")
	if(RENEGADE_INSTALL_DIR)
		add_test(NAME renegade_retail_install COMMAND renegade_content_tests --run_test=install/retail_archives)
		set_tests_properties(renegade_retail_install PROPERTIES ENVIRONMENT "RENEGADE_INSTALL_DIR=${RENEGADE_INSTALL_DIR}" LABELS "retail")
		set_tests_properties(renegade_levels_tests renegade_session_tests renegade_content_tests renegade_cinematics_content_tests PROPERTIES ENVIRONMENT "RENEGADE_INSTALL_DIR=${RENEGADE_INSTALL_DIR}")
		if(TARGET renegade_level_view_tests)
			set_tests_properties(renegade_level_view_tests PROPERTIES ENVIRONMENT "RENEGADE_INSTALL_DIR=${RENEGADE_INSTALL_DIR}")
			if(WIN32 AND FFMPEG_RUNTIME_DIRECTORY)
				set_tests_properties(renegade_level_view_tests PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${FFMPEG_RUNTIME_DIRECTORY}")
			endif()
		endif()
	endif()
endif()
if(COMMAND engine_link_std_everywhere)
	engine_link_std_everywhere("${CMAKE_CURRENT_SOURCE_DIR}")
endif()
