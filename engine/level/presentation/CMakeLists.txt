include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/module_library.cmake")
engine_add_module_library(engine_level_assets
    MODULES model_collision.cppm
    DEPENDS engine_level generals_assets
    TESTS model_collision.test)
