# Read-only virtual file system: sources (loose directories, .big archives)
# merged in explicit precedence order into one case-insensitive namespace.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/module_library.cmake")

engine_add_module_library(engine_filesystem
	MODULES
		core/path.cppm
		core/text_files.cppm
		core/virtual_file_system.cppm
		adapters/directory/directory_source.cppm
		adapters/big/big_index.cppm
		adapters/big/big_archive_source.cppm
		adapters/mix/mix_archive_source.cppm
		adapters/pe/resources.cppm
	TESTS filesystem.test adapters/pe/resources.test text_files.test
	TESTS_OPTION ENGINE_FILESYSTEM_BUILD_TESTS)
