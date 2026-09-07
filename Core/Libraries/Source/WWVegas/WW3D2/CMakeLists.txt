set(WW3D2_SRC
	VertexFormat.h
    #AnimObj.cpp
    #AnimObj.h
    #AssetMgr.cpp
    #AssetMgr.h
	StringUtilities.h
    #Camera.cpp
    #Camera.h
    #ClassId.h # unused
    Collect.cpp
    Collect.h
    ColTest.cpp
    ColTest.h
    ColType.h
    Composite.cpp
    Composite.h
    #Dazzle.cpp
    #Dazzle.h
    DList.h
    #HLOD.cpp
    #HLOD.h
    IntTest.h
    #Light.cpp
    #Light.h


    #Mapper.cpp
    #Mapper.h
    MatInfo.cpp
    MatInfo.h
    MatPass.cpp
    MatPass.h
    #MatrixMapper.cpp
    #MatrixMapper.h
    #Mesh.cpp
    #Mesh.h
    #MeshGeometry.cpp
    #MeshGeometry.h
    #MeshMatDesc.cpp
    #MeshMatDesc.h
    #MeshMdl.cpp
    #MeshMdl.h
    #MeshMdlIO.cpp
    #PartBuf.cpp
    #PartBuf.h
    #PartEmt.cpp
    #PartEmt.h
    PrimAnim.h
    Projector.cpp
    Projector.h
    Proxy.h
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
    #Shader.cpp
    #Shader.h
    ShdLib.h
    TexProject.cpp
    TexProject.h
    Texture.cpp
    Texture.h
    #VertMaterial.cpp
    #VertMaterial.h
    #W3DFile.h
    W3DObsolete.h
    W3DErr.h
    WW3D.cpp
    WW3D.h
    WW3DIds.h
    WW3DTrig.h
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
