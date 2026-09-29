# MVVM building blocks shared by all UI: observable properties and commands.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/module_library.cmake")
engine_add_module_library(engine_gui_mvvm
	MODULES observable.cppm
	TESTS observable.test
	TESTS_OPTION ENGINE_GUI_MVVM_BUILD_TESTS)
