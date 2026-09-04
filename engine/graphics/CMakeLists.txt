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
            scene/RenderScene.cppm
            scene/meshes/StaticMeshRenderer.cppm
            scene/lighting/Lighting.cppm
            scene/lighting/LightRenderer.cppm
            scene/environment/Environment.cppm
            scene/shadows/Shadows.cppm
            scene/decals/Decals.cppm
            scene/decals/DecalVisibility.cppm
            scene/decals/DecalDraw.cppm
            scene/particles/Particles.cppm
            scene/particles/ParticleVisibility.cppm
            scene/particles/ParticleDraw.cppm
            scene/particles/ParticleRenderer.cppm
            scene/screen/ScreenDistortion.cppm
            scene/beams/Beam.cppm
            scene/models/ModelVisibility.cppm
            scene/models/Skeleton.cppm
            scene/models/ModelInstance.cppm
            scene/models/Animation.cppm
            scene/models/Skinning.cppm
            scene/transparency/Transparency.cppm
            scene/views/View.cppm
            scene/visibility/Visibility.cppm
            scene/lod/LOD.cppm
            scene/gpu/GPUScene.cppm
            scene/draw/DrawGeneration.cppm
            render-graph/RenderGraph.cppm
            render-graph/RenderGraphExecution.cppm
            rhi/RHI.cppm
            rhi/Frame.cppm
            passes/opaque/OpaquePass.cppm
            passes/shadow/ShadowPass.cppm
            passes/transparent/TransparentPass.cppm
            passes/decal/DecalPass.cppm
            passes/particles/ParticlePass.cppm
            passes/post-process/PostProcess.cppm
            passes/sky/SkyPass.cppm
)

target_compile_features(generals_graphics PUBLIC cxx_std_20)

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
			DEPENDS
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/basic_opaque.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/particle_billboard.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/screen_distortion.slang"
				"${CMAKE_CURRENT_SOURCE_DIR}/shaders/source/beam.slang"
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

option(RTS_BUILD_GRAPHICS_TESTS "Build modern graphics unit tests." OFF)

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
        resources/bindless/BindlessResourceTable.test.cppm
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
        generals_graphics_skinning_test
        scene/models/Skinning.test.cppm
    )

    add_graphics_test(
        generals_graphics_static_mesh_renderer_test
        scene/meshes/StaticMeshRenderer.test.cppm
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
        scene/beams/Beam.test.cppm
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
    endif()

    add_graphics_test(
        generals_graphics_rhi_test
        rhi/RHI.test.cppm
    )

    add_graphics_test(
        generals_graphics_frame_test
        rhi/Frame.test.cppm
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
