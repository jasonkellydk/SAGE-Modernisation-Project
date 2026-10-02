include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/module_library.cmake")
engine_add_module_library(engine_assets_filesystem
    MODULES virtual_asset_source.cppm
    DEPENDS generals_assets engine_filesystem
    TESTS virtual_asset_source.test)
