# Text encodings shared by config files, the network and saves.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/module_library.cmake")

engine_add_module_library(engine_core_text
	MODULES quoted_printable.cppm utf.cppm
	TESTS quoted_printable.test
	TESTS_OPTION ENGINE_CORE_TEXT_BUILD_TESTS)
if(TARGET engine_core_text_tests)
	set_tests_properties(engine_core_text_tests PROPERTIES PASS_REGULAR_EXPRESSION "No errors detected")
endif()
