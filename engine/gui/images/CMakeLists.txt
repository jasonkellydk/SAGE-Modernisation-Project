include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/module_library.cmake")
engine_add_module_library(engine_gui_images
    MODULES images.cppm
    DEPENDS generals_graphics generals_assets
    TESTS images.test)
