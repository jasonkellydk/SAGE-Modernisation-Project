# Source-policy guard for the modern simulation code (regex scope, not a C++
# parser; review still owns semantics). See games/generalszh/migration/README.md.
#   legacy-include   includes GeneralsMD/Core/WWVegas headers
#   legacy-type      legacy vocabulary types (AsciiString, Real, Int, Coord3D, ...)
#   float            float/double in simulation code (use engine/core/math/fixed)
#   the-naming       legacy global naming (TheGlobalData, TheThingFactory, ...)
#   singleton        static Instance()/GetInstance() accessors
#   genre-dependency engine/gameplay/common importing rts/fps, or rts<->fps
#   world-access     gameplay reaching the mutable World via GetWorld(); use
#                    ecs::Lookup for reads and Commands() for writes
# Graphics, gui, assets and video are still shared with the legacy build and
# are outside this guard's scope for now.
cmake_minimum_required(VERSION 3.25)
if(NOT DEFINED SOURCE_ROOT)
    get_filename_component(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
endif()

set(modern_roots
    engine/ecs engine/jobs engine/events engine/time engine/core/math/fixed engine/core/serialization engine/net engine/audio engine/effects engine/config engine/filesystem engine/compression engine/level engine/localization engine/scripting
    engine/gameplay games/generalszh)
# Presentation code may use float; everything else in scope is simulation.
set(float_exempt "^(engine/audio|engine/effects|games/generalszh/presentation|games/generalszh/hosts|engine/core/math/fixed/presentation|engine/level/presentation)/")

if(NOT DEFINED MODERN_CODE_DEBT_FILE)
    set(MODERN_CODE_DEBT_FILE "${CMAKE_CURRENT_LIST_DIR}/modern_code_debt.cmake")
endif()
include("${MODERN_CODE_DEBT_FILE}")

set(sources "")
foreach(root IN LISTS modern_roots)
    file(GLOB_RECURSE found RELATIVE "${SOURCE_ROOT}"
        "${SOURCE_ROOT}/${root}/*.cppm" "${SOURCE_ROOT}/${root}/*.cpp"
        "${SOURCE_ROOT}/${root}/*.h" "${SOURCE_ROOT}/${root}/*.hpp")
    list(APPEND sources ${found})
endforeach()
list(SORT sources)

set(failures "")
set(used_debt "")
function(report path rule)
    if("${path}|${rule}" IN_LIST modern_code_debt)
        set(used_debt "${used_debt};${path}|${rule}" PARENT_SCOPE)
    else()
        set(failures "${failures}${path}: ${rule}\n" PARENT_SCOPE)
    endif()
endfunction()

foreach(path IN LISTS sources)
    file(READ "${SOURCE_ROOT}/${path}" text)
    # Keep include paths, then drop comments and string literals so field
    # names such as "Real" inside strings do not count.
    string(REGEX MATCHALL "#[ \t]*include[ \t]*[<\"][^>\"\n]*[>\"]" includes "${text}")
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " text "${text}")
    string(REGEX REPLACE "//[^\n]*" " " text "${text}")
    string(REGEX REPLACE "\"([^\"\\\\\n]|\\\\.)*\"" "\"\"" text "${text}")
    string(REGEX REPLACE "'([^'\\\\\n]|\\\\.)*'" "''" text "${text}")

    foreach(include IN LISTS includes)
        if(include MATCHES "(GeneralsMD|Core/|WWVegas|WW3D2|GameLogic/|GameClient/|GameNetwork/|Common/|Lib/BaseType|always\\.h|PreRTS)")
            report("${path}" legacy-include)
            break()
        endif()
    endforeach()
    if(text MATCHES "(^|[^A-Za-z_0-9])(AsciiString|UnicodeString|Real|Int|UnsignedInt|Bool|UnsignedShort|UnsignedByte|Coord2D|Coord3D|ICoord2D|ICoord3D)([^A-Za-z_0-9]|$)")
        report("${path}" legacy-type)
    endif()
    if(NOT path MATCHES "${float_exempt}" AND text MATCHES "(^|[^A-Za-z_0-9])(float|double)([^A-Za-z_0-9]|$)")
        report("${path}" float)
    endif()
    if(text MATCHES "(^|[^A-Za-z_0-9])The[A-Z][A-Za-z_0-9]*")
        report("${path}" the-naming)
    endif()
    if(text MATCHES "static[^;{}()]*[ \t&*](Get)?Instance[ \t]*\\(")
        report("${path}" singleton)
    endif()
    if(path MATCHES "^(engine/gameplay|games/generalszh)/" AND text MATCHES "GetWorld[ \t]*\\(")
        report("${path}" world-access)
    endif()
    # common is genre-neutral; genres never depend on each other.
    set(import_prefix "(^|[;\n])[ \t]*(export[ \t]+)?import[ \t]+engine[.]gameplay[.]")
    if((path MATCHES "^engine/gameplay/common/" AND text MATCHES "${import_prefix}(rts|fps)[.]")
       OR (path MATCHES "^engine/gameplay/rts/" AND text MATCHES "${import_prefix}fps[.]")
       OR (path MATCHES "^engine/gameplay/fps/" AND text MATCHES "${import_prefix}rts[.]"))
        report("${path}" genre-dependency)
    endif()
endforeach()

# Debt must shrink: an entry that no longer matches is an error until removed.
foreach(entry IN LISTS modern_code_debt)
    if(NOT entry IN_LIST used_debt)
        string(APPEND failures "${entry}: resolved-debt (remove it from modern_code_debt.cmake)\n")
    endif()
endforeach()

if(failures)
    message(FATAL_ERROR "Modern code policy violations:\n${failures}")
endif()
message(STATUS "Modern code policy checks passed (regex scope; semantic review required)")
