# Gameplay layout guard (regex scope, not a C++ parser; review owns semantics).
# See games/generalszh/migration/README.md.
#   engine-game-import             engine code importing game modules
#   domain-layout                  gameplay files outside <genre>/<domain>/<folder>/
#   component-outside-components   ComponentTraits specialisation outside components/
#   system-outside-systems         *System type outside systems/
#   feature-wrapper                *Simulation / *Systems wrapper classes
#   execution-owner                Scheduler/SystemRegistry owned outside the composition root
#   presentation-object-style      presentation/host code with stateful Director/Manager/Controller
#                                  types or entity-keyed maps (ECS only: side-table components and
#                                  stateless systems); known debt in presentation_debt.cmake
cmake_minimum_required(VERSION 3.25)
if(NOT DEFINED SOURCE_ROOT)
    get_filename_component(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
endif()

set(domain_folders "components|systems|definitions|algorithms|resources")
set(engine_domain "^engine/gameplay/(common|rts|fps)/[a-z0-9_]+/(${domain_folders})/[a-z0-9_]+[.]cppm$")
set(game_domain "^games/(generalszh|renegade)/gameplay/[a-z0-9_]+/(${domain_folders})/[a-z0-9_]+[.]cppm$")
set(composition_root "^games/(generalszh|renegade)/session/")

file(GLOB_RECURSE sources RELATIVE "${SOURCE_ROOT}"
    "${SOURCE_ROOT}/engine/*.cppm"
    "${SOURCE_ROOT}/games/generalszh/*.cppm"
    "${SOURCE_ROOT}/games/renegade/*.cppm")
list(SORT sources)
set(failures "")
foreach(path IN LISTS sources)
    file(READ "${SOURCE_ROOT}/${path}" text)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " text "${text}")
    string(REGEX REPLACE "//[^\n]*" " " text "${text}")
    if(path MATCHES "^engine/" AND text MATCHES "(^|[;\n])[ \t]*(export[ \t]+)?import[ \t]+(games[.]|generalszh[.]|renegade[.])")
        string(APPEND failures "${path}: engine-game-import\n")
    endif()
    if(NOT path MATCHES "^(engine/gameplay|games/(generalszh|renegade))/")
        continue()
    endif()

    set(in_gameplay FALSE)
    if(path MATCHES "^(engine/gameplay|games/(generalszh|renegade)/gameplay)/")
        set(in_gameplay TRUE)
        if(NOT path MATCHES "${engine_domain}" AND NOT path MATCHES "${game_domain}")
            string(APPEND failures "${path}: domain-layout (expected <genre>/<domain>/{${domain_folders}}/<file>.cppm)\n")
        endif()
    endif()
    if(in_gameplay)
        if(text MATCHES "ComponentTraits[ \t\r\n]*<" AND NOT path MATCHES "/components/")
            string(APPEND failures "${path}: component-outside-components\n")
        endif()
        if(text MATCHES "(class|struct)[ \t\r\n]+[A-Za-z_][A-Za-z_0-9]*System([^A-Za-z_0-9]|$)" AND NOT path MATCHES "/systems/")
            string(APPEND failures "${path}: system-outside-systems\n")
        endif()
    endif()
    if(text MATCHES "(class|struct)[ \t\r\n]+[A-Za-z_][A-Za-z_0-9]*(Simulation|Systems)([^A-Za-z_0-9]|$)")
        string(APPEND failures "${path}: feature-wrapper (${CMAKE_MATCH_0})\n")
    endif()
    # Domains may offer `inline void Register<Name>(ecs::SystemRegistry &, ...)`
    # helpers; only the composition root owns a registry or scheduler.
    if(NOT path MATCHES "${composition_root}")
        string(REGEX REPLACE "(^|\n)inline void Register[A-Za-z_0-9]*\\([^)]*\\)" "\\1RegistrationHelper()" owner_text "${text}")
        if(owner_text MATCHES "(^|[^A-Za-z_0-9])(Scheduler|SystemRegistry)([^A-Za-z_0-9]|$)")
            string(APPEND failures "${path}: execution-owner\n")
        endif()
    endif()
endforeach()
# ECS is the only accepted architecture, presentation included.
include("${CMAKE_CURRENT_LIST_DIR}/presentation_debt.cmake")
file(GLOB_RECURSE presentation_sources RELATIVE "${SOURCE_ROOT}"
    "${SOURCE_ROOT}/games/generalszh/presentation/*.cppm"
    "${SOURCE_ROOT}/games/generalszh/presentation/*.cpp"
    "${SOURCE_ROOT}/games/generalszh/hosts/*.cppm"
    "${SOURCE_ROOT}/games/generalszh/hosts/*.cpp"
    "${SOURCE_ROOT}/games/renegade/presentation/*.cppm"
    "${SOURCE_ROOT}/games/renegade/hosts/*.cppm")
list(SORT presentation_sources)
foreach(path IN LISTS presentation_sources)
    file(READ "${SOURCE_ROOT}/${path}" text)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " text "${text}")
    string(REGEX REPLACE "//[^\n]*" " " text "${text}")
    set(object_style FALSE)
    if(text MATCHES "(class|struct)[ \t\r\n]+[A-Za-z_0-9]*(Director|Manager|Controller)([^A-Za-z_0-9]|$)"
        OR text MATCHES "std::(unordered_)?map<[ \t]*std::uint64_t")
        set(object_style TRUE)
    endif()
    list(FIND presentation_debt "${path}" debt_index)
    if(object_style AND debt_index EQUAL -1)
        string(APPEND failures "${path}: presentation-object-style (ECS only: side-table components and stateless systems)\n")
    elseif(NOT object_style AND NOT debt_index EQUAL -1)
        string(APPEND failures "${path}: presentation debt paid off; remove it from presentation_debt.cmake\n")
    endif()
endforeach()

if(failures)
    message(FATAL_ERROR "Gameplay architecture violations:\n${failures}")
endif()
message(STATUS "Gameplay architecture checks passed (regex scope; semantic review required)")
