cmake_minimum_required(VERSION 3.28)

option(ENGINE_GUI_MVVM_BUILD_TESTS "Build MVVM tests" ${BUILD_TESTING})
if(NOT TARGET engine_gui_mvvm)
	add_subdirectory(mvvm)
endif()
add_subdirectory(wnd)
