include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/module_library.cmake")
engine_add_module_library(engine_gui_w3d
	MODULES binding.cppm dialog_template.cppm dialog_input.cppm dialog_layout.cppm value_controls.cppm menu_entry.cppm navigation.cppm marquee.cppm list_selection.cppm input_capture.cppm text_edit.cppm scrollbar.cppm
	DEPENDS engine_gui_mvvm
	TESTS binding.test dialog_template.test dialog_input.test dialog_layout.test value_controls.test menu_entry.test navigation.test marquee.test list_selection.test input_capture.test text_edit.test scrollbar.test
	TESTS_OPTION ENGINE_GUI_MVVM_BUILD_TESTS)
