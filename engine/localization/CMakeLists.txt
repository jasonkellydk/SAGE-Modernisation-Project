# Localized text: a format-neutral string table and format adapters (CSF, STR).
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/module_library.cmake")

engine_add_module_library(engine_localization
	MODULES
		model/string_table.cppm
		adapters/csf/csf_reader.cppm
		adapters/str/str_reader.cppm
	TESTS adapters/csf/csf_reader.test adapters/str/str_reader.test
	TEST_DEPENDS engine_filesystem
	TESTS_OPTION ENGINE_LOCALIZATION_BUILD_TESTS)
