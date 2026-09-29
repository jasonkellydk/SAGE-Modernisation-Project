# Deterministic binary encoding shared by the lockstep protocol, commands,
# checkpoints and replays.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/module_library.cmake")

engine_add_module_library(engine_core_serialization
	MODULES byte_stream.cppm
	TESTS byte_stream.test
	TESTS_OPTION ENGINE_CORE_SERIALIZATION_BUILD_TESTS)
if(TARGET engine_core_serialization_tests)
	set_tests_properties(engine_core_serialization_tests PROPERTIES PASS_REGULAR_EXPRESSION "No errors detected")
endif()
