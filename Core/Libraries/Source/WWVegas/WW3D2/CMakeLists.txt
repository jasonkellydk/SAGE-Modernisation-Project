set(WW3D2_SRC
    #AnimObj.cpp
    #AnimObj.h
    #AssetMgr.cpp
    #AssetMgr.h
    #Camera.cpp
    #Camera.h
    Collect.cpp
    Collect.h
    ColTest.cpp
    ColTest.h
    ColType.h
    Composite.cpp
    Composite.h
    #Dazzle.cpp
    #Dazzle.h
    #HLOD.cpp
    #HLOD.h
    IntTest.h
    #Light.cpp
    #Light.h
    #Mesh.cpp
    #Mesh.h
    #MeshGeometry.cpp
    #MeshGeometry.h
    #MeshMdl.cpp
    #MeshMdl.h
    #MeshMdlIO.cpp
    #PartBuf.cpp
    #PartBuf.h
    #PartEmt.cpp
    #PartEmt.h
    Projector.cpp
    Projector.h
    RendObj.cpp
    RendObj.h
    #RInfo.cpp
    #RInfo.h
    #Scene.cpp
    #Scene.h
    SegLine.cpp
    SegLine.h
    SegLineRenderer.cpp
    SegLineRenderer.h
    TexProject.cpp
    TexProject.h
    Texture.cpp
    Texture.h
    #W3DFile.h
    W3DErr.h
    WW3D.cpp
    WW3D.h
    WW3DIds.h
)

add_library(corei_ww3d2 INTERFACE)

target_sources(corei_ww3d2 INTERFACE ${WW3D2_SRC})

target_include_directories(corei_ww3d2 INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if (MSVC AND NOT IS_VS6_BUILD)
    target_link_libraries(corei_ww3d2 INTERFACE
        comsuppw
    )
endif()

target_link_libraries(corei_ww3d2 INTERFACE
    core_browserengine
    core_wwlib
    core_wwmath
)
