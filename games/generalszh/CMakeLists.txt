cmake_minimum_required(VERSION 4.4.3)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(generalszh LANGUAGES CXX)
	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)
	include(CTest)
	get_filename_component(generalszh_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
	# Standalone modern build: the engine without the legacy game or renderer.
	add_subdirectory("${generalszh_repo_root}/engine" "${CMAKE_CURRENT_BINARY_DIR}/engine")
endif()

# Zero Hour on the modern engine. Layout and rules: migration/README.md.
#   gameplay/<domain>/  Zero Hour-only rules
#   content/            config schemas binding documents to definitions
#   session/            composition root (world, systems, injected definitions)
#   hosts/<host>/       executables
include("${CMAKE_CURRENT_LIST_DIR}/../../engine/cmake/module_library.cmake")
get_filename_component(generalszh_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# The playable game: platform, renderer, assets and video. The root build
# already provides these targets; the standalone build adds them here.
option(GENERALSZH_BUILD_GAME "Build the modern game executable" ${WIN32})
if(GENERALSZH_BUILD_GAME AND NOT TARGET generals_graphics)
	include("${generalszh_repo_root}/cmake/stb.cmake")
	include("${generalszh_repo_root}/cmake/ffmpeg.cmake")
	include("${generalszh_repo_root}/cmake/slang.cmake")
	add_subdirectory("${generalszh_repo_root}/engine/assets" "${CMAKE_CURRENT_BINARY_DIR}/engine/assets")
	add_subdirectory("${generalszh_repo_root}/engine/video" "${CMAKE_CURRENT_BINARY_DIR}/engine/video")
	add_subdirectory("${generalszh_repo_root}/engine/graphics" "${CMAKE_CURRENT_BINARY_DIR}/engine/graphics")
	add_subdirectory("${generalszh_repo_root}/engine/gui" "${CMAKE_CURRENT_BINARY_DIR}/engine/gui")
endif()

# Presentation frame outputs: what presentation systems produce for each frame
# (objects as drawn, and as effects and sound read them). Plain data.
engine_add_module_library(generalszh_presentation_frame
	MODULES
		presentation/effects/light_pulses.cppm presentation/effects/tracers.cppm presentation/effects/scorch_marks.cppm presentation/objects/resources/object_instance.cppm presentation/objects/resources/look_models.cppm presentation/objects/resources/object_frame.cppm
		presentation/objects/components/track_marks.cppm
	DEPENDS engine_ecs engine_gameplay_appearance)

if(GENERALSZH_BUILD_GAME)
	if(NOT TARGET engine_audio_video)
		add_subdirectory("${generalszh_repo_root}/engine/audio/adapters/video" "${CMAKE_CURRENT_BINARY_DIR}/engine/audio/video")
	endif()
	# The in-game interface's views: the control bar layout bound to its view model.
	engine_add_module_library(generalszh_hud_views
		MODULES hud/control_bar_view.cppm hud/generals_powers_view.cppm hud/shortcut_bar_view.cppm hud/observer_panel_view.cppm hud/comm_views.cppm
		DEPENDS generalszh_hud generals_ui_wnd)
	engine_add_module_library(generalszh_shell_views
		MODULES shell/main_menu/main_menu_view.cppm shell/options/options_view.cppm shell/replay/replay_menu_view.cppm shell/dialog/message_box_view.cppm shell/save_load/save_load_view.cppm shell/skirmish/skirmish_view.cppm shell/lan/lan_view.cppm shell/challenge/challenge_view.cppm shell/quit_menu/quit_menu_view.cppm shell/score/score_screen_view.cppm shell/load_screen/load_screen_view.cppm
		DEPENDS generalszh_shell generals_ui_wnd
		TESTS shell/main_menu/main_menu_view.test shell/options/options_view.test shell/dialog/message_box_view.test shell/load_screen/load_screen_view.test
		TEST_DEPENDS generalszh_content_install)
	# The looks' models as presentation data: each look's model entry (parts, turned bones, animation, collision shape),
	# the library of them and the system that loads them (drawing, picking and effects read it).
	engine_add_module_library(generalszh_presentation_models
		MODULES
			presentation/models/model_entry.cppm
			presentation/models/model_library.cppm
			presentation/models/model_library_system.cppm
			presentation/models/pointer_pick_system.cppm
			presentation/models/fire_fx_bone_system.cppm
			presentation/models/scenery_clearing_system.cppm
		DEPENDS generals_graphics generals_assets generalszh_presentation_frame generalszh_presentation_objects generalszh_presentation_interaction engine_ecs
		TESTS presentation/models/model_entry.test)
	# Presentation rendering: adapters from the client world to the graphics renderers.
	engine_add_module_library(generalszh_presentation_rendering
		MODULES
			presentation/rendering/object_rendering.cppm
			presentation/rendering/texture_files.cppm
			presentation/rendering/terrain_rendering.cppm
			presentation/rendering/water_rendering.cppm
			presentation/rendering/particle_rendering.cppm
			presentation/rendering/tracer_rendering.cppm
			presentation/rendering/model_bones.cppm
			presentation/rendering/shroud_pixels.cppm
			presentation/rendering/infantry_lighting.cppm
			presentation/rendering/standing_water.cppm
			presentation/rendering/bridge_models.cppm
			presentation/rendering/bridge_rendering.cppm
			presentation/rendering/road_rendering.cppm
			presentation/rendering/sky_box_rendering.cppm
			presentation/rendering/building_occlusion.cppm
		DEPENDS generals_graphics generals_assets engine_level engine_level_presentation engine_effects_particles generalszh_content generalszh_presentation_frame generalszh_presentation_models
			engine_gameplay_vision generalszh_presentation_effects generalszh_presentation_objects generalszh_content_install generalszh_presentation_roads
		TESTS presentation/rendering/shroud_pixels.test presentation/rendering/infantry_lighting.test presentation/rendering/standing_water.test presentation/rendering/bridge_models.test presentation/rendering/building_occlusion.test
		TEST_DEPENDS generalszh_content_install)
	target_sources(generalszh_presentation_rendering PRIVATE
		presentation/rendering/object_rendering.cpp
		presentation/rendering/water_rendering.cpp
		presentation/rendering/particle_rendering.cpp
		presentation/rendering/model_bones.cpp
		presentation/rendering/sky_box_rendering.cpp)
	set_source_files_properties(presentation/rendering/object_rendering.cpp presentation/rendering/water_rendering.cpp
		presentation/rendering/particle_rendering.cpp presentation/rendering/model_bones.cpp presentation/rendering/sky_box_rendering.cpp PROPERTIES LANGUAGE CXX)
	add_subdirectory(hosts/game)
endif()

# Mounting an install: loose files, then archives in the original load order.
# The test uses a real install when GENERALSZH_INSTALL_DIR is set.
engine_add_module_library(generalszh_content_install
	MODULES content/install/install_mount.cppm content/install/asset_paths.cppm
	DEPENDS engine_filesystem
	TESTS content/install/install_mount.test content/install/asset_paths.test
	TEST_DEPENDS engine_config)
if(TARGET generalszh_content_install_tests)
	target_compile_definitions(generalszh_content_install_tests PRIVATE
		GENERALSZH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
	set_tests_properties(generalszh_content_install_tests PROPERTIES
		PASS_REGULAR_EXPRESSION "No errors detected")
endif()
# Zero Hour content: grammar (embedded), content loading, object catalog.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/content/ini/grammar.json" GRAMMAR_JSON)
configure_file(content/ini/zero_hour_grammar.cppm.in
	"${CMAKE_CURRENT_BINARY_DIR}/generated/zero_hour_grammar.cppm" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS content/ini/grammar.json)
engine_add_module_library(generalszh_content
	MODULES
		"${CMAKE_CURRENT_BINARY_DIR}/generated/zero_hour_grammar.cppm"
		content/loading/content_loader.cppm
		content/objects/kind_of.cppm
		content/objects/object_definition.cppm
		content/objects/module_interfaces.cppm
		content/objects/object_catalog.cppm
		content/objects/model_draw.cppm
		content/objects/model_conditions.cppm
		content/objects/object_status.cppm
		content/objects/model_states.cppm
		content/terrain/terrain_type.cppm
		content/terrain/bridge_content.cppm
		content/terrain/road_content.cppm
		content/locomotors/locomotor_catalog.cppm
		content/water/water_settings.cppm
		content/combat/weapon_bonus_content.cppm
		content/combat/combat_catalog.cppm
		content/combat/cooldown_creation_content.cppm
		content/combat/emp_content.cppm
		content/water/wave_guide_content.cppm
		content/combat/damage_fx_content.cppm
		content/combat/loadout_content.cppm
		content/combat/poison_content.cppm
		content/crates/crate_content.cppm
		content/images/animation_2d.cppm
		content/containment/garrison_points.cppm
		content/containment/parachute_content.cppm
		content/veterancy/create_modules.cppm
		content/locomotors/chassis_look.cppm
		content/locomotors/animation_steering.cppm
		content/upgrades/building_extensions.cppm
		content/loading/game_content.cppm
		content/loading/map_overrides.cppm
		content/images/mapped_image_files.cppm
		content/containment/transport_content.cppm
		content/control_bar/command_catalog.cppm
		content/control_bar/control_bar_scheme.cppm
		content/death/death_content.cppm
		content/physics/physics_content.cppm
		content/lifetime/lifetime_content.cppm
		content/creation/creation_lists.cppm
		content/healing/healing_content.cppm
		content/mines/mine_content.cppm
		content/economy/auto_deposit_content.cppm
		content/powers/special_ability_content.cppm
		content/fire/fire_content.cppm
		content/stealth/stealth_content.cppm
		content/vision/dynamic_clearing_content.cppm
		content/effects/bone_fx_content.cppm
		content/effects/transition_damage_content.cppm
		content/topple/topple_content.cppm
		content/production/production_content.cppm
		content/models/model_rigs.cppm
		content/aircraft/aircraft_content.cppm
		content/global/game_data.cppm
		content/global/language_fonts.cppm
		content/global/draw_group_info.cppm
		content/global/header_templates.cppm
		content/global/radius_decal.cppm
		content/railroad/railroad_content.cppm
		content/containment/combat_drop_content.cppm
		content/containment/railed_transport_content.cppm
		content/global/in_game_ui.cppm
		content/global/mouse.cppm
		content/ai/ai_data.cppm
		content/sciences/science_content.cppm
		content/eva/eva_content.cppm
		content/global/player_templates.cppm
		content/global/challenge_generals.cppm
		content/global/campaigns.cppm
		content/global/videos.cppm
		content/global/multiplayer_settings.cppm
		content/maps/map_cache.cppm
		content/maps/map_strings.cppm
		content/input/command_map.cppm
		content/powers/special_powers.cppm
		content/upgrades/upgrade_content.cppm
		content/slaves/slaved_content.cppm
		content/horde/horde_content.cppm
		content/harvesting/harvest_content.cppm
	DEPENDS engine_core_text engine_config engine_filesystem engine_localization engine_gameplay_vision generalszh_content_install engine_gameplay_movement engine_gameplay_health engine_gameplay_weapons engine_gameplay_combat engine_gameplay_containment engine_gameplay_death engine_gameplay_physics engine_gameplay_healing engine_gameplay_poison engine_gameplay_construction engine_gameplay_construction_work engine_gameplay_fire engine_gameplay_stealth engine_gameplay_topple engine_gameplay_upgrades engine_gameplay_slaves engine_gameplay_horde engine_gameplay_veterancy engine_gameplay_harvesting engine_gameplay_parachute
	TESTS content/objects/object_catalog.test content/water/water_settings.test content/terrain/terrain_type.test content/sciences/science_content.test content/combat/combat_catalog.test content/images/mapped_image_files.test content/powers/special_powers.test content/objects/model_states.test content/healing/healing_content.test content/models/model_rigs.test content/stealth/stealth_content.test content/fire/fire_content.test content/death/death_content.test content/production/production_content.test content/global/game_data.test content/global/player_templates.test content/maps/map_cache.test content/input/command_map.test content/upgrades/upgrade_content.test content/control_bar/command_catalog.test content/ai/ai_data.test content/effects/bone_fx_content.test content/aircraft/aircraft_content.test content/effects/transition_damage_content.test
	TEST_DEPENDS generalszh_content_install engine_level)
if(TARGET generalszh_content_tests)
	set_tests_properties(generalszh_content_tests PROPERTIES PASS_REGULAR_EXPRESSION "No errors detected")
endif()

# Scripting: Zero Hour vocabularies on the generic engine script runtime.
engine_add_module_library(generalszh_scripting
	MODULES
		scripting/core_vocabulary.cppm
		scripting/script_parameters.cppm
		scripting/camera_vocabulary.cppm
		scripting/unit_vocabulary.cppm
		scripting/presentation_vocabulary.cppm
		scripting/match_vocabulary.cppm
		scripting/player_vocabulary.cppm
	DEPENDS engine_scripting engine_level
	TESTS scripting/core_vocabulary.test scripting/unit_vocabulary.test scripting/match_vocabulary.test scripting/player_vocabulary.test
	TEST_DEPENDS generalszh_content_install engine_level)
if(TARGET generalszh_scripting_tests)
	set_tests_properties(generalszh_scripting_tests PROPERTIES PASS_REGULAR_EXPRESSION "No errors detected")
endif()

# A game's setup before it starts (GameInfo): slots, map, options and their options string.
engine_add_module_library(generalszh_session_setup
	MODULES session/setup/game_setup.cppm session/setup/script_qualify.cppm session/setup/skirmish_level.cppm session/setup/match_plan.cppm
		session/setup/replay_body.cppm session/setup/start_positions.cppm session/setup/map_metadata.cppm
	DEPENDS engine_core_text engine_core_serialization generalszh_content engine_level engine_net
	TESTS session/setup/game_setup.test)

# The LAN lobby (LANAPI): the original's datagrams and its lobby, over an injected link.
engine_add_module_library(generalszh_network_lan
	MODULES network/lan/lan_protocol.cppm network/lan/lan_lobby.cppm
	DEPENDS generalszh_session_setup engine_core_text
	TESTS network/lan/lan_lobby.test)

# Front end (MVVM): model and view models are headless; views bind WND layouts.
engine_add_module_library(generalszh_shell
	MODULES
		shell/model/shell_model.cppm
		shell/main_menu/main_menu_view_model.cppm
		shell/options/user_options.cppm
		shell/options/options_view_model.cppm
		shell/credits/credits_view_model.cppm
		shell/dialog/message_box_view_model.cppm
		shell/replay/replay_header.cppm
		shell/replay/replay_menu_view_model.cppm
		shell/save_load/save_game_info.cppm
		shell/save_load/save_game_file.cppm
		shell/save_load/save_load_view_model.cppm
		shell/game_setup/setup_catalog.cppm
		shell/skirmish/skirmish_view_model.cppm
		shell/skirmish/skirmish_tooltips.cppm
		shell/lan/lan_tooltips.cppm
		shell/lan/lan_lobby_view_model.cppm
		shell/lan/lan_game_options_view_model.cppm
		shell/challenge/challenge_view_model.cppm
		shell/quit_menu/quit_menu_view_model.cppm
		shell/score/academy_advice.cppm
		shell/score/score_screen_view_model.cppm
		shell/score/battle_honors.cppm
		shell/intro/intro_sequence.cppm
		shell/intro/load_screen_bitmap.cppm
		shell/load_screen/load_screen_kind.cppm
		shell/load_screen/shell_game_load_screen_view_model.cppm
		shell/load_screen/mission_load_screen_view_model.cppm
		shell/load_screen/multiplayer_load_screen_view_model.cppm
	DEPENDS engine_gui_mvvm engine_config engine_core_text generalszh_session_setup generalszh_network_lan
	TESTS shell/main_menu/main_menu_view_model.test shell/options/options_view_model.test shell/credits/credits_view_model.test
		shell/replay/replay_menu_view_model.test shell/save_load/save_load_view_model.test shell/skirmish/skirmish_view_model.test shell/skirmish/skirmish_tooltips.test shell/lan/lan_tooltips.test shell/lan/lan_view_model.test shell/challenge/challenge_view_model.test shell/quit_menu/quit_menu_view_model.test shell/score/score_screen_view_model.test
		shell/score/battle_honors.test shell/score/academy_advice.test shell/intro/intro_sequence.test shell/intro/load_screen_bitmap.test shell/load_screen/load_screen_view_model.test
	TEST_DEPENDS generalszh_content_install)

if(BUILD_TESTING)
	# Every shipped map through the generic level adapter (needs GENERALSZH_INSTALL_DIR).
	set_source_files_properties(content/install/shipped_maps.test PROPERTIES LANGUAGE CXX)
	add_executable(generalszh_shipped_maps_tests content/install/shipped_maps.test)
	set_property(TARGET generalszh_shipped_maps_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
	target_compile_definitions(generalszh_shipped_maps_tests PRIVATE BOOST_TEST_INCLUDED)
	target_link_libraries(generalszh_shipped_maps_tests PRIVATE generalszh_content_install engine_level Boost::included_unit_test_framework)
	add_test(NAME generalszh_shipped_maps_tests COMMAND generalszh_shipped_maps_tests --log_level=message)
endif()

if(BUILD_TESTING)
	# The Zero Hour INI grammar, checked against the shipped data when present
	# (GENERALSZH_DATA_DIR, default GeneralsMD/Run; skipped otherwise).
	find_package(boost_included_unit_test_framework CONFIG REQUIRED)
	set_source_files_properties(content/ini/grammar.test PROPERTIES LANGUAGE CXX)
	add_executable(generalszh_content_ini_tests content/ini/grammar.test)
	set_property(TARGET generalszh_content_ini_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
	target_compile_definitions(generalszh_content_ini_tests PRIVATE BOOST_TEST_INCLUDED
		GENERALSZH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
		GENERALSZH_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
	target_link_libraries(generalszh_content_ini_tests PRIVATE engine_config Boost::included_unit_test_framework)
	add_test(NAME generalszh_content_ini_tests COMMAND generalszh_content_ini_tests --log_level=message)

	# Migration ledger: every legacy module, INI block/field, script
	# action/condition and subsystem must have a valid row.
	find_package(Python3 COMPONENTS Interpreter REQUIRED)
	add_test(NAME generalszh_migration_ledger
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/migration/tools/ledger.py" check)
	add_test(NAME generalszh_migration_ledger_tool
		COMMAND "${Python3_EXECUTABLE}" -m unittest ledger_test
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/migration/tools")
endif()

# Commands: what players order, as they travel the lockstep command bus.
engine_add_module_library(generalszh_commands
	MODULES commands/game_commands.cppm
	DEPENDS engine_net engine_events engine_ecs engine_core_math_fixed
	TESTS commands/game_commands.test)

# Gameplay: Zero Hour rules as domain algorithms over the ECS world (spawning,
# orders, teams, special powers, level setup); the systems are the engine's.
engine_add_module_library(generalszh_gameplay
	MODULES
		gameplay/objects/resources/object_templates.cppm
		gameplay/teams/resources/team_templates.cppm
		gameplay/world/resources/game_world.cppm
		gameplay/world/resources/solo_play.cppm
		gameplay/world/resources/deselections.cppm
		gameplay/world/components/difficulty_bonus.cppm
		gameplay/crates/components/crate.cppm
		gameplay/crates/components/pilot_seeker.cppm
		gameplay/containment/components/heal_seeker.cppm
		gameplay/containment/components/held_aboard.cppm
		gameplay/teams/components/tech_building.cppm
		gameplay/crates/resources/crates.cppm
		gameplay/objects/algorithms/object_factory.cppm
		gameplay/teams/algorithms/team_states.cppm
		gameplay/ai/resources/ai_players.cppm
		gameplay/ai/algorithms/ai_players.cppm
		gameplay/ai/algorithms/ai_base_building.cppm
		gameplay/ai/algorithms/ai_team_building.cppm
		gameplay/ai/algorithms/ai_superweapon.cppm
		gameplay/sciences/algorithms/general_ranks.cppm
		gameplay/scripts/resources/script_records.cppm
		gameplay/orders/resources/command_bar_overrides.cppm
		gameplay/orders/resources/buildable_overrides.cppm
		gameplay/orders/resources/hotkey_squads.cppm
		gameplay/scripts/components/emptied_watch.cppm
		gameplay/score/resources/score_keepers.cppm
		gameplay/score/algorithms/scoring.cppm
		gameplay/scripts/algorithms/unit_script_orders.cppm
		gameplay/scripts/algorithms/object_counting.cppm
		gameplay/scripts/algorithms/trigger_area_queries.cppm
		gameplay/scripts/algorithms/team_sequences.cppm
		gameplay/scripts/algorithms/command_button_targets.cppm
		gameplay/orders/algorithms/command_buttons.cppm
		gameplay/orders/algorithms/command_button_readiness.cppm
		gameplay/containment/algorithms/parachuting.cppm
		gameplay/creation/algorithms/creation_list_runner.cppm
		gameplay/aircraft/algorithms/airfields.cppm
		gameplay/production/algorithms/team_building.cppm
		gameplay/lifecycle/algorithms/retire_now.cppm
		gameplay/mines/components/minefield_generator.cppm
		gameplay/mines/algorithms/minefields.cppm
		gameplay/mines/components/mine_clearer.cppm
		gameplay/mines/systems/mine_clearing_detail_system.cppm
		gameplay/mines/algorithms/mine_clearing.cppm
		gameplay/construction/components/builder_boredom.cppm
		gameplay/construction/systems/builder_boredom_system.cppm
		gameplay/construction/algorithms/builder_boredom.cppm
		gameplay/ai/components/tunnel_guard.cppm
		gameplay/ai/components/guard.cppm
		gameplay/containment/components/scripted_evacuation.cppm
		gameplay/ai/components/repulsion.cppm
		gameplay/ai/components/attack_squad.cppm
		gameplay/orders/algorithms/unit_orders.cppm
		gameplay/orders/algorithms/repair_orders.cppm
		gameplay/orders/components/formation_move.cppm
		gameplay/orders/systems/formation_speed_system.cppm
		gameplay/orders/algorithms/group_columns.cppm
		gameplay/orders/algorithms/group_formations.cppm
		gameplay/orders/algorithms/group_scatter.cppm
		gameplay/orders/algorithms/group_orders.cppm
		gameplay/orders/algorithms/wander_orders.cppm
		gameplay/teams/algorithms/team_actions.cppm
		gameplay/powers/algorithms/special_power_state.cppm
		gameplay/eva/resources/eva_notices.cppm
		gameplay/production/resources/production_notices.cppm
		gameplay/production/components/cost_modifying.cppm
		gameplay/production/systems/drone_repair_system.cppm
		gameplay/powers/resources/cash_notices.cppm
		gameplay/hacking/components/internet_hack.cppm
		gameplay/battleplans/components/battle_plan.cppm
		gameplay/crates/components/hijacker.cppm
		gameplay/economy/components/warehouse_crippling.cppm
		gameplay/combat/components/battle_bus.cppm
		gameplay/combat/components/weapon_bonus_pulse.cppm
		gameplay/creation/components/ocl_timer.cppm
		gameplay/effects/resources/effect_cues.cppm
		gameplay/combat/components/enemy_near.cppm
		gameplay/combat/components/checkpoint.cppm
		gameplay/containment/components/railed_transport.cppm
		gameplay/containment/systems/railed_transport_systems.cppm
		gameplay/containment/algorithms/railed_transports.cppm
		gameplay/containment/algorithms/transport_riders.cppm
		gameplay/powers/components/leaflet_drop.cppm
		gameplay/combat/components/firestorm.cppm
		gameplay/containment/components/assault_transport.cppm
		gameplay/abilities/components/special_abilities.cppm
		gameplay/abilities/components/special_object.cppm
		gameplay/abilities/components/ability_laser.cppm
		gameplay/abilities/components/sticky_bomb.cppm
		gameplay/abilities/resources/sticky_bomb_cues.cppm
		gameplay/abilities/resources/ability_notices.cppm
		gameplay/abilities/algorithms/ability_setup.cppm
		gameplay/powers/algorithms/power_trigger.cppm
		gameplay/teams/algorithms/defection.cppm
		gameplay/abilities/systems/special_ability_system.cppm
		gameplay/abilities/systems/sticky_bomb_system.cppm
		gameplay/abilities/algorithms/special_objects.cppm
		gameplay/bridges/components/bridge.cppm
		gameplay/bridges/resources/bridge_cues.cppm
		gameplay/bridges/systems/bridge_damage_system.cppm
		gameplay/walls/components/wall_piece.cppm
		gameplay/walls/systems/wall_piece_system.cppm
		gameplay/walls/algorithms/wall_pieces.cppm
		gameplay/world/algorithms/map_properties.cppm
		gameplay/bridges/algorithms/bridges.cppm
		gameplay/walls/algorithms/walls.cppm
		gameplay/waveguide/components/wave_guide.cppm
		gameplay/waveguide/resources/wave_guide_events.cppm
		gameplay/waveguide/algorithms/wave_shape.cppm
		gameplay/waveguide/systems/wave_guide_system.cppm
		gameplay/waveguide/algorithms/wave_guides.cppm
		gameplay/academy/resources/academy_stats.cppm
		gameplay/academy/algorithms/academy_records.cppm
		gameplay/hacking/resources/hack_cues.cppm
		gameplay/hacking/algorithms/hack_orders.cppm
		gameplay/hacking/algorithms/hack_effects.cppm
		gameplay/hacking/systems/internet_hack_system.cppm
		gameplay/hacking/algorithms/hack_events.cppm
		gameplay/battleplans/resources/battle_plan_players.cppm
		gameplay/battleplans/resources/battle_plan_cues.cppm
		gameplay/battleplans/systems/battle_plan_system.cppm
		gameplay/battleplans/algorithms/battle_plan_bonuses.cppm
		gameplay/battleplans/algorithms/battle_plan_events.cppm
		gameplay/crates/systems/hijacker_system.cppm
		gameplay/crates/algorithms/hijacking.cppm
		gameplay/economy/systems/warehouse_crippling_system.cppm
		gameplay/economy/algorithms/warehouse_crippling.cppm
		gameplay/combat/systems/battle_bus_system.cppm
		gameplay/combat/algorithms/battle_bus.cppm
		gameplay/combat/systems/weapon_bonus_pulse_system.cppm
		gameplay/combat/algorithms/weapon_bonus_pulses.cppm
		gameplay/creation/systems/ocl_timer_system.cppm
		gameplay/effects/components/bone_fx.cppm
		gameplay/effects/systems/bone_fx_system.cppm
		gameplay/effects/algorithms/bone_fx_effects.cppm
		gameplay/effects/systems/transition_creation_system.cppm
		gameplay/effects/algorithms/transition_creations.cppm
		gameplay/containment/algorithms/parachute_landings.cppm
		gameplay/effects/components/radius_decal.cppm
		gameplay/effects/resources/radius_decal_looks.cppm
		gameplay/effects/algorithms/radius_decals.cppm
		gameplay/effects/systems/radius_decal_system.cppm
		gameplay/beacons/resources/beacons.cppm
		gameplay/combat_drop/components/combat_drop.cppm
		gameplay/aircraft/components/airfield_healing.cppm
		gameplay/flight_deck/components/flight_deck.cppm
		gameplay/flight_deck/systems/flight_deck_systems.cppm
		gameplay/aircraft/systems/airfield_heal_system.cppm
		gameplay/flight_deck/algorithms/flight_decks.cppm
		gameplay/combat_drop/resources/deferred_orders.cppm
		gameplay/combat_drop/algorithms/combat_drop_orders.cppm
		gameplay/combat_drop/algorithms/rappel_landings.cppm
		gameplay/combat_drop/systems/combat_drop_systems.cppm
		gameplay/beacons/algorithms/beacons.cppm
		gameplay/railroad/components/railcar.cppm
		gameplay/railroad/resources/rail_network.cppm
		gameplay/railroad/algorithms/railroad_motion.cppm
		gameplay/railroad/algorithms/railroad_carriages.cppm
		gameplay/railroad/systems/railroad_system.cppm
		gameplay/railroad/systems/railroad_collision_system.cppm
		gameplay/creation/algorithms/ocl_timers.cppm
		gameplay/containment/systems/assault_transport_system.cppm
		gameplay/containment/algorithms/assault_transports.cppm
		gameplay/combat/algorithms/neutron_blasts.cppm
		gameplay/combat/algorithms/bunker_busters.cppm
		gameplay/combat/systems/enemy_near_system.cppm
		gameplay/combat/systems/checkpoint_system.cppm
		gameplay/powers/algorithms/leaflet_drops.cppm
		gameplay/combat/systems/firestorm_system.cppm
		gameplay/combat/algorithms/firestorms.cppm
		gameplay/stealth/algorithms/disguises.cppm
		gameplay/stealth/algorithms/supply_stealth.cppm
		gameplay/stealth/components/supply_stealth_grant.cppm
		gameplay/stealth/systems/supply_stealth_grant_system.cppm
		gameplay/movement/algorithms/goal_claim_rules.cppm
		gameplay/movement/systems/destination_adjust_system.cppm
		gameplay/movement/systems/move_away_system.cppm
		gameplay/movement/algorithms/unit_settles.cppm
		gameplay/movement/systems/enter_obstacle_system.cppm
		gameplay/movement/systems/goal_claim_system.cppm
		gameplay/abilities/algorithms/special_ability_update.cppm
		gameplay/abilities/components/command_button_hunt.cppm
		gameplay/abilities/systems/command_button_hunt_system.cppm
		gameplay/abilities/algorithms/command_button_hunts.cppm
		gameplay/powers/algorithms/special_power_launch.cppm
		gameplay/powers/algorithms/cash_bounty.cppm
		gameplay/orders/algorithms/command_application.cppm
		gameplay/veterancy/algorithms/veterancy_placement.cppm
		gameplay/combat/components/cleanup_hazard.cppm
		gameplay/combat/algorithms/flares.cppm
		gameplay/combat/algorithms/object_flown_launches.cppm
		gameplay/powers/algorithms/visible_payloads.cppm
		gameplay/powers/resources/spy_vision_catalog.cppm
		gameplay/powers/components/launcher_door.cppm
		gameplay/powers/algorithms/launcher_doors.cppm
		gameplay/powers/systems/launcher_door_system.cppm
		gameplay/powers/components/particle_cannon.cppm
		gameplay/powers/algorithms/power_targeting.cppm
		gameplay/powers/algorithms/shortcut_powers.cppm
		gameplay/production/resources/rally_notices.cppm
		gameplay/production/algorithms/rally_points.cppm
		gameplay/containment/algorithms/player_evacuation.cppm
		gameplay/powers/algorithms/particle_cannons.cppm
		gameplay/powers/systems/particle_cannon_system.cppm
		gameplay/powers/components/spectre_gunship.cppm
		gameplay/powers/algorithms/spectre_gunships.cppm
		gameplay/powers/systems/spectre_gunship_system.cppm
		gameplay/ai/components/mob_member.cppm
		gameplay/ai/systems/mob_member_system.cppm
		gameplay/ai/algorithms/mobs.cppm
		gameplay/ai/algorithms/slave_orders.cppm
		gameplay/ai/resources/retaliation_modes.cppm
		gameplay/ai/algorithms/retaliation.cppm
		gameplay/combat/resources/cooldown_creation_catalog.cppm
		gameplay/combat/components/cooldown_creation.cppm
		gameplay/combat/systems/cooldown_creation_system.cppm
		gameplay/combat/systems/deploy_system.cppm
		gameplay/powers/components/spy_vision.cppm
		gameplay/powers/systems/spy_vision_system.cppm
		gameplay/powers/algorithms/spy_visions.cppm
		gameplay/combat/systems/cleanup_hazard_system.cppm
		gameplay/combat/resources/unmanned_notices.cppm
		gameplay/combat/systems/pilot_kill_system.cppm
		gameplay/scripts/algorithms/object_status_queries.cppm
		gameplay/combat/algorithms/unmanned_vehicles.cppm
		gameplay/combat/algorithms/subdual_changes.cppm
		gameplay/containment/components/initial_payload.cppm
		gameplay/containment/algorithms/initial_payloads.cppm
		gameplay/containment/components/rider_change.cppm
		gameplay/containment/algorithms/rider_changes.cppm
		gameplay/combat/algorithms/common_targets.cppm
		gameplay/combat/algorithms/attack_records.cppm
		gameplay/scripts/algorithms/object_edits.cppm
		gameplay/ai/systems/tunnel_guard_system.cppm
		gameplay/ai/systems/guard_system.cppm
		gameplay/ai/algorithms/guards.cppm
		gameplay/ai/algorithms/tunnel_guards.cppm
		gameplay/ai/systems/repulsion_system.cppm
		gameplay/ai/algorithms/repulsion.cppm
		gameplay/ai/systems/attack_squad_system.cppm
		gameplay/ai/algorithms/attack_squads.cppm
		gameplay/ai/components/team_path_follow.cppm
		gameplay/ai/systems/team_path_follow_system.cppm
		gameplay/ai/components/exact_path_follow.cppm
		gameplay/ai/systems/exact_path_follow_system.cppm
		gameplay/ai/algorithms/team_path_follows.cppm
		gameplay/containment/algorithms/garrisons.cppm
		gameplay/containment/algorithms/put_inside.cppm
		gameplay/teams/algorithms/reinforcements.cppm
		gameplay/containment/algorithms/tunnels.cppm
		gameplay/construction/algorithms/selling.cppm
		gameplay/construction/algorithms/build_legality.cppm
		gameplay/construction/algorithms/building.cppm
		gameplay/construction/algorithms/construction_cancel.cppm
		gameplay/construction/components/rebuild_hole.cppm
		gameplay/construction/algorithms/rebuild_holes.cppm
		gameplay/production/algorithms/build_cost.cppm
		gameplay/orders/algorithms/view_targets.cppm
		gameplay/production/algorithms/unit_queue.cppm
		gameplay/orders/algorithms/command_availability.cppm
		gameplay/orders/algorithms/build_tooltip_facts.cppm
		gameplay/orders/algorithms/move_hints.cppm
		gameplay/world/algorithms/difficulty_bonuses.cppm
		gameplay/world/algorithms/level_setup.cppm
		gameplay/world/algorithms/position_search.cppm
		gameplay/world/algorithms/navigation_setup.cppm
		gameplay/world/resources/water_changes.cppm
		gameplay/world/resources/map_scenery.cppm
		gameplay/combat/algorithms/shot_creation_lists.cppm
		gameplay/production/algorithms/production_completion.cppm
		gameplay/fire/algorithms/fire_embers.cppm
		gameplay/spawning/algorithms/spawn_completion.cppm
		gameplay/crates/algorithms/crate_pickup_step.cppm
		gameplay/mines/algorithms/minefield_deaths.cppm
		gameplay/topple/algorithms/topple_stumps.cppm
		gameplay/upgrades/algorithms/upgrade_creations.cppm
		gameplay/death/algorithms/death_aftermath.cppm
		gameplay/scripts/algorithms/value_groups.cppm
		gameplay/world/resources/music_progress.cppm
		gameplay/world/resources/chat_inbox.cppm
		gameplay/world/resources/match_rules.cppm
		gameplay/world/algorithms/water_levels.cppm
		gameplay/world/algorithms/starting_objects.cppm
		gameplay/appearance/components/steering_look.cppm
		gameplay/appearance/components/building_extensions.cppm
		gameplay/appearance/systems/appearance_system.cppm
		gameplay/appearance/systems/steering_look_system.cppm
		gameplay/appearance/systems/panic_look_system.cppm
		gameplay/appearance/systems/extension_look_system.cppm
		gameplay/combat/systems/projectile_body_system.cppm
		gameplay/upgrades/resources/upgrade_effects.cppm
		gameplay/upgrades/components/command_set_override.cppm
		gameplay/upgrades/algorithms/grant_upgrade.cppm
		gameplay/upgrades/algorithms/research.cppm
		gameplay/upgrades/algorithms/upgrade_affects.cppm
		gameplay/production/systems/production_refund_system.cppm
		gameplay/production/systems/production_allowance_system.cppm
		gameplay/upgrades/systems/upgrade_effect_system.cppm
		gameplay/upgrades/systems/research_completion_system.cppm
		gameplay/crates/systems/crate_touch_system.cppm
		gameplay/crates/systems/pilot_seek_system.cppm
		gameplay/containment/systems/heal_seek_system.cppm
		gameplay/containment/systems/scripted_evacuation_system.cppm
		gameplay/teams/systems/tech_building_system.cppm
		gameplay/crates/algorithms/crate_rules.cppm
		gameplay/crates/algorithms/car_bombs.cppm
		gameplay/crates/algorithms/sabotage.cppm
	DEPENDS generalszh_content generalszh_commands engine_level engine_gameplay_movement engine_gameplay_teams engine_gameplay_identity engine_gameplay_combat engine_gameplay_containment engine_gameplay_delivery engine_gameplay_lifecycle engine_gameplay_appearance engine_gameplay_death engine_gameplay_physics engine_gameplay_lifetime engine_gameplay_healing engine_gameplay_poison engine_gameplay_construction engine_gameplay_construction_work engine_gameplay_fire engine_gameplay_stealth engine_gameplay_topple engine_gameplay_propaganda engine_gameplay_mines engine_gameplay_emp engine_gameplay_collision engine_gameplay_blocking engine_gameplay_production engine_gameplay_aircraft engine_gameplay_upgrades engine_gameplay_slaves engine_gameplay_status engine_gameplay_veterancy engine_gameplay_horde engine_gameplay_harvesting engine_gameplay_docking engine_gameplay_loadout engine_gameplay_sciences engine_gameplay_powers engine_gameplay_parachute engine_gameplay_match engine_gameplay_radar engine_gameplay_vision engine_gameplay_areas)

# Presentation: what the player sees and hears of the game (never read by
# the simulation). Audio: the game's sound content and files on the engine mixer.
if(TARGET engine_audio_ffmpeg)
	engine_add_module_library(generalszh_presentation_audio
		MODULES
			presentation/audio/audio_content.cppm
			presentation/audio/sound_files.cppm
		DEPENDS engine_audio engine_audio_ffmpeg generalszh_content
		TESTS presentation/audio/audio_content.test
		TEST_DEPENDS generalszh_content_install)
	if(TARGET generalszh_presentation_audio_tests)
		set_tests_properties(generalszh_presentation_audio_tests PROPERTIES PASS_REGULAR_EXPRESSION "No errors detected")
	endif()
endif()

# Presentation effects: particle systems and FX lists on the engine's particle world.
engine_add_module_library(generalszh_presentation_effects
	MODULES
		presentation/effects/laser_looks.cppm
		presentation/effects/uplink_looks.cppm
		presentation/effects/effects_content.cppm
		presentation/effects/fx_playback.cppm
		presentation/effects/damage_effects.cppm
		presentation/effects/heat_haze.cppm
		presentation/effects/particle_drawing.cppm
		presentation/effects/weather_settings.cppm
		presentation/effects/snow.cppm
		presentation/effects/sky_box.cppm
		presentation/animation/tread_roll.cppm
	DEPENDS engine_effects_particles generalszh_content generalszh_presentation_frame
	TESTS presentation/effects/effects_content.test presentation/animation/tread_roll.test presentation/effects/snow.test presentation/effects/sky_box.test
	TEST_DEPENDS generalszh_content_install)
if(TARGET generalszh_presentation_effects_tests)
	set_tests_properties(generalszh_presentation_effects_tests PROPERTIES PASS_REGULAR_EXPRESSION "No errors detected")
endif()

# Session: the composition root that owns the world, systems, scheduler and scripts.
engine_add_module_library(generalszh_session
	MODULES session/session_view.cppm session/script_bridge.cppm session/session.cppm session/lockstep_session.cppm
		session/composition/lifecycle.cppm
		session/composition/spatial.cppm
		session/composition/identity.cppm
		session/composition/powers.cppm
		session/composition/death.cppm
		session/composition/containment.cppm
		session/composition/radar.cppm
		session/composition/vision.cppm
		session/composition/health.cppm
		session/composition/construction.cppm
		session/composition/abilities.cppm
		session/composition/ai.cppm
		session/composition/combat.cppm
		session/composition/appearance.cppm
		session/composition/scripts.cppm
		session/composition/world.cppm
		session/composition/movement.cppm
		session/composition/status.cppm
		session/composition/production.cppm
		session/composition/navigation.cppm
		session/composition/poison.cppm
		session/composition/bridges.cppm
		session/composition/hacking.cppm
		session/composition/battleplans.cppm
		session/composition/crates.cppm
		session/composition/economy.cppm
		session/composition/stealth.cppm
		session/composition/creation.cppm
		session/composition/effects.cppm
		session/composition/railroad.cppm
		session/composition/combat_drop.cppm
		session/composition/flight_deck.cppm
		session/composition/aircraft.cppm
		session/composition/weapons.cppm
		session/composition/upgrades.cppm
		session/composition/veterancy.cppm
		session/composition/horde.cppm
		session/composition/slaves.cppm
		session/composition/parachute.cppm
		session/composition/match.cppm
		session/composition/collision.cppm
		session/composition/teams.cppm
		session/composition/delivery.cppm
		session/composition/physics.cppm
		session/composition/lifetime.cppm
		session/composition/healing.cppm
		session/composition/fire.cppm
		session/composition/propaganda.cppm
		session/composition/mines.cppm
		session/composition/emp.cppm
		session/composition/tick_events.cppm
		session/composition/simulation_setup.cppm
		session/composition/simulation.cppm
		session/composition/areas.cppm
		session/composition/topple.cppm
		session/composition/wave_guides.cppm
		session/composition/blocking.cppm
		session/composition/docking.cppm
		session/composition/harvesting.cppm
		session/composition/loadout.cppm
	DEPENDS generalszh_gameplay generalszh_content generalszh_scripting engine_gameplay_movement engine_gameplay_teams engine_gameplay_identity engine_gameplay_combat engine_gameplay_containment engine_gameplay_delivery engine_gameplay_lifecycle engine_gameplay_death engine_gameplay_physics engine_gameplay_lifetime engine_gameplay_healing engine_gameplay_poison engine_gameplay_construction engine_gameplay_construction_work engine_gameplay_fire engine_gameplay_stealth engine_gameplay_topple engine_gameplay_propaganda engine_gameplay_mines engine_gameplay_emp engine_gameplay_collision engine_gameplay_blocking engine_gameplay_production engine_gameplay_economy engine_gameplay_aircraft generalszh_commands engine_net engine_gameplay_match
	TESTS session/session.test session/determinism_guards.test session/transition_damage.test session/helicopters.test session/jets.test session/railroad.test session/combat_drop.test session/soak.test session/flight_deck.test session/campaign_scan.test session/water_levels.test session/waypoint_powers.test session/music_progress.test session/checkpoints.test session/payload_strikes.test session/railed_transport.test session/garrison_exits.test session/skirmish_start.test session/research.test session/lockstep.test session/crates.test session/create_modules.test session/object_looks.test session/team_states.test session/cash_powers.test session/special_abilities.test session/bridges.test session/walls.test session/unmanned_vehicles.test session/retaliation.test session/object_status.test session/tunnel_guards.test session/script_objects.test session/reinforcements.test session/guards.test session/attacks.test session/drones.test session/movement.test session/unit_blocking.test session/dam_break.test session/academy.test session/team_path_follow.test session/view_keys.test session/waypoints.test
	TEST_DEPENDS generalszh_content_install engine_level generalszh_session_setup generalszh_hud)
target_sources(generalszh_session PRIVATE session/session_checkpoint.cpp session/client_match.cpp)
set_source_files_properties(session/session_checkpoint.cpp session/client_match.cpp PROPERTIES LANGUAGE CXX)
# The in-game interface (the original's ControlBar and InGameUI) as headless view models over the session's view.
engine_add_module_library(generalszh_hud
	MODULES hud/hot_keys.cppm hud/build_refusal.cppm hud/science_buttons.cppm hud/idle_workers.cppm hud/control_bar_state.cppm hud/control_bar_portrait.cppm hud/control_bar_view_model.cppm hud/generals_powers.cppm hud/superweapon_timers.cppm hud/shortcut_bar.cppm hud/match_record.cppm hud/observer_panel.cppm hud/build_tooltip.cppm hud/control_bar_scheme_look.cppm hud/control_bar_stage.cppm hud/in_game_chat.cppm hud/diplomacy.cppm
	DEPENDS generalszh_session engine_gui_mvvm generalszh_commands engine_gameplay_sciences engine_gameplay_powers generalszh_presentation_hud generalszh_shell
	TESTS hud/control_bar_view_model.test hud/hot_keys.test hud/control_bar_portrait.test hud/control_bar_multi_select.test hud/idle_workers.test hud/generals_powers.test hud/superweapon_timers.test hud/shortcut_bar.test hud/match_record.test hud/observer_panel.test hud/build_tooltip.test hud/control_bar_scheme_look.test hud/control_bar_stage.test hud/in_game_chat.test hud/diplomacy.test
	TEST_DEPENDS generalszh_content_install engine_level generalszh_session_setup)

# Presentation objects: look state on the simulation's own entities (side-table
# components), moved on by stateless presentation systems on real frame time.
engine_add_module_library(generalszh_presentation_objects
	MODULES
		presentation/objects/components/vehicle_motion.cppm
		presentation/objects/components/object_presentation.cppm
		presentation/objects/components/effect_attachments.cppm
		presentation/objects/resources/presentation_resources.cppm
		presentation/objects/resources/look_catalog.cppm
		presentation/objects/resources/floating_texts.cppm
		presentation/objects/systems/vehicle_motion_systems.cppm
		presentation/objects/algorithms/barrel_placement.cppm
		presentation/objects/algorithms/emp_look.cppm
		presentation/objects/algorithms/emp_sparks.cppm
		presentation/objects/algorithms/neutron_jitter.cppm
		presentation/objects/algorithms/tint_envelope.cppm
		presentation/objects/systems/disable_presentation_systems.cppm
		presentation/objects/systems/rotor_wash_system.cppm
		presentation/objects/systems/repair_weld_system.cppm
		presentation/objects/systems/jet_touchdown_sound_system.cppm
		presentation/objects/systems/spectre_strafe_system.cppm
		presentation/objects/systems/fade_presentation_system.cppm
		presentation/objects/components/object_icons.cppm
		presentation/objects/algorithms/object_icon_layout.cppm
		presentation/objects/systems/object_icon_systems.cppm
		presentation/objects/algorithms/flash_phases.cppm
		presentation/objects/systems/ability_presentation_systems.cppm
		presentation/objects/systems/script_flash_systems.cppm
		presentation/objects/systems/object_presentation_systems.cppm
		presentation/objects/systems/effect_attachment_systems.cppm
		presentation/objects/systems/detector_ping_systems.cppm
		presentation/objects/components/hit_fx.cppm
		presentation/objects/algorithms/chassis_motion.cppm
		presentation/objects/algorithms/model_state_changes.cppm
		presentation/objects/systems/chassis_systems.cppm
		presentation/objects/systems/hit_fx_systems.cppm
		presentation/objects/systems/harvest_presentation_systems.cppm
		presentation/objects/resources/world_animations.cppm
		presentation/objects/resources/breeze.cppm
		presentation/objects/resources/cloud_layer.cppm
		presentation/objects/resources/detail_settings.cppm
		presentation/objects/algorithms/cloud_drift.cppm
		presentation/objects/systems/cloud_drift_system.cppm
		presentation/objects/resources/dynamic_lights.cppm
		presentation/objects/algorithms/track_laying.cppm
		presentation/objects/systems/track_mark_systems.cppm
		presentation/objects/resources/tree_breeze.cppm
		presentation/objects/algorithms/tree_breeze_sway.cppm
		presentation/objects/systems/tree_breeze_systems.cppm
		presentation/objects/components/tree_bend.cppm
		presentation/objects/algorithms/tree_bending.cppm
		presentation/objects/systems/tree_bend_systems.cppm
		presentation/objects/resources/scenery.cppm
		presentation/objects/algorithms/scenery_setup.cppm
		presentation/objects/systems/scenery_systems.cppm
		presentation/objects/algorithms/debris_animation.cppm
		presentation/objects/systems/debris_systems.cppm
		presentation/objects/systems/dynamic_light_systems.cppm
		presentation/objects/systems/tracer_systems.cppm
		presentation/objects/systems/snow_system.cppm
		presentation/objects/systems/promotion_presentation_systems.cppm
		presentation/objects/systems/crate_presentation_systems.cppm
		presentation/objects/systems/cash_presentation_systems.cppm
		presentation/objects/algorithms/vehicle_motion_setup.cppm
		presentation/objects/algorithms/look_setup.cppm
		presentation/objects/algorithms/look_models.cppm
		presentation/objects/algorithms/presentation_schedule.cppm
		presentation/objects/algorithms/tick_effects.cppm
		presentation/objects/resources/placement_ghosts.cppm
		presentation/objects/systems/placement_ghost_system.cppm
		presentation/objects/resources/rally_point_markers.cppm
		presentation/objects/systems/rally_point_marker_system.cppm
		presentation/objects/resources/move_hints.cppm
		presentation/objects/systems/move_hint_system.cppm
		presentation/objects/resources/waypoint_paths.cppm
		presentation/objects/algorithms/rally_lines.cppm
		presentation/objects/systems/waypoint_path_system.cppm
		presentation/objects/resources/radius_cursor.cppm
		presentation/objects/systems/radius_cursor_system.cppm
		presentation/objects/systems/radius_decal_view_system.cppm
		presentation/objects/systems/rope_view_systems.cppm
		presentation/objects/components/uplink_effects.cppm
		presentation/objects/components/beacon_look.cppm
		presentation/objects/algorithms/laser_beams.cppm
		presentation/objects/algorithms/draw_bones.cppm
		presentation/objects/systems/uplink_systems.cppm
		presentation/objects/systems/projectile_stream_systems.cppm
		presentation/objects/systems/firestorm_presentation_systems.cppm
		presentation/objects/systems/bone_fx_presentation_systems.cppm
		presentation/objects/resources/bridge_art.cppm
		presentation/objects/components/bridge_look.cppm
		presentation/objects/algorithms/bridge_geometry.cppm
		presentation/objects/systems/bridge_look_system.cppm
		presentation/objects/components/wave_guide_effects.cppm
		presentation/objects/algorithms/wave_sprays.cppm
		presentation/objects/systems/wave_guide_effect_system.cppm
		presentation/objects/algorithms/bridge_radar.cppm
	DEPENDS generalszh_presentation_effects generalszh_presentation_frame generalszh_session generalszh_content engine_ecs generalszh_presentation_interaction
	TESTS presentation/objects/vehicle_motion.test presentation/objects/emp_look.test presentation/objects/part_looks.test presentation/objects/flash_phases.test presentation/objects/object_icons.test presentation/objects/uplink_effects.test presentation/objects/projectile_streams.test presentation/objects/fire_effects.test presentation/objects/radius_cursor.test presentation/objects/rope_views.test presentation/objects/infantry_states.test presentation/objects/construction_scaffold.test presentation/objects/rally_point_marker.test presentation/objects/move_hints.test presentation/objects/detail_settings.test presentation/objects/cloud_layer.test presentation/objects/scenery.test presentation/objects/bridge_looks.test presentation/objects/wave_guide_effects.test
	TEST_DEPENDS generalszh_content_install engine_level generalszh_session_setup generalszh_presentation_models)

# Presentation hud: the in-game interface's own drawing geometry (the original's W3D control bar draws), in pixels.
engine_add_module_library(generalszh_presentation_hud
	MODULES
		presentation/hud/algorithms/power_bar.cppm
		presentation/hud/algorithms/experience_bar.cppm
		presentation/hud/resources/in_game_messages.cppm
		presentation/hud/algorithms/message_list.cppm
		presentation/hud/resources/eva_state.cppm
		presentation/hud/algorithms/eva_queue.cppm
		presentation/hud/algorithms/superweapon_list.cppm
		presentation/hud/resources/radar_events.cppm
		presentation/hud/resources/money_feedback.cppm
		presentation/hud/resources/radar_sounds.cppm
		presentation/hud/algorithms/radar_event_rules.cppm
		presentation/hud/algorithms/radar_display.cppm
		presentation/hud/resources/military_caption.cppm
		presentation/hud/algorithms/military_caption_typing.cppm
		presentation/hud/resources/screen_fade.cppm
		presentation/hud/algorithms/screen_fade_steps.cppm
		presentation/hud/resources/named_timers.cppm
		presentation/hud/algorithms/named_timer_lines.cppm
		presentation/hud/resources/cameo_flashes.cppm
		presentation/hud/algorithms/cameo_flash_steps.cppm
		presentation/hud/resources/cinematic_text.cppm
		presentation/hud/algorithms/cinematic_text_layout.cppm
		presentation/hud/resources/popup_message.cppm
		presentation/hud/algorithms/popup_message_layout.cppm
		presentation/hud/algorithms/control_bar_timers.cppm
		presentation/hud/algorithms/language_font_choice.cppm
		presentation/hud/algorithms/in_game_ui_layout.cppm
		presentation/hud/algorithms/screen_capture.cppm
		presentation/hud/algorithms/replay_fast_forward.cppm
	DEPENDS engine_ecs generalszh_content engine_gameplay_powers
	TESTS presentation/hud/power_bar.test presentation/hud/experience_bar.test presentation/hud/message_list.test presentation/hud/eva_queue.test presentation/hud/radar_event_rules.test presentation/hud/radar_display.test presentation/hud/screen_capture.test
		presentation/hud/military_caption.test presentation/hud/screen_fade.test
		presentation/hud/named_timers.test presentation/hud/script_interface.test presentation/hud/language_fonts.test presentation/hud/in_game_ui.test)

# Presentation camera: what scripts do to the view beyond moving it (the original's camera shakers and the motion blur
# filter's timing), as resources and free functions the host applies to its camera.
# Presentation roads: the map's roads as the road buffer builds them (W3DRoadBuffer), as a resource the renderer draws.
engine_add_module_library(generalszh_presentation_roads
	MODULES
		presentation/roads/resources/road_geometry.cppm
		presentation/roads/algorithms/road_network.cppm
	DEPENDS engine_ecs engine_level generalszh_content
	TESTS presentation/roads/roads.test
	TEST_DEPENDS generalszh_content_install)
if(TARGET generalszh_presentation_roads_tests)
	set_tests_properties(generalszh_presentation_roads_tests PROPERTIES PASS_REGULAR_EXPRESSION "No errors detected")
endif()

engine_add_module_library(generalszh_presentation_camera
	MODULES
		presentation/camera/resources/camera_shakers.cppm
		presentation/camera/algorithms/camera_shaking.cppm
		presentation/camera/resources/view_filter.cppm
		presentation/camera/algorithms/motion_blur_steps.cppm
		presentation/camera/algorithms/screen_filters.cppm
		presentation/camera/algorithms/selection_focus.cppm
		presentation/camera/algorithms/camera_slave.cppm
		presentation/camera/algorithms/camera_keys.cppm
	DEPENDS engine_ecs
	TESTS presentation/camera/camera_effects.test presentation/camera/camera_keys.test)

# Presentation interaction: the player's selection and orders (the original's
# SelectionTranslator / CommandTranslator), as rules, side tables and systems.
engine_add_module_library(generalszh_presentation_interaction
	MODULES
		presentation/interaction/algorithms/selection_rules.cppm
		presentation/interaction/components/selected.cppm
		presentation/interaction/resources/interaction_resources.cppm
		presentation/interaction/resources/unit_voice_cues.cppm
		presentation/interaction/resources/academy_client_records.cppm
		presentation/interaction/algorithms/unit_voices.cppm
		presentation/interaction/systems/interaction_systems.cppm
		presentation/interaction/resources/build_placement.cppm
		presentation/interaction/systems/build_placement_system.cppm
		presentation/interaction/algorithms/selection_setup.cppm
		presentation/interaction/algorithms/rule_queries.cppm
		presentation/interaction/resources/mouse_tooltip.cppm
		presentation/interaction/algorithms/mouse_tooltip.cppm
		presentation/interaction/algorithms/mouseover_names.cppm
		presentation/interaction/systems/mouse_tooltip_system.cppm
		presentation/interaction/algorithms/meta_events.cppm
		presentation/interaction/algorithms/hotkey_teams.cppm
		presentation/interaction/algorithms/select_keys.cppm
	DEPENDS engine_ecs generalszh_content generalszh_session generalszh_commands engine_gameplay_spatial engine_gameplay_identity engine_gameplay_health engine_gameplay_weapons engine_gameplay_slaves engine_gameplay_teams engine_gameplay_appearance generalszh_gameplay
	TESTS presentation/interaction/interaction.test presentation/interaction/mouse_tooltip.test presentation/interaction/meta_events.test presentation/interaction/hotkey_teams.test presentation/interaction/unit_voices.test presentation/interaction/select_keys.test)

# Presentation sound: what the player hears, as side tables and stateless
# systems in presentation's frame schedule (sound playback needs the audio stack).
if(TARGET generalszh_presentation_audio)
	engine_add_module_library(generalszh_presentation_sound
		MODULES
			presentation/audio/components/sound_loops.cppm
			presentation/audio/resources/audio_resources.cppm
			presentation/audio/systems/audio_systems.cppm
			presentation/audio/systems/unit_voice_system.cppm
			presentation/audio/systems/wave_guide_sound_system.cppm
			presentation/audio/algorithms/audio_schedule.cppm
		DEPENDS generalszh_presentation_audio generalszh_presentation_objects generalszh_presentation_interaction engine_audio engine_ecs engine_gameplay_construction engine_gameplay_upgrades
		TESTS presentation/audio/audio_systems.test)
	# Presentation scripted: what the local player's scripts set for their presentation (ScriptEngine's client-side
	# actions), applied to the presentation's resources and side tables.
	engine_add_module_library(generalszh_presentation_scripted
		MODULES
			presentation/scripted/resources/scripted_presentation.cppm
			presentation/scripted/algorithms/client_script_effects.cppm
			presentation/scripted/algorithms/camera_script_effects.cppm
		DEPENDS generalszh_presentation_objects generalszh_presentation_hud generalszh_presentation_audio generalszh_presentation_sound generalszh_presentation_interaction generalszh_presentation_camera engine_camera generalszh_scripting generalszh_session engine_core_serialization
		TESTS presentation/scripted/scripted_presentation.test)
	# The in-game messages' once-a-tick system (it plays their sound).
	engine_add_module_library(generalszh_presentation_hud_messages
		MODULES presentation/hud/systems/in_game_message_system.cppm presentation/hud/systems/eva_system.cppm presentation/hud/systems/production_presentation_system.cppm presentation/hud/systems/radar_event_system.cppm presentation/hud/systems/overcharge_notice_system.cppm presentation/hud/systems/rally_notice_system.cppm presentation/hud/systems/construction_notice_system.cppm presentation/hud/systems/military_caption_system.cppm presentation/hud/systems/screen_fade_system.cppm presentation/hud/systems/beacon_system.cppm presentation/hud/systems/money_sound_system.cppm presentation/hud/systems/radar_sound_system.cppm
		DEPENDS generalszh_presentation_hud generalszh_presentation_sound engine_gameplay_match engine_gameplay_identity generalszh_gameplay engine_gameplay_economy engine_gameplay_production engine_gameplay_status engine_gameplay_construction engine_gameplay_radar engine_gameplay_teams
		TESTS presentation/hud/eva_system.test presentation/hud/beacon_system.test presentation/hud/unit_voice_system.test
		TEST_DEPENDS generalszh_content_install engine_level generalszh_session_setup generalszh_commands)
	# The interface's overlay as drawn this frame (InGameUI::postDraw, each Drawable's UI): the frame packet and its
	# extraction from the world.
	engine_add_module_library(generalszh_presentation_overlay
		MODULES
			presentation/hud/resources/in_game_overlay.cppm
			presentation/hud/algorithms/overlay_extract.cppm
		DEPENDS generalszh_hud generalszh_presentation_hud generalszh_presentation_objects generalszh_presentation_interaction generalszh_session generalszh_content engine_ecs engine_gameplay_containment
		TESTS presentation/hud/overlay_extract.test)
	# The presentation's composition root: per domain, which resources the presentation keeps in the simulation's
	# world, what goes with an object that goes, and which systems run once a tick and once a frame.
	if(TARGET generalszh_presentation_models)
		engine_add_module_library(generalszh_presentation_composition
			MODULES
				presentation/composition/objects.cppm
				presentation/composition/hud.cppm
				presentation/composition/interaction.cppm
				presentation/composition/audio.cppm
				presentation/composition/schedules.cppm
			DEPENDS generalszh_presentation_roads generalszh_presentation_overlay generalszh_presentation_models generalszh_presentation_objects generalszh_presentation_effects generalszh_presentation_hud generalszh_presentation_hud_messages generalszh_presentation_camera generalszh_presentation_audio generalszh_presentation_sound generalszh_presentation_interaction generalszh_content engine_gameplay_teams engine_effects_particles generalszh_session engine_ecs)
	endif()
endif()

# Every C++ target may `import std;` (see engine/cmake/std_module.cmake).
if(COMMAND engine_link_std_everywhere)
	engine_link_std_everywhere("${CMAKE_CURRENT_SOURCE_DIR}")
endif()
