set(WW3D2_SRC
    GraphicsMesh.cpp
    GraphicsMesh.h
    GraphicsMeshQueries.h
    GraphicsMaterial.h
    GraphicsMaterialPass.h
    GraphicsGeometry.cpp
    GraphicsGeometry.h
    AnimObj.cpp
    AnimObj.h
    AssetMgr.cpp
    AssetMgr.h
    Camera.cpp
    Camera.h
    #ClassId.h
    #Collect.cpp
    #Collect.h
    #ColTest.cpp
    #ColTest.h
    #ColType.h
    #Composite.cpp
    #Composite.h
    Dazzle.cpp
    Dazzle.h
    #DecalSys.cpp
    #DecalSys.h
    #DList.h
    #FormConv.cpp
    #FormConv.h
    HLOD.cpp
    HLOD.h
    #IntTest.h
    Light.cpp
    Light.h


    #Line3D.cpp
    #Line3D.h
    Mapper.cpp
    Mapper.h
    #MatInfo.cpp
    #MatInfo.h
    #MatPass.cpp
    #MatPass.h
    MatrixMapper.cpp
    MatrixMapper.h
    Mesh.cpp
    Mesh.h
    MeshGeometry.cpp
    MeshGeometry.h
    MeshMatDesc.cpp
    MeshMatDesc.h
    MeshMdl.cpp
    MeshMdl.h
    MeshMdlIO.cpp
    PartBuf.cpp
    PartBuf.h
    PartEmt.cpp
    PartEmt.h
    #PrimAnim.h
    #Projector.cpp
    #Projector.h
    #Proxy.h
    #RendObj.cpp
    #RendObj.h
    RInfo.cpp
    RInfo.h
    Scene.cpp
    Scene.h
    #SegLine.cpp
    #SegLine.h
    #SegLineRenderer.cpp
    #SegLineRenderer.h
    Shader.cpp
    Shader.h
#    ShdLib.h
    #TexProject.cpp
    #TexProject.h
    #Texture.cpp
    #Texture.h
    VertMaterial.cpp
    VertMaterial.h
    W3DFile.h
    #W3DObsolete.h
    #W3DErr.h
#    WW3D.cpp
#    WW3D.h
    #WW3DIds.h
    #WW3DTrig.h
)

add_library(z_ww3d2 STATIC)
set_property(TARGET z_ww3d2 PROPERTY CXX_SCAN_FOR_MODULES ON)
set_target_properties(z_ww3d2 PROPERTIES OUTPUT_NAME ww3d2)

target_sources(z_ww3d2 PRIVATE ${WW3D2_SRC})


add_dependencies(z_ww3d2 generals_graphics_shaders)

target_include_directories(z_ww3d2 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(z_ww3d2 PUBLIC
    $<$<BOOL:${IS_VS6_BUILD}>:WINVER=0x0500>
    SLANG_STATIC
)

target_precompile_headers(z_ww3d2 PRIVATE
    [["Utility/CppMacros.h"]] # Must be first, to be removed when abandoning VC6
    [["WWLib/always.h"]]
    [["WWLib/STLUtils.h"]]
    [["WWLib/WWCommon.h"]]
    [["WWLib/wwstring.h"]]
)

target_link_libraries(z_ww3d2 PRIVATE
    generals_assets
    generals_graphics
    core_wwdebug
    corei_ww3d2
    z_wwcommon
    zi_always
    SDL3::SDL3
)
