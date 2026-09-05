cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(generals_graphics LANGUAGES CXX)
endif()

add_library(generals_graphics STATIC)

target_sources(generals_graphics
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
            resources/handles/ResourceHandle.cppm
            memory/AlignedAllocator.cppm
            resources/pools/ResourcePool.cppm
            resources/meshes/Mesh.cppm
            resources/textures/Texture.cppm
            resources/samplers/Sampler.cppm
            resources/materials/Material.cppm
            resources/residency/GPUResourceResidency.cppm
            resources/bindless/BindlessResourceTable.cppm
            shaders/ParameterLayout.cppm
            shaders/Pipeline.cppm
            shaders/ShaderLibrary.cppm
            scene/terrain/TerrainVisibility.cppm
            scene/terrain/TerrainGeometry.cppm
            scene/terrain/TerrainRenderer.cppm
            scene/surfaces/SurfaceGeometry.cppm
            scene/surfaces/SurfaceRenderer.cppm
            scene/lines/LineDrawing.cppm
            scene/bibs/BibRenderer.cppm
            scene/roads/RoadRenderer.cppm
            scene/shadows/StencilVolumeRenderer.cppm
            scene/screen/ScreenFilterRenderer.cppm
            scene/debug/DebugRenderer.cppm
            scene/shadows/ProjectedShadowRenderer.cppm
            scene/props/PropLighting.cppm
            scene/props/PropGeometry.cppm
            scene/props/PropMaterial.cppm
            scene/props/PropRenderer.cppm
            scene/props/TransparentGeometry.cppm
            scene/props/MaterialPassQueue.cppm
            scene/water/WaterGeometry.cppm
            scene/water/WaterView.cppm
            scene/water/WaterRenderer.cppm
            scene/trees/TreeGeometry.cppm
            scene/trees/TreeRenderer.cppm
            scene/tracks/TrackGeometry.cppm
            scene/scorches/ScorchGeometry.cppm
            scene/bridges/BridgeRenderer.cppm
            scene/RenderScene.cppm
            scene/attachments/Attachments.cppm
            scene/meshes/StaticMeshRenderer.cppm
            scene/lighting/Lighting.cppm
            scene/lighting/EnvironmentLighting.cppm
            scene/lighting/LightRenderer.cppm
            scene/environment/Environment.cppm
            scene/shadows/Shadows.cppm
            scene/shadows/DirectionalShadowRenderer.cppm
            scene/decals/Decals.cppm
            scene/decals/DecalVisibility.cppm
            scene/decals/DecalDraw.cppm
            scene/world/WorldQuads.cppm
            scene/particles/Particles.cppm
            scene/particles/ParticleVisibility.cppm
            scene/particles/ParticleDraw.cppm
            scene/particles/ParticleRenderer.cppm
            scene/weather/WeatherParticles.cppm
            scene/screen/ScreenDistortion.cppm
            scene/screen/FullscreenOverlay.cppm
            scene/ring/Ring.cppm
            scene/beams/Beam.cppm
            scene/models/ModelVisibility.cppm
            scene/models/Skeleton.cppm
            scene/models/ModelInstance.cppm
            scene/models/Animation.cppm
            scene/models/AnimationBlend.cppm
            scene/models/Skinning.cppm
            scene/models/ModelAssetGeometry.cppm
            scene/models/ModelAssetBinding.cppm
            scene/transparency/Transparency.cppm
            scene/views/View.cppm
            scene/visibility/Visibility.cppm
            scene/lod/LOD.cppm
            scene/gpu/GPUScene.cppm
            scene/draw/DrawGeneration.cppm
            render-graph/RenderGraph.cppm
            render-graph/RenderGraphExecution.cppm
            text/GlyphAtlas.cppm
            renderer2d/Renderer2D.cppm
            rhi/RHI.cppm
            frame/FrameTargets.cppm
            capture/FrameCapture.cppm
            capture/MovieCapture.cppm
            frame/FrameOwner.cppm
            frame/SceneRenderers.cppm
            render-graph/Frame.cppm
            passes/opaque/OpaquePass.cppm
            passes/shadow/ShadowPass.cppm
            passes/transparent/TransparentPass.cppm
            passes/decal/DecalPass.cppm
            passes/particles/ParticlePass.cppm
            passes/post-process/PostProcess.cppm
            passes/sky/SkyPass.cppm
            passes/video/VideoPass.cppm
            video/VideoRenderer.cppm
)

target_compile_features(generals_graphics PUBLIC cxx_std_20)

if(TARGET generals_assets)
    target_link_libraries(generals_graphics PUBLIC generals_assets)
endif()

if(TARGET generals_video)
    target_link_libraries(generals_graphics PUBLIC generals_video)
endif()

if(TARGET core_profile_tracy)
    target_link_libraries(generals_graphics PUBLIC core_profile_tracy)
endif()

if(WIN32)
	set(GRAPHICS_SHADER_ASSET_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/shaders/precompiled/dx11")
	if(TARGET slang-bootstrap)
		set(GRAPHICS_SHADER_ASSET_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders/$<CONFIG>")
		set(GRAPHICS_BASIC_OPAQUE_VERTEX_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/basic_opaque.vso")
		set(GRAPHICS_SKINNED_BASIC_OPAQUE_VERTEX_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/basic_opaque_skinned.vso")
		set(GRAPHICS_BASIC_OPAQUE_FRAGMENT_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/basic_opaque.pso")
		set(GRAPHICS_PARTICLE_VERTEX_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/particle_billboard.vso")
		set(GRAPHICS_PARTICLE_FRAGMENT_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/particle_billboard.pso")
		set(GRAPHICS_SCREEN_DISTORTION_VERTEX_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/screen_distortion.vso")
		set(GRAPHICS_SCREEN_DISTORTION_FRAGMENT_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/screen_distortion.pso")
		set(GRAPHICS_BEAM_VERTEX_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/beam.vso")
		set(GRAPHICS_BEAM_FRAGMENT_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/beam.pso")
		set(GRAPHICS_VIDEO_VERTEX_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/video.vso")
		set(GRAPHICS_VIDEO_FRAGMENT_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/video.pso")
		set(GRAPHICS_RING_VERTEX_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/ring.vso")
		set(GRAPHICS_RING_FRAGMENT_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/ring.pso")
		set(GRAPHICS_FULLSCREEN_OVERLAY_VERTEX_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/fullscreen_overlay.vso")
		set(GRAPHICS_FULLSCREEN_OVERLAY_FRAGMENT_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/fullscreen_overlay.pso")
		set(GRAPHICS_WORLD_QUAD_VERTEX_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/world_quad.vso")
		set(GRAPHICS_WORLD_QUAD_FRAGMENT_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/world_quad.pso")
		set(GRAPHICS_SURFACE_VERTEX_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/surface.vso")
		set(GRAPHICS_SCREEN_FILTER_VERTEX_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/screen_filter.vso")
		set(GRAPHICS_SURFACE_FRAGMENT_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/surface.pso")
		set(GRAPHICS_SCREEN_FILTER_FRAGMENT_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/screen_filter.pso")
		set(GRAPHICS_TERRAIN_VERTEX_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/terrain.vso")
		set(GRAPHICS_TERRAIN_FRAGMENT_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/terrain.pso")
		set(GRAPHICS_UI_2D_VERTEX_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/ui_2d.vso")
		set(GRAPHICS_UI_2D_FRAGMENT_SHADER "${GRAPHICS_SHADER_ASSET_DIRECTORY}/ui_2d.pso")
		add_custom_command(
			OUTPUT
				"${GRAPHICS_BASIC_OPAQUE_VERTEX_SHADER}"
				"${GRAPHICS_SKINNED_BASIC_OPAQUE_VERTEX_SHADER}"
				"${GRAPHICS_BASIC_OPAQUE_FRAGMENT_SHADER}"
				"${GRAPHICS_PARTICLE_VERTEX_SHADER}"
				"${GRAPHICS_PARTICLE_FRAGMENT_SHADER}"
				"${GRAPHICS_SCREEN_DISTORTION_VERTEX_SHADER}"
				"${GRAPHICS_SCREEN_DISTORTION_FRAGMENT_SHADER}"
				"${GRAPHICS_BEAM_VERTEX_SHADER}"
				"${GRAPHICS_BEAM_FRAGMENT_SHADER}"
				"${GRAPHICS_VIDEO_VERTEX_SHADER}"
				"${GRAPHICS_VIDEO_FRAGMENT_SHADER}"
				"${GRAPHICS_RING_VERTEX_SHADER}"
				"${GRAPHICS_RING_FRAGMENT_SHADER}"
				"${GRAPHICS_FULLSCREEN_OVERLAY_VERTEX_SHADER}"
				"${GRAPHICS_FULLSCREEN_OVERLAY_FRAGMENT_SHADER}"
				"${GRAPHICS_WORLD_QUAD_VERTEX_SHADER}"
				"${GRAPHICS_WORLD_QUAD_FRAGMENT_SHADER}"
				"${GRAPHICS_TERRAIN_VERTEX_SHADER}"
				"${GRAPHICS_SURFACE_VERTEX_SHADER}"
				"${GRAPHICS_SCREEN_FILTER_VERTEX_SHADER}"
				"${GRAPHICS_SURFACE_FRAGMENT_SHADER}"
				"${GRAPHICS_SCREEN_FILTER_FRAGMENT_SHADER}"
				"${GRAPHICS_TERRAIN_FRAGMENT_SHADER}"
				"${GRAPHICS_UI_2D_VERTEX_SHADER}"
				"${GRAPHICS_UI_2D_FRAGMENT_SHADER}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${GRAPHICS_SHADER_ASSET_DIRECTORY}"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry basic_opaque_vertex
				-stage vertex
				-matrix-layout-row-major
				-o "${GRAPHICS_BASIC_OPAQUE_VERTEX_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/basic_opaque.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry basic_opaque_pixel
				-stage pixel
				-matrix-layout-row-major
				-o "${GRAPHICS_BASIC_OPAQUE_FRAGMENT_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/basic_opaque.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry basic_opaque_skinned_vertex
				-stage vertex
				-matrix-layout-row-major
				-o "${GRAPHICS_SKINNED_BASIC_OPAQUE_VERTEX_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/basic_opaque.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry particle_billboard_vertex
				-stage vertex
				-matrix-layout-row-major
				-o "${GRAPHICS_PARTICLE_VERTEX_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/particle_billboard.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry particle_billboard_pixel
				-stage pixel
				-matrix-layout-row-major
				-o "${GRAPHICS_PARTICLE_FRAGMENT_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/particle_billboard.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry screen_distortion_vertex
				-stage vertex
				-matrix-layout-row-major
				-o "${GRAPHICS_SCREEN_DISTORTION_VERTEX_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/screen_distortion.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry screen_distortion_pixel
				-stage pixel
				-matrix-layout-row-major
				-o "${GRAPHICS_SCREEN_DISTORTION_FRAGMENT_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/screen_distortion.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry beam_vertex
				-stage vertex
				-matrix-layout-row-major
				-o "${GRAPHICS_BEAM_VERTEX_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/beam.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry beam_pixel
				-stage pixel
				-matrix-layout-row-major
				-o "${GRAPHICS_BEAM_FRAGMENT_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/beam.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry video_vertex
				-stage vertex
				-matrix-layout-row-major
				-o "${GRAPHICS_VIDEO_VERTEX_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/video.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry video_pixel
				-stage pixel
				-matrix-layout-row-major
				-o "${GRAPHICS_VIDEO_FRAGMENT_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/video.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry ring_vertex
				-stage vertex
				-matrix-layout-row-major
				-o "${GRAPHICS_RING_VERTEX_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/ring.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry ring_pixel
				-stage pixel
				-matrix-layout-row-major
				-o "${GRAPHICS_RING_FRAGMENT_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/ring.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry fullscreen_overlay_vertex
				-stage vertex
				-matrix-layout-row-major
				-o "${GRAPHICS_FULLSCREEN_OVERLAY_VERTEX_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/fullscreen_overlay.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry fullscreen_overlay_pixel
				-stage pixel
				-matrix-layout-row-major
				-o "${GRAPHICS_FULLSCREEN_OVERLAY_FRAGMENT_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/fullscreen_overlay.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry world_quad_vertex
				-stage vertex
				-matrix-layout-row-major
				-o "${GRAPHICS_WORLD_QUAD_VERTEX_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/world_quad.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry world_quad_pixel
				-stage pixel
				-matrix-layout-row-major
				-o "${GRAPHICS_WORLD_QUAD_FRAGMENT_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/world_quad.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry ui_2d_vertex
				-stage vertex
				-matrix-layout-row-major
				-o "${GRAPHICS_UI_2D_VERTEX_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/ui_2d.slang"
			COMMAND $<TARGET_FILE:slang-bootstrap>
				-target dxbc
				-profile sm_5_0
				-entry ui_2d_pixel
				-stage pixel
				-matrix-layout-row-major
				-o "${GRAPHICS_UI_2D_FRAGMENT_SHADER}"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/ui_2d.slang"
            COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0
                -entry terrain_vertex -stage vertex -matrix-layout-row-major
                -o "${GRAPHICS_TERRAIN_VERTEX_SHADER}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/terrain.slang"
            COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0
                -entry terrain_pixel -stage pixel -matrix-layout-row-major
                -o "${GRAPHICS_TERRAIN_FRAGMENT_SHADER}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/terrain.slang"

            COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0
                -entry surface_vertex -stage vertex -matrix-layout-row-major
                -o "${GRAPHICS_SURFACE_VERTEX_SHADER}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/surface.slang"
            COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0
                -entry surface_pixel -stage pixel -matrix-layout-row-major
                -o "${GRAPHICS_SURFACE_FRAGMENT_SHADER}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/surface.slang"
            COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0
                -entry screen_filter_vertex -stage vertex -matrix-layout-row-major
                -o "${GRAPHICS_SCREEN_FILTER_VERTEX_SHADER}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/screen_filter.slang"
            COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0
                -entry screen_filter_pixel -stage pixel -matrix-layout-row-major
                -o "${GRAPHICS_SCREEN_FILTER_FRAGMENT_SHADER}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/screen_filter.slang"
			DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/environment_lighting.slangh"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/basic_opaque.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/particle_billboard.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/screen_distortion.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/beam.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/video.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/ring.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/fullscreen_overlay.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/world_quad.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/ui_2d.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/terrain.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/surface.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/screen_filter.slang"
				slang-bootstrap
			VERBATIM
		)
		add_custom_target(generals_graphics_shaders
			DEPENDS
				"${GRAPHICS_BASIC_OPAQUE_VERTEX_SHADER}"
				"${GRAPHICS_SKINNED_BASIC_OPAQUE_VERTEX_SHADER}"
				"${GRAPHICS_BASIC_OPAQUE_FRAGMENT_SHADER}"
				"${GRAPHICS_PARTICLE_VERTEX_SHADER}"
				"${GRAPHICS_PARTICLE_FRAGMENT_SHADER}"
				"${GRAPHICS_SCREEN_DISTORTION_VERTEX_SHADER}"
				"${GRAPHICS_SCREEN_DISTORTION_FRAGMENT_SHADER}"
				"${GRAPHICS_BEAM_VERTEX_SHADER}"
				"${GRAPHICS_BEAM_FRAGMENT_SHADER}"
				"${GRAPHICS_VIDEO_VERTEX_SHADER}"
				"${GRAPHICS_VIDEO_FRAGMENT_SHADER}"
				"${GRAPHICS_RING_VERTEX_SHADER}"
				"${GRAPHICS_RING_FRAGMENT_SHADER}"
				"${GRAPHICS_FULLSCREEN_OVERLAY_VERTEX_SHADER}"
				"${GRAPHICS_FULLSCREEN_OVERLAY_FRAGMENT_SHADER}"
				"${GRAPHICS_WORLD_QUAD_VERTEX_SHADER}"
				"${GRAPHICS_WORLD_QUAD_FRAGMENT_SHADER}"
				"${GRAPHICS_TERRAIN_VERTEX_SHADER}"
				"${GRAPHICS_SURFACE_VERTEX_SHADER}"
				"${GRAPHICS_SCREEN_FILTER_VERTEX_SHADER}"
				"${GRAPHICS_SURFACE_FRAGMENT_SHADER}"
				"${GRAPHICS_SCREEN_FILTER_FRAGMENT_SHADER}"
				"${GRAPHICS_TERRAIN_FRAGMENT_SHADER}"
				"${GRAPHICS_UI_2D_VERTEX_SHADER}"
				"${GRAPHICS_UI_2D_FRAGMENT_SHADER}"
		)
		add_dependencies(generals_graphics generals_graphics_shaders)
	endif()

    target_sources(generals_graphics
        PUBLIC
            FILE_SET CXX_MODULES
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
            FILES
                backends/dx11/DX11RHI.cppm
                backends/dx11/DX11Coexistence.cppm
    )

    target_link_libraries(generals_graphics PRIVATE d3d11)
    set_property(TARGET generals_graphics PROPERTY GRAPHICS_SHADER_ASSET_DIRECTORY "${GRAPHICS_SHADER_ASSET_DIRECTORY}")
endif()

option(RTS_BUILD_GRAPHICS_TESTS "Build generic graphics unit tests." OFF)

if(RTS_BUILD_GRAPHICS_TESTS)
    include(CTest)
    find_package(Boost CONFIG REQUIRED)

    function(add_graphics_test test_name test_source)
        add_executable(${test_name})

        target_sources(${test_name}
            PRIVATE
                FILE_SET CXX_MODULES
                BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                FILES
                    ${test_source}
        )

        target_compile_features(${test_name} PRIVATE cxx_std_20)
        target_include_directories(${test_name} PRIVATE ${Boost_INCLUDE_DIRS})
        target_link_libraries(${test_name} PRIVATE generals_graphics)

        add_test(NAME ${test_name} COMMAND ${test_name})
    endfunction()

    add_graphics_test(generals_graphics_terrain_visibility_test scene/terrain/TerrainVisibility.test.cppm)
    add_graphics_test(generals_graphics_terrain_geometry_test scene/terrain/TerrainGeometry.test.cppm)
    if(WIN32)
        add_graphics_test(generals_graphics_road_drawing_test scene/roads/RoadDrawing.test.cppm)
        add_graphics_test(generals_graphics_movie_capture_test capture/MovieCapture.test.cppm)
        add_graphics_test(generals_graphics_scene_renderers_test frame/SceneRenderers.test.cppm)
        target_compile_definitions(generals_graphics_scene_renderers_test PRIVATE
            GRAPHICS_SCENE_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_scene_renderers_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_scorch_drawing_test scene/scorches/ScorchDrawing.test.cppm)
        target_compile_definitions(generals_graphics_scorch_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_scorch_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_stencil_volume_drawing_test scene/shadows/StencilVolumeDrawing.test.cppm)
        add_graphics_test(generals_graphics_projected_shadow_drawing_test scene/shadows/ProjectedShadowDrawing.test.cppm)
        add_graphics_test(generals_graphics_directional_shadow_drawing_test scene/shadows/DirectionalShadowDrawing.test.cppm)
        target_compile_definitions(generals_graphics_directional_shadow_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_directional_shadow_drawing_test generals_graphics_shaders)
        endif()
        target_compile_definitions(generals_graphics_projected_shadow_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_projected_shadow_drawing_test generals_graphics_shaders)
        endif()
        target_compile_definitions(generals_graphics_stencil_volume_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_stencil_volume_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_bib_drawing_test scene/bibs/BibDrawing.test.cppm)
        target_compile_definitions(generals_graphics_bib_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_bib_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_line_drawing_test scene/lines/LineDrawing.test.cppm)
        target_compile_definitions(generals_graphics_line_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_line_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_screen_filter_drawing_test scene/screen/ScreenFilterDrawing.test.cppm)
        target_compile_definitions(generals_graphics_screen_filter_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_screen_filter_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_debug_drawing_test scene/debug/DebugDrawing.test.cppm)
        target_compile_definitions(generals_graphics_debug_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_debug_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_track_drawing_test scene/tracks/TrackDrawing.test.cppm)
        target_compile_definitions(generals_graphics_track_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_track_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_tree_drawing_test scene/trees/TreeDrawing.test.cppm)
        target_compile_definitions(generals_graphics_tree_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_tree_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_prop_drawing_test scene/props/PropDrawing.test.cppm)
        target_compile_definitions(generals_graphics_prop_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_prop_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_water_drawing_test scene/water/WaterDrawing.test.cppm)
        target_compile_definitions(generals_graphics_water_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_water_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_bridge_drawing_test scene/bridges/BridgeDrawing.test.cppm)
        target_compile_definitions(generals_graphics_bridge_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_bridge_drawing_test generals_graphics_shaders)
        endif()
        target_compile_definitions(generals_graphics_road_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_road_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_terrain_renderer_test scene/terrain/TerrainRenderer.test.cppm)
        target_compile_definitions(generals_graphics_terrain_renderer_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_terrain_renderer_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_transparent_drawing_test scene/props/TransparentDrawing.test.cppm)
        target_compile_definitions(generals_graphics_transparent_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_transparent_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_material_pass_drawing_test scene/props/MaterialPassDrawing.test.cppm)
        add_graphics_test(generals_graphics_screen_distortion_drawing_test scene/screen/ScreenDistortionDrawing.test.cppm)
        target_compile_definitions(generals_graphics_screen_distortion_drawing_test PRIVATE
            GRAPHICS_SCREEN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_screen_distortion_drawing_test generals_graphics_shaders)
        endif()
        target_compile_definitions(generals_graphics_material_pass_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_material_pass_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_model_texture_drawing_test scene/models/ModelTextureDrawing.test.cppm)
        target_compile_definitions(generals_graphics_model_texture_drawing_test PRIVATE
            GRAPHICS_MODEL_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_model_texture_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_particle_texture_drawing_test scene/particles/ParticleTextureDrawing.test.cppm)
        target_compile_definitions(generals_graphics_particle_texture_drawing_test PRIVATE
            GRAPHICS_PARTICLE_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_particle_texture_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_point_light_drawing_test scene/props/PointLightDrawing.test.cppm)
        target_compile_definitions(generals_graphics_point_light_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_point_light_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_line_material_drawing_test scene/lines/LineMaterialDrawing.test.cppm)
        target_compile_definitions(generals_graphics_line_material_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_line_material_drawing_test generals_graphics_shaders)
        endif()
        add_graphics_test(generals_graphics_glyph_atlas_drawing_test text/GlyphAtlasDrawing.test.cppm)
        target_compile_definitions(generals_graphics_glyph_atlas_drawing_test PRIVATE
            GRAPHICS_TERRAIN_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}")
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_glyph_atlas_drawing_test generals_graphics_shaders)
        endif()
    endif()



    add_graphics_test(
        generals_graphics_resource_handle_test
        resources/handles/ResourceHandle.test.cppm
    )

    add_graphics_test(
        generals_graphics_aligned_allocator_test
        memory/AlignedAllocator.test.cppm
    )

    add_graphics_test(
        generals_graphics_resource_pool_test
        resources/pools/ResourcePool.test.cppm
    )

    add_graphics_test(
        generals_graphics_mesh_test
        resources/meshes/Mesh.test.cppm
    )

    add_graphics_test(
        generals_graphics_texture_test
        resources/textures/Texture.test.cppm
    )

    add_graphics_test(
        generals_graphics_sampler_test
        resources/samplers/Sampler.test.cppm
    )

    add_graphics_test(
        generals_graphics_material_test
        resources/materials/Material.test.cppm
    )

    add_graphics_test(
        generals_graphics_gpu_resource_residency_test
        resources/residency/GPUResourceResidency.test.cppm
    )

    add_graphics_test(
        generals_graphics_bindless_resource_table_test
        resources/bindless/bindlessResourceTable.test.cppm
    )

    add_graphics_test(
        generals_graphics_parameter_layout_test
        shaders/ParameterLayout.test.cppm
    )

    add_graphics_test(
        generals_graphics_pipeline_test
        shaders/Pipeline.test.cppm
    )

    add_graphics_test(
        generals_graphics_shader_library_test
        shaders/ShaderLibrary.test.cppm
    )

    add_graphics_test(
        generals_graphics_lighting_test
        scene/lighting/Lighting.test.cppm
    )

    add_graphics_test(
        generals_graphics_light_renderer_test
        scene/lighting/LightRenderer.test.cppm
    )

    add_graphics_test(
        generals_graphics_environment_test
        scene/environment/Environment.test.cppm
    )

    add_graphics_test(
        generals_graphics_shadows_test
        scene/shadows/Shadows.test.cppm
    )

    add_graphics_test(
        generals_graphics_decals_test
        scene/decals/Decals.test.cppm
    )

    add_graphics_test(
        generals_graphics_decal_visibility_test
        scene/decals/DecalVisibility.test.cppm
    )

    add_graphics_test(
        generals_graphics_decal_draw_test
        scene/decals/DecalDraw.test.cppm
    )

    add_graphics_test(
        generals_graphics_transparency_test
        scene/transparency/Transparency.test.cppm
    )

    add_graphics_test(
        generals_graphics_render_scene_test
        scene/RenderScene.test.cppm
    )

    add_graphics_test(
        generals_graphics_attachment_graph_test
        scene/attachments/Attachments.test.cppm
    )
    if(WIN32)
        target_sources(generals_graphics_attachment_graph_test
            PRIVATE
                FILE_SET CXX_MODULES
                BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                FILES
                    tests/visual_regression/VisualRegression.cppm
        )
        target_compile_definitions(generals_graphics_attachment_graph_test PRIVATE
            GRAPHICS_ATTACHMENT_REFERENCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/scene/attachments/__snapshots__"
            GRAPHICS_ATTACHMENT_FAILURE_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/attachment_graph_failures"
            GRAPHICS_ATTACHMENT_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}"
        )
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_attachment_graph_test generals_graphics_shaders)
        endif()
    endif()

    add_graphics_test(
        generals_graphics_model_visibility_test
        scene/models/ModelVisibility.test.cppm
    )

    add_graphics_test(
        generals_graphics_skeleton_test
        scene/models/Skeleton.test.cppm
    )

    add_graphics_test(
        generals_graphics_model_instance_test
        scene/models/ModelInstance.test.cppm
    )

    add_graphics_test(
        generals_graphics_animation_test
        scene/models/Animation.test.cppm
    )

    add_graphics_test(
        generals_graphics_animation_blend_test
        scene/models/AnimationBlend.test.cppm
    )

    add_graphics_test(
        generals_graphics_skinning_test
        scene/models/Skinning.test.cppm
    )

    add_graphics_test(
        generals_graphics_static_mesh_renderer_test
        scene/meshes/StaticMeshRenderer.test.cppm
    )

    add_graphics_test(
        generals_graphics_model_asset_binding_test
        scene/models/ModelAssetBinding.test.cppm
    )

    add_graphics_test(
        generals_graphics_model_asset_geometry_test
        scene/models/ModelAssetGeometry.test.cppm
    )
    if(WIN32)
        target_sources(generals_graphics_static_mesh_renderer_test
            PRIVATE
                FILE_SET CXX_MODULES
                BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                FILES
                    tests/visual_regression/VisualRegression.cppm
        )
        target_compile_definitions(generals_graphics_static_mesh_renderer_test PRIVATE
            GRAPHICS_STATIC_MESH_REFERENCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/scene/meshes/__snapshots__"
            GRAPHICS_STATIC_MESH_FAILURE_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/static_mesh_renderer_failures"
            GRAPHICS_STATIC_MESH_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}"
        )
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_static_mesh_renderer_test generals_graphics_shaders)
        endif()
    endif()

    add_graphics_test(
        generals_graphics_view_test
        scene/views/View.test.cppm
    )

    add_graphics_test(
        generals_graphics_visibility_test
        scene/visibility/Visibility.test.cppm
    )

    add_graphics_test(
        generals_graphics_lod_test
        scene/lod/LOD.test.cppm
    )

    add_graphics_test(
        generals_graphics_gpu_scene_test
        scene/gpu/GPUScene.test.cppm
    )

    add_graphics_test(
        generals_graphics_draw_generation_test
        scene/draw/DrawGeneration.test.cppm
    )

    add_graphics_test(
        generals_graphics_render_graph_test
        render-graph/RenderGraph.test.cppm
    )

    add_graphics_test(
        generals_graphics_render_graph_execution_test
        render-graph/RenderGraphExecution.test.cppm
    )

    add_graphics_test(
        generals_graphics_renderer2d_test
        renderer2d/Renderer2D.test.cppm
    )

    add_graphics_test(
        generals_graphics_opaque_pass_test
        passes/opaque/OpaquePass.test.cppm
    )
    if(WIN32)
        target_compile_definitions(generals_graphics_opaque_pass_test PRIVATE
            GRAPHICS_DX11_TEST_SHADER_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests/visual_regression/shaders/precompiled/dx11"
        )
    endif()

    add_graphics_test(
        generals_graphics_shadow_pass_test
        passes/shadow/ShadowPass.test.cppm
    )

    add_graphics_test(
        generals_graphics_transparent_pass_test
        passes/transparent/TransparentPass.test.cppm
    )

    add_graphics_test(
        generals_graphics_decal_pass_test
        passes/decal/DecalPass.test.cppm
    )

    add_graphics_test(
        generals_graphics_particles_test
        scene/particles/Particles.test.cppm
    )

    add_graphics_test(
        generals_graphics_particle_visibility_test
        scene/particles/ParticleVisibility.test.cppm
    )

    add_graphics_test(
        generals_graphics_particle_draw_test
        scene/particles/ParticleDraw.test.cppm
    )

    add_graphics_test(
        generals_graphics_particle_pass_test
        passes/particles/ParticlePass.test.cppm
    )

    add_graphics_test(
        generals_graphics_weather_particles_test
        scene/weather/WeatherParticles.test.cppm
    )

    add_graphics_test(
        generals_graphics_world_quads_test
        scene/world/WorldQuads.test.cppm
    )

    if(WIN32)
        add_graphics_test(
            generals_graphics_particle_renderer_test
            scene/particles/ParticleRenderer.test.cppm
        )
        target_sources(generals_graphics_particle_renderer_test
            PRIVATE
                FILE_SET CXX_MODULES
                BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                FILES
                    tests/visual_regression/VisualRegression.cppm
        )
        target_compile_definitions(generals_graphics_particle_renderer_test PRIVATE
            GRAPHICS_PARTICLE_REFERENCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/scene/particles/__snapshots__"
            GRAPHICS_PARTICLE_FAILURE_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/particle_renderer_failures"
            GRAPHICS_PARTICLE_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}"
        )
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_particle_renderer_test generals_graphics_shaders)
        endif()
    endif()


    add_graphics_test(
        generals_graphics_beam_test
        scene/beams/beam.test.cppm
    )

    add_graphics_test(
        generals_graphics_ring_test
        scene/ring/Ring.test.cppm
    )

    add_graphics_test(
        generals_graphics_fullscreen_overlay_test
        scene/screen/FullscreenOverlay.test.cppm
    )

    add_graphics_test(
        generals_graphics_post_process_test
        passes/post-process/PostProcess.test.cppm
    )

    add_graphics_test(
        generals_graphics_sky_pass_test
        passes/sky/SkyPass.test.cppm
    )

    if(WIN32)
        add_graphics_test(
            generals_graphics_video_renderer_test
            video/VideoRenderer.test.cppm
        )
        target_sources(generals_graphics_video_renderer_test
            PRIVATE
                FILE_SET CXX_MODULES
                BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                FILES
                    tests/visual_regression/VisualRegression.cppm
        )
        target_compile_definitions(generals_graphics_video_renderer_test PRIVATE
            GRAPHICS_VIDEO_REFERENCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/video/__snapshots__"
            GRAPHICS_VIDEO_FAILURE_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/video_renderer_failures"
            GRAPHICS_VIDEO_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}"
        )
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_video_renderer_test generals_graphics_shaders)
        endif()
    endif()

    if(WIN32)
        add_graphics_test(
            generals_graphics_w3d_e2e_test
            test_scenarios/w3d/W3DE2E.test.cppm
        )
        target_sources(generals_graphics_w3d_e2e_test
            PRIVATE
                FILE_SET CXX_MODULES
                BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                FILES
                    tests/visual_regression/VisualRegression.cppm
        )
        file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/GeneralsMD/Run/Art/W3D" GRAPHICS_W3D_E2E_MODEL_DIRECTORY_CMAKE)
        file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/GeneralsMD/Run/Art/Textures" GRAPHICS_W3D_E2E_TEXTURE_DIRECTORY_CMAKE)
        target_compile_definitions(generals_graphics_w3d_e2e_test PRIVATE
            GRAPHICS_W3D_E2E_MODEL_DIRECTORY="${GRAPHICS_W3D_E2E_MODEL_DIRECTORY_CMAKE}"
            GRAPHICS_W3D_E2E_TEXTURE_DIRECTORY="${GRAPHICS_W3D_E2E_TEXTURE_DIRECTORY_CMAKE}"
            GRAPHICS_W3D_E2E_REFERENCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/test_scenarios/w3d/__snapshots__"
            GRAPHICS_W3D_E2E_FAILURE_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/w3d_e2e_failures"
            GRAPHICS_W3D_E2E_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}"
        )

        add_graphics_test(
            generals_graphics_visual_regression_test
            tests/visual_regression/VisualRegression.test.cppm
        )
        target_sources(generals_graphics_visual_regression_test
            PRIVATE
                FILE_SET CXX_MODULES
                BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                FILES
                    tests/visual_regression/VisualRegression.cppm
        )
        target_compile_definitions(generals_graphics_visual_regression_test PRIVATE
            GRAPHICS_VISUAL_REFERENCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests/visual_regression/references"
            GRAPHICS_VISUAL_FAILURE_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/visual_regression_failures"
            GRAPHICS_VISUAL_SHADER_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests/visual_regression/shaders/precompiled/dx11"
            GRAPHICS_RENDERER_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}"
        )
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_visual_regression_test generals_graphics_shaders)
        endif()

        add_graphics_test(
            generals_graphics_weather_particles_snapshot_test
            scene/weather/WeatherParticles.snapshot.test.cppm
        )
        target_sources(generals_graphics_weather_particles_snapshot_test
            PRIVATE
                FILE_SET CXX_MODULES
                BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                FILES
                    tests/visual_regression/VisualRegression.cppm
        )
        target_compile_definitions(generals_graphics_weather_particles_snapshot_test PRIVATE
            GRAPHICS_WEATHER_REFERENCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/scene/weather/__snapshots__"
            GRAPHICS_WEATHER_FAILURE_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/weather_particle_snapshot_failures"
            GRAPHICS_WEATHER_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}"
        )
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_weather_particles_snapshot_test generals_graphics_shaders)
        endif()

        add_graphics_test(
            generals_graphics_ring_snapshot_test
            scene/ring/Ring.snapshot.test.cppm
        )
        target_sources(generals_graphics_ring_snapshot_test
            PRIVATE
                FILE_SET CXX_MODULES
                BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                FILES
                    tests/visual_regression/VisualRegression.cppm
        )
        target_compile_definitions(generals_graphics_ring_snapshot_test PRIVATE
            GRAPHICS_RING_SNAPSHOT_REFERENCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/scene/ring/__snapshots__"
            GRAPHICS_RING_SNAPSHOT_FAILURE_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/ring_snapshot_failures"
            GRAPHICS_RING_SNAPSHOT_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}"
        )
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_ring_snapshot_test generals_graphics_shaders)
        endif()

        add_graphics_test(
            generals_graphics_world_quads_snapshot_test
            scene/world/WorldQuads.snapshot.test.cppm
        )
        target_sources(generals_graphics_world_quads_snapshot_test
            PRIVATE
                FILE_SET CXX_MODULES
                BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                FILES
                    tests/visual_regression/VisualRegression.cppm
        )
        target_compile_definitions(generals_graphics_world_quads_snapshot_test PRIVATE
            GRAPHICS_WORLD_QUAD_REFERENCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/scene/world/__snapshots__"
            GRAPHICS_WORLD_QUAD_FAILURE_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/world_quad_snapshot_failures"
            GRAPHICS_WORLD_QUAD_SHADER_DIRECTORY="${GRAPHICS_SHADER_ASSET_DIRECTORY}"
        )
        if(TARGET generals_graphics_shaders)
            add_dependencies(generals_graphics_world_quads_snapshot_test generals_graphics_shaders)
        endif()
    endif()

    add_graphics_test(
        generals_graphics_rhi_test
        rhi/RHI.test.cppm
    )

    add_graphics_test(
        generals_graphics_frame_test
        frame/FrameOwner.test.cppm
    )

    if(WIN32)
        add_graphics_test(
            generals_graphics_dx11_rhi_test
            backends/dx11/DX11RHI.test.cppm
        )
        target_compile_definitions(generals_graphics_dx11_rhi_test PRIVATE
            GRAPHICS_DX11_TEST_SHADER_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests/visual_regression/shaders/precompiled/dx11"
        )
    endif()
endif()

if(WIN32 AND TARGET slang-bootstrap)
    add_custom_command(OUTPUT "${GRAPHICS_SHADER_ASSET_DIRECTORY}/tree.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/tree.pso"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry tree_vertex -stage vertex -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/tree.vso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/tree.slang"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry tree_pixel -stage pixel -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/tree.pso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/tree.slang"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/environment_lighting.slangh" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/tree.slang" slang-bootstrap VERBATIM)
    add_custom_target(generals_graphics_tree_shaders DEPENDS "${GRAPHICS_SHADER_ASSET_DIRECTORY}/tree.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/tree.pso")
    add_dependencies(generals_graphics generals_graphics_tree_shaders)
endif()

if(TARGET generals_graphics_tree_drawing_test AND TARGET generals_graphics_tree_shaders)
    add_dependencies(generals_graphics_tree_drawing_test generals_graphics_tree_shaders)
endif()


if(WIN32 AND TARGET slang-bootstrap)
    add_custom_command(OUTPUT "${GRAPHICS_SHADER_ASSET_DIRECTORY}/ocean.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/ocean.pso"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry OceanVertexMain -stage vertex -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/ocean.vso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/ocean.slang"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry OceanPixelMain -stage pixel -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/ocean.pso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/ocean.slang"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/environment_lighting.slangh" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/ocean.slang" slang-bootstrap VERBATIM)
    add_custom_target(generals_graphics_ocean_shaders DEPENDS "${GRAPHICS_SHADER_ASSET_DIRECTORY}/ocean.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/ocean.pso")
    add_dependencies(generals_graphics generals_graphics_ocean_shaders)
endif()

if(WIN32 AND TARGET slang-bootstrap)
    add_custom_command(OUTPUT "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_surface.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_surface.pso"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry WaterSurfaceVertexMain -stage vertex -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_surface.vso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/water_surface.slang"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry WaterSurfacePixelMain -stage pixel -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_surface.pso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/water_surface.slang"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/environment_lighting.slangh" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/water_surface.slang" slang-bootstrap VERBATIM)
    add_custom_target(generals_graphics_water_surface_shaders DEPENDS "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_surface.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_surface.pso")
    add_dependencies(generals_graphics generals_graphics_water_surface_shaders)
endif()

if(WIN32 AND TARGET slang-bootstrap)
    add_custom_command(OUTPUT "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_displacement.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_displacement.pso"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry WaterDisplacementVertexMain -stage vertex -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_displacement.vso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/water_displacement.slang"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry WaterDisplacementPixelMain -stage pixel -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_displacement.pso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/water_displacement.slang"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/water_displacement.slang" slang-bootstrap VERBATIM)
    add_custom_target(generals_graphics_water_displacement_shaders DEPENDS "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_displacement.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_displacement.pso")
    add_dependencies(generals_graphics generals_graphics_water_displacement_shaders)
endif()

if(WIN32 AND TARGET slang-bootstrap)
    add_custom_command(OUTPUT "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_sky.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_sky.pso"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry WaterSkyVertexMain -stage vertex -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_sky.vso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/water_sky.slang"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry WaterSkyPixelMain -stage pixel -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_sky.pso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/water_sky.slang"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/water_sky.slang" slang-bootstrap VERBATIM)
    add_custom_target(generals_graphics_water_sky_shaders DEPENDS "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_sky.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_sky.pso")
    add_dependencies(generals_graphics generals_graphics_water_sky_shaders)
endif()

if(WIN32 AND TARGET slang-bootstrap)
    add_custom_command(OUTPUT "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_track.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_track.pso"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry WaterTrackVertexMain -stage vertex -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_track.vso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/water_track.slang"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry WaterTrackPixelMain -stage pixel -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_track.pso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/water_track.slang"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/water_track.slang" slang-bootstrap VERBATIM)
    add_custom_target(generals_graphics_water_track_shaders DEPENDS "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_track.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/water_track.pso")
    add_dependencies(generals_graphics generals_graphics_water_track_shaders)
endif()

if(WIN32 AND TARGET slang-bootstrap)
    add_custom_command(OUTPUT "${GRAPHICS_SHADER_ASSET_DIRECTORY}/prop.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/prop.pso"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry prop_vertex -stage vertex -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/prop.vso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/prop.slang"
        COMMAND $<TARGET_FILE:slang-bootstrap> -target dxbc -profile sm_5_0 -entry prop_pixel -stage pixel -matrix-layout-row-major -o "${GRAPHICS_SHADER_ASSET_DIRECTORY}/prop.pso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/prop.slang"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/environment_lighting.slangh" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/prop.slang" slang-bootstrap VERBATIM)
    add_custom_target(generals_graphics_prop_shaders DEPENDS "${GRAPHICS_SHADER_ASSET_DIRECTORY}/prop.vso" "${GRAPHICS_SHADER_ASSET_DIRECTORY}/prop.pso")
    add_dependencies(generals_graphics generals_graphics_prop_shaders)
endif()
