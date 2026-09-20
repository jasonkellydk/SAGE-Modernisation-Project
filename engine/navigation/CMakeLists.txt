cmake_minimum_required(VERSION 3.28)

# Standalone configuration keeps characterization tests free of game/SDK dependencies.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(engine_navigation LANGUAGES CXX)
    set(NAVIGATION_STANDALONE ON)
endif()

option(ENGINE_NAVIGATION_BUILD_TESTS "Build navigation characterization tests" ${NAVIGATION_STANDALONE})

add_library(engine_navigation STATIC)
target_sources(engine_navigation PUBLIC
    FILE_SET CXX_MODULES FILES
        navigation.cppm
        costs.cppm
        diagnostics/frame_capture.cppm
        path/smoothing.cppm
        path/reconstruction.cppm
        querymemo.cppm
        search_workspace.cppm
        search/route_search.cppm
        search/hpa_route.cppm
        search/cluster_topology.cppm
        search/nearest_goal_bound.cppm
        search/unsigned_divisor.cppm
        search/cell_query_cache.cppm
        search/graph/weighted_steps.cppm
        search/graph/captured_weighted.cppm
        search/graph/captured_flee.cppm
        search/graph/destination_rank.cppm
        dynamic_request_queue.cppm
        scheduling/deadline_queue.cppm
        scheduling/continuation.cppm
        spatial/contact_pairs.cppm
        spatial/collision/cylinder_contact.cppm
        spatial/query/visit_epoch.cppm
        spatial/storage/contact_pair_pool.cppm
        spatial/storage/contact_queue.cppm
        topology/terrain_slope.cppm
        topology/reachability_cache.cppm
        topology/footprint_reachability.cppm
        movement/start_escape.cppm
        movement/following/route_geometry.cppm
        movement/following/route_target.cppm
        movement/corridor/phase_line.cppm
        movement/goal_projection.cppm
        movement/formation_layout.cppm
        movement/occupancy_policy.cppm
        movement/terrain_policy.cppm
        movement/classification/weighted_cell.cppm
        movement/destination/reservations.cppm
        movement/destination/flee_goal.cppm
        movement/corridor/clearance.cppm
        movement/corridor/yield_paths.cppm
        movement/snapshot/occupants.cppm
        movement/snapshot/cells.cppm
        movement/relationship_query.cppm
)
target_include_directories(engine_navigation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_features(engine_navigation PUBLIC cxx_std_23)
set_property(TARGET engine_navigation PROPERTY CXX_SCAN_FOR_MODULES ON)
if(TARGET core_config)
    target_link_libraries(engine_navigation PUBLIC core_config)
endif()

# Attach the game-facing module after the GeneralsMD dependency targets exist.
# The adapter uses the game's headers and memory policy.
function(engine_navigation_attach_generalsmd game_target)
    add_library(engine_navigation_generalsmd STATIC)
    target_sources(engine_navigation_generalsmd PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}"
        FILES "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/pathfinder.cppm"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/path/path.cppm"
    )
    set_source_files_properties("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/pathfinder.cppm"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/path/path.cppm"
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON
    )
    get_target_property(game_includes ${game_target} INCLUDE_DIRECTORIES)
    target_include_directories(engine_navigation_generalsmd PRIVATE
        ${game_includes}
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../Core/GameEngine/Include/Precompiled"
    )
    target_compile_definitions(engine_navigation_generalsmd PRIVATE IG_DEBUG_STACKTRACE)
    target_compile_features(engine_navigation_generalsmd PUBLIC cxx_std_23)
    set_property(TARGET engine_navigation_generalsmd PROPERTY CXX_SCAN_FOR_MODULES ON)
    target_link_libraries(engine_navigation_generalsmd PUBLIC engine_navigation)
    target_link_libraries(engine_navigation_generalsmd PRIVATE
        generals_graphics
        zi_always
        corei_gameengine_public
        core_profile_legacy
        z_wwvegas
        SDL3::SDL3
    )
    target_link_libraries(${game_target} PRIVATE engine_navigation_generalsmd)
endfunction()

function(engine_navigation_add_world_tests)
    if(NOT ENGINE_NAVIGATION_BUILD_TESTS)
        return()
    endif()
    find_package(boost_included_unit_test_framework CONFIG REQUIRED)
    set(nav_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    foreach(world_target IN ITEMS engine_navigation_world_tests engine_navigation_simulation_tests)
        if(world_target STREQUAL "engine_navigation_world_tests")
            set(world_sources
                "${nav_dir}/pathfinder.test"
                "${nav_dir}/partition.test"
                "${nav_dir}/test/world_fixture.test"
            )
            set(world_modules
                "${nav_dir}/test/world_fixture.cppm"
                "${nav_dir}/test/partition_fixture.cppm"
            )
        elseif(world_target STREQUAL "engine_navigation_simulation_tests")
            set(world_sources "${nav_dir}/test/simulation_fixture.test")
            set(world_modules "${nav_dir}/test/simulation_fixture.cppm")
        endif()
        add_executable(${world_target} ${world_sources})
        set_source_files_properties(${world_sources} PROPERTIES LANGUAGE CXX SKIP_PRECOMPILE_HEADERS ON)
        target_sources(${world_target} PRIVATE
            FILE_SET CXX_MODULES BASE_DIRS "${nav_dir}" FILES ${world_modules})
        set_source_files_properties(${world_modules} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
        target_compile_definitions(${world_target} PRIVATE BOOST_TEST_INCLUDED IG_DEBUG_STACKTRACE
            NAVIGATION_FIXTURE_DIRECTORY="${nav_dir}/test/fixtures")
        target_include_directories(${world_target} PRIVATE "${nav_dir}/../../Core/GameEngine/Include/Precompiled")
        target_link_libraries(${world_target} PRIVATE
            engine_navigation_generalsmd Boost::included_unit_test_framework
            z_gameengine z_gameenginedevice zi_always
            comctl32 d3d9lib imm32 SDL3::SDL3 vfw32 winmm)
        set_property(TARGET ${world_target} PROPERTY CXX_SCAN_FOR_MODULES ON)
        add_custom_command(TARGET ${world_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:${world_target}>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "$<IF:$<CONFIG:Debug>,${FFMPEG_DEBUG_RUNTIME_DIRECTORY},${FFMPEG_RUNTIME_DIRECTORY}>"
            "$<TARGET_FILE_DIR:${world_target}>")
    endforeach()
    add_test(NAME engine_navigation_world_tests COMMAND engine_navigation_world_tests)
    target_include_directories(engine_navigation_simulation_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Main")
    add_test(NAME engine_navigation_simulation_tests COMMAND engine_navigation_simulation_tests
        --run_test=real_twilight_flame_initialization_and_objectless_vehicle_preview
        --log_level=message)
    set_tests_properties(engine_navigation_simulation_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        LABELS "integration")
    # Each fixture owns engine singletons; run the larger scenario in its own process.
    add_test(NAME engine_navigation_request_lifecycle_tests COMMAND engine_navigation_simulation_tests
        --run_test=deferred_ground_requests_keep_the_latest_order --log_level=message)
    foreach(unit_type AmericaVehicleDozer AmericaVehicleHumvee AmericaInfantryRanger ChinaVehicleSupplyTruck GLAInfantryWorker)
        string(TOLOWER "${unit_type}" command_case)
        add_test(NAME engine_navigation_command_${command_case}_tests COMMAND engine_navigation_simulation_tests
            --run_test=player_commands_respond_while_background_searches_are_busy --log_level=message)
        set_tests_properties(engine_navigation_command_${command_case}_tests PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
            ENVIRONMENT "SAGE_NAVIGATION_COMMAND_UNIT=${unit_type}"
            LABELS "integration;responsiveness" TIMEOUT 60)
    endforeach()
    add_test(NAME engine_navigation_command_newest_batch_tests COMMAND engine_navigation_simulation_tests
        --run_test=new_player_commands_overtake_older_player_command_batches --log_level=message)
    set_tests_properties(engine_navigation_command_newest_batch_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        LABELS "integration;responsiveness" TIMEOUT 60)
    add_test(NAME engine_navigation_command_same_tick_tests COMMAND engine_navigation_simulation_tests
        --run_test=new_commands_overtake_same_tick_orders --log_level=message)
    add_test(NAME engine_navigation_command_preemption_tests COMMAND engine_navigation_simulation_tests
        --run_test=new_commands_preempt_active_foreground_without_abandoning_earlier_orders --log_level=message)
    add_test(NAME engine_navigation_crowded_route_parity_tests COMMAND engine_navigation_simulation_tests
        --run_test=crowded_native_and_captured_searches_agree --log_level=message)
    set_tests_properties(engine_navigation_command_same_tick_tests engine_navigation_command_preemption_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        LABELS "integration;responsiveness" TIMEOUT 60)
    set_tests_properties(engine_navigation_crowded_route_parity_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        LABELS "integration" TIMEOUT 60)
    add_test(NAME engine_navigation_serial_determinism_tests COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:engine_navigation_simulation_tests>"
        "-DTEST_DIR=$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/test/serial_determinism.cmake")
    set_tests_properties(engine_navigation_serial_determinism_tests PROPERTIES
        LABELS "integration;determinism" TIMEOUT 300)
    add_test(NAME engine_navigation_save_oracle_determinism_tests COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:engine_navigation_simulation_tests>"
        "-DTEST_DIR=$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/test/save_oracle_determinism.cmake")
    set_tests_properties(engine_navigation_save_oracle_determinism_tests PROPERTIES
        LABELS "integration;determinism" TIMEOUT 300
        SKIP_REGULAR_EXPRESSION "SKIP: Set SAGE_NAVIGATION_TEST_SAVE")
    add_test(NAME engine_navigation_special_serial_determinism_tests COMMAND "${CMAKE_COMMAND}"
        "-DTEST_EXE=$<TARGET_FILE:engine_navigation_simulation_tests>"
        "-DTEST_DIR=$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        -DSCENARIOS=special
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/test/serial_determinism.cmake")
    set_tests_properties(engine_navigation_special_serial_determinism_tests PROPERTIES
        LABELS "integration;determinism" TIMEOUT 120)
    add_test(NAME engine_navigation_yield_lifecycle_tests COMMAND engine_navigation_simulation_tests
        --run_test=yield_requests_reject_missing_inputs --log_level=message)
    add_test(NAME engine_navigation_slope_movement_tests COMMAND engine_navigation_simulation_tests
        --run_test=ranger_keeps_moving_across_twilight_north_slope --log_level=message)
    add_test(NAME engine_navigation_crowd_movement_tests COMMAND engine_navigation_simulation_tests
        --run_test=northwest_twilight_crowd_keeps_moving --log_level=message)
    set_tests_properties(engine_navigation_crowd_movement_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        LABELS "integration;stress" TIMEOUT 60)
    set_tests_properties(engine_navigation_slope_movement_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        LABELS "integration")
    set_tests_properties(engine_navigation_yield_lifecycle_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        LABELS "integration")
    set_tests_properties(engine_navigation_request_lifecycle_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        LABELS "integration")
    add_test(NAME engine_navigation_simulation_stress_tests COMMAND engine_navigation_simulation_tests
        --run_test=ten_thousand_real_units_on_twilight_flame --log_level=message)
    set_tests_properties(engine_navigation_simulation_stress_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        LABELS "integration;stress"
        TIMEOUT 600)
    add_test(NAME engine_navigation_formation_destination_tests COMMAND engine_navigation_simulation_tests
        --run_test=ten_thousand_formation_destinations_are_valid --log_level=message)
    set_tests_properties(engine_navigation_formation_destination_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        LABELS "integration;stress" TIMEOUT 300)
    add_test(NAME engine_navigation_fresh_destination_tests COMMAND engine_navigation_simulation_tests
        --run_test=fresh_destination_fallback_does_not_require_legacy_search_storage --log_level=message)
    set_tests_properties(engine_navigation_fresh_destination_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        LABELS "integration" TIMEOUT 60)
    add_test(NAME engine_navigation_dock_fairness_tests COMMAND engine_navigation_simulation_tests
        --run_test=returning_supply_truck_does_not_overtake_a_waiting_truck --log_level=message)
    add_test(NAME engine_navigation_driving_stability_tests COMMAND engine_navigation_simulation_tests
        --run_test=clear_route_driving_does_not_repeatedly_reverse_steering --log_level=message)
    add_test(NAME engine_navigation_route_repair_tests COMMAND engine_navigation_simulation_tests
        --run_test=route_repair_rejoins_farther_tail_when_nearest_node_is_enclosed --log_level=message)
    add_test(NAME engine_navigation_supply_transit_tests COMMAND engine_navigation_simulation_tests
        --run_test=supply_trucks_keep_driving_between_shared_docks --log_level=message)
    add_test(NAME engine_navigation_queue_snapshot_tests COMMAND engine_navigation_simulation_tests
        --run_test=pending_navigation_orders_survive_snapshot_in_arrival_order --log_level=message)
    add_test(NAME engine_navigation_queue_savegame_tests COMMAND engine_navigation_simulation_tests
        --run_test=pending_navigation_orders_survive_full_game_save --log_level=message)
    add_test(NAME engine_navigation_queue_budget_tests COMMAND engine_navigation_simulation_tests
        --run_test=mass_orders_keep_pending_requests_between_ticks --log_level=message)
    add_test(NAME engine_navigation_replay_terrain_route_tests COMMAND engine_navigation_simulation_tests
        --run_test=long_replay_routes_repeat_on_static_twilight_terrain --log_level=message)
    add_test(NAME engine_navigation_captured_replay_terrain_route_tests COMMAND engine_navigation_simulation_tests
        --run_test=long_replay_routes_repeat_on_static_twilight_terrain --log_level=message)
    add_test(NAME engine_navigation_incremental_occupancy_tests COMMAND engine_navigation_simulation_tests
        --run_test=incremental_occupancy_matches_full_capture_during_native_movement --log_level=message)
    set_tests_properties(engine_navigation_incremental_occupancy_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        LABELS "integration;determinism" TIMEOUT 120)
    set_tests_properties(engine_navigation_captured_replay_terrain_route_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        ENVIRONMENT "SAGE_NAVIGATION_ROUTE_CAPTURED=1;SAGE_NAVIGATION_ROUTE_REPEATS=2"
        LABELS "integration;determinism" TIMEOUT 120)
    set_tests_properties(engine_navigation_dock_fairness_tests engine_navigation_supply_transit_tests
        engine_navigation_driving_stability_tests engine_navigation_route_repair_tests
        engine_navigation_queue_snapshot_tests engine_navigation_queue_savegame_tests
        engine_navigation_queue_budget_tests engine_navigation_replay_terrain_route_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_navigation_simulation_tests>"
        LABELS "integration" TIMEOUT 60)
endfunction()

if(ENGINE_NAVIGATION_BUILD_TESTS)
    find_package(boost_included_unit_test_framework CONFIG REQUIRED)
    find_package(Threads REQUIRED)
    # Historical kernels remain available only to characterization/reference tests.
    # The game cannot accidentally select the linked lists or bounded request ring.
    add_library(engine_navigation_reference STATIC)
    target_sources(engine_navigation_reference PUBLIC FILE_SET CXX_MODULES FILES
        test/reference/search_lists.cppm
        test/reference/request_queue.cppm)
    target_link_libraries(engine_navigation_reference PUBLIC engine_navigation)
    set(ENGINE_NAVIGATION_TESTS
        navigation.test
        costs.test
        search_lists.test
        request_queue.test
        querymemo.test
        search_workspace.test
        search/route_search.test
        search/nearest_goal_bound.test
        search/hpa_route.test
        search/cluster_topology.test
        search/unsigned_divisor.test
        search/cell_query_cache.test
        path/reconstruction.test
        dynamic_request_queue.test
        scheduling/deadline_queue.test
        scheduling/continuation.test
        spatial/contact_pairs.test
        spatial/collision/cylinder_contact.test
        spatial/query/visit_epoch.test
        spatial/storage/contact_pair_pool.test
        spatial/storage/contact_queue.test
        topology/terrain_slope.test
        topology/reachability_cache.test
        topology/footprint_reachability.test
        movement/start_escape.test
        movement/following/route_geometry.test
        movement/following/route_target.test
        movement/corridor/phase_line.test
        movement/goal_projection.test
        movement/formation_layout.test
        movement/occupancy_policy.test
        movement/snapshot/occupants.test
        movement/snapshot/cells.test
        movement/terrain_policy.test
        movement/classification/weighted_cell.test
        movement/destination/reservations.test
        movement/destination/flee_goal.test
        movement/corridor/clearance.test
        movement/corridor/yield_paths.test
        search/graph/weighted_steps.test
        search/graph/captured_weighted.test
        search/graph/captured_flee.test
        search/graph/destination_rank.test
        movement/relationship_query.test
    )
    set_source_files_properties(${ENGINE_NAVIGATION_TESTS} PROPERTIES LANGUAGE CXX)
    enable_testing()
    add_executable(engine_navigation_tests ${ENGINE_NAVIGATION_TESTS})
    target_compile_definitions(engine_navigation_tests PRIVATE BOOST_TEST_INCLUDED)
    target_link_libraries(engine_navigation_tests PRIVATE
        engine_navigation
        engine_navigation_reference
        Threads::Threads
        Boost::included_unit_test_framework
    )
    set_property(TARGET engine_navigation_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
    add_test(NAME engine_navigation_tests COMMAND engine_navigation_tests)
endif()
