# Generic RTS tactical camera (client presentation; floats allowed).
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/module_library.cmake")

engine_add_module_library(engine_camera
	MODULES
		motion/camera_motion.cppm
		motion/anchor_transition.cppm
		model/rts_camera.cppm
	DEPENDS engine_core_math
	TESTS camera.test
	TESTS_OPTION ENGINE_CAMERA_BUILD_TESTS)
