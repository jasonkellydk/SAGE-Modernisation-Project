include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/module_library.cmake")
engine_add_module_library(engine_gui_text
    MODULES font_face.cppm layout.cppm renderer.cppm
    DEPENDS generals_graphics generals_assets
    TESTS font_face.test)
