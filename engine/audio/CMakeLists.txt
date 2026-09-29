# Audio: sound definitions, the software mixer (3D spatialization, buses,
# streams), the sound player that sequences sound events, and the output
# interface. File formats are decoders (decoders/<library>: FFmpeg); sound
# output backends are adapters (adapters/<api>: XAudio2, preferred on
# Windows, and SDL3). Presentation only: never
# read by the simulation.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/module_library.cmake")

engine_add_module_library(engine_audio
	MODULES
		definitions/sound_event.cppm
		definitions/wave_length.cppm
		mixing/pcm.cppm
		mixing/spatial.cppm
		mixing/mixer.cppm
		playback/sound_player.cppm
		output/audio_output.cppm
	TESTS audio.test
	TESTS_OPTION ENGINE_AUDIO_BUILD_TESTS)
if(TARGET engine_audio_tests)
	set_tests_properties(engine_audio_tests PROPERTIES PASS_REGULAR_EXPRESSION "No errors detected")
endif()

# A module library whose platform code sits in one private implementation unit.
function(engine_audio_adapter target module implementation)
	engine_add_module_library(${target} MODULES ${module} DEPENDS engine_audio ${ARGN})
	target_sources(${target} PRIVATE ${implementation})
	set_source_files_properties(${implementation} PROPERTIES LANGUAGE CXX)
endfunction()

find_package(FFMPEG QUIET)
if(FFMPEG_FOUND)
	engine_add_module_library(engine_audio_ffmpeg
		MODULES decoders/ffmpeg/ffmpeg_decoder.cppm
		DEPENDS engine_audio
		TESTS decoders/ffmpeg/ffmpeg_decoder.test
		TESTS_OPTION ENGINE_AUDIO_BUILD_TESTS)
	target_sources(engine_audio_ffmpeg PRIVATE decoders/ffmpeg/ffmpeg_decoder.cpp)
	set_source_files_properties(decoders/ffmpeg/ffmpeg_decoder.cpp PROPERTIES LANGUAGE CXX)
	target_include_directories(engine_audio_ffmpeg PRIVATE ${FFMPEG_INCLUDE_DIRS})
	target_link_directories(engine_audio_ffmpeg PUBLIC ${FFMPEG_LIBRARY_DIRS})
	target_link_libraries(engine_audio_ffmpeg PUBLIC ${FFMPEG_LIBRARIES})
	if(TARGET engine_audio_ffmpeg_tests)
		set_tests_properties(engine_audio_ffmpeg_tests PROPERTIES PASS_REGULAR_EXPRESSION "No errors detected")
		if(DEFINED FFMPEG_RUNTIME_DIRECTORY)
			set_tests_properties(engine_audio_ffmpeg_tests PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${FFMPEG_RUNTIME_DIRECTORY}")
		endif()
	endif()
endif()

find_package(SDL3 CONFIG QUIET)
if(TARGET SDL3::SDL3)
	engine_audio_adapter(engine_audio_sdl3 adapters/sdl3/sdl3_output.cppm adapters/sdl3/sdl3_output.cpp)
	target_link_libraries(engine_audio_sdl3 PRIVATE SDL3::SDL3)

	set(preferred_outputs engine_audio_sdl3)
	if(WIN32)
		engine_audio_adapter(engine_audio_xaudio2 adapters/xaudio2/xaudio2_output.cppm adapters/xaudio2/xaudio2_output.cpp)
		target_link_libraries(engine_audio_xaudio2 PRIVATE xaudio2 ole32)
		list(PREPEND preferred_outputs engine_audio_xaudio2)
	endif()
	engine_add_module_library(engine_audio_output
		MODULES adapters/preferred_output.cppm
		DEPENDS ${preferred_outputs})
endif()
