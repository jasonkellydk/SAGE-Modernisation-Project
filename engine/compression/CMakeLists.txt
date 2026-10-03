# Decompression codecs used by content formats, and the 8-byte wrapper that
# tells them apart.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/module_library.cmake")
find_package(ZLIB REQUIRED)

# FindZLIB describes Windows import libraries as UNKNOWN, which hides their
# DLL from CMake's runtime dependency deployment. Preserve static packages and
# describe a co-installed shared runtime when the import-library stem matches.
set(engine_zlib_target ZLIB::ZLIB)
if(WIN32)
	get_target_property(engine_zlib_type ZLIB::ZLIB TYPE)
	if(engine_zlib_type STREQUAL "UNKNOWN_LIBRARY" AND ZLIB_LIBRARY_RELEASE)
		get_filename_component(engine_zlib_stem "${ZLIB_LIBRARY_RELEASE}" NAME_WE)
		get_filename_component(engine_zlib_lib_directory "${ZLIB_LIBRARY_RELEASE}" DIRECTORY)
		find_file(engine_zlib_runtime NAMES "${engine_zlib_stem}.dll"
			PATHS "${engine_zlib_lib_directory}/../bin" NO_DEFAULT_PATH)
		if(engine_zlib_runtime)
			add_library(engine_zlib_runtime_target SHARED IMPORTED GLOBAL)
			set_target_properties(engine_zlib_runtime_target PROPERTIES
				IMPORTED_IMPLIB "${ZLIB_LIBRARY_RELEASE}" IMPORTED_LOCATION "${engine_zlib_runtime}"
				INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIRS}")
			set(engine_zlib_target engine_zlib_runtime_target)
		endif()
	endif()
endif()

engine_add_module_library(engine_compression
	MODULES
		refpack/refpack_decoder.cppm
		zlib/zlib_decoder.cppm
		framing/compressed_block.cppm
	DEPENDS ${engine_zlib_target}
	TESTS
		refpack/refpack_decoder.test
		framing/compressed_block.test
	TESTS_OPTION ENGINE_COMPRESSION_BUILD_TESTS)
