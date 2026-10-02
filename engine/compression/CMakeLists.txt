# Decompression codecs used by content formats, and the 8-byte wrapper that
# tells them apart.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/module_library.cmake")
find_package(ZLIB REQUIRED)

engine_add_module_library(engine_compression
	MODULES
		refpack/refpack_decoder.cppm
		zlib/zlib_decoder.cppm
		framing/compressed_block.cppm
	DEPENDS ZLIB::ZLIB
	TESTS
		refpack/refpack_decoder.test
		framing/compressed_block.test
	TESTS_OPTION ENGINE_COMPRESSION_BUILD_TESTS)
