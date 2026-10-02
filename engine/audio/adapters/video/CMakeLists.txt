include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/module_library.cmake")
engine_add_module_library(engine_audio_video
    MODULES movie_sound.cppm
    DEPENDS engine_audio generals_video
    TESTS movie_sound.test)
