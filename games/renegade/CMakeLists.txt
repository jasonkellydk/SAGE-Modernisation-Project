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
option(RENEGADE_BUILD_GAME "Build the graphical retail Renegade host" ${WIN32})
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
engine_add_module_library(renegade_session
	MODULES session/session.cppm
	DEPENDS renegade_defense engine_net_authority
	TESTS gameplay/defense/defense.test)
engine_add_module_library(renegade_content
	MODULES content/install/install_mount.cppm content/armor/armor_catalog.cppm content/input/schema.cppm content/input/profile_catalog.cppm content/presentation/input_configuration.cppm content/presentation/input_profile_store.cppm
	DEPENDS engine_filesystem engine_config engine_platform engine_core_serialization renegade_defense
	TESTS content/install/install_mount.test content/armor/armor_catalog.test content/input/configuration.test content/input/profile_catalog.test content/presentation/input_profile_store.test
	TEST_DEPENDS engine_core_serialization)
engine_add_module_library(renegade_hud
	MODULES hud/vitals_view_model.cppm
	DEPENDS engine_gui_w3d engine_gameplay_health_components
	TESTS hud/vitals_view_model.test)
engine_add_module_library(renegade_menu
	MODULES presentation/menu/main_menu.cppm presentation/menu/intro.cppm presentation/menu/navigation.cppm presentation/menu/audio_settings.cppm presentation/menu/audio_preview.cppm presentation/menu/audio_preferences.cppm presentation/menu/menu_entry.cppm presentation/menu/movie_gallery.cppm presentation/menu/video_settings.cppm presentation/menu/performance_settings.cppm presentation/menu/controls_settings.cppm presentation/menu/control_profiles.cppm presentation/menu/multiplayer_options.cppm
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
		MODULES content/presentation/asset_paths.cppm content/presentation/strings.cppm content/presentation/style.cppm content/presentation/credits.cppm presentation/menu/movie_audio.cppm
		DEPENDS engine_assets_filesystem engine_config engine_audio_video
		TESTS content/presentation/strings.test content/presentation/style.test content/presentation/credits.test presentation/menu/movie_audio.test ${renegade_retail_intro_tests}
		TEST_DEPENDS renegade_content engine_gui_w3d generals_video)
	if(TARGET renegade_menu_assets_tests)
		set_tests_properties(renegade_menu_assets_tests PROPERTIES ENVIRONMENT "RENEGADE_INSTALL_DIR=${RENEGADE_INSTALL_DIR}")
		if(WIN32 AND FFMPEG_RUNTIME_DIRECTORY)
			set_tests_properties(renegade_menu_assets_tests PROPERTIES
				ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${FFMPEG_RUNTIME_DIRECTORY}")
		endif()
	endif()
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
	endif()
endif()
if(COMMAND engine_link_std_everywhere)
	engine_link_std_everywhere("${CMAKE_CURRENT_SOURCE_DIR}")
endif()
