module;

#define BOOST_TEST_MODULE GraphicsDX11RHITests

#include <boost/test/included/unit_test.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <windows.h>

export module Graphics.Backends.DX11.Tests;

#ifndef GRAPHICS_DX11_TEST_SHADER_DIRECTORY
#define GRAPHICS_DX11_TEST_SHADER_DIRECTORY "."
#endif

import Graphics.Backends.DX11;
import Graphics.Backends.DX11.FrameRuntime;
import Graphics.RenderGraph.Frame;
import Graphics.Passes.Opaque;
import Graphics.Resources.Bindless.BindlessResourceTable;
import Graphics.Resources.Residency.GPUResourceResidency;

using namespace Graphics;

BOOST_AUTO_TEST_CASE(dx11_coexistence_requires_a_shared_device)
{
	Graphics_DX11_Shutdown_Shared_Frame();
	BOOST_CHECK(!Register_Frame_Draw_Executor(nullptr, nullptr));
	BOOST_CHECK(!Graphics_DX11_Begin_Frame());
	BOOST_CHECK(!Graphics_DX11_Execute_Queued_Draws());
	BOOST_CHECK(!Graphics_DX11_End_Frame());
	BOOST_CHECK(!Graphics_DX11_Present());
	Graphics_DX11_Abort_Frame();
	Graphics_DX11_Shutdown_Shared_Frame();
}

static LRESULT CALLBACK Frame_Test_Window_Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	return DefWindowProcA(window, message, wparam, lparam);
}

static HWND Create_Frame_Test_Window()
{
	constexpr char class_name[] = "GraphicsDX11FrameTestWindow";
	const HINSTANCE instance = GetModuleHandleA(nullptr);
	WNDCLASSEXA window_class{};
	window_class.cbSize = sizeof(window_class);
	window_class.lpfnWndProc = Frame_Test_Window_Proc;
	window_class.hInstance = instance;
	window_class.lpszClassName = class_name;
	RegisterClassExA(&window_class);
	return CreateWindowExA(0, class_name, class_name, WS_OVERLAPPEDWINDOW, 0, 0, 32, 32, nullptr, nullptr, instance, nullptr);
}

static DX11DeviceOptions Make_DX11_Test_Options()
{
	DX11DeviceOptions options;
	options.use_warp = true;
	options.shader_directory = GRAPHICS_DX11_TEST_SHADER_DIRECTORY;
	return options;
}

BOOST_AUTO_TEST_CASE(frame_owner_preserves_retained_attachments_and_recovers_after_release)
{
    struct RuntimeScope {
        HWND window = Create_Frame_Test_Window();
        ~RuntimeScope() {
            Graphics_DX11_Shutdown_Shared_Frame();
            DestroyWindow(window);
        }
    } scope;
    BOOST_REQUIRE(scope.window != nullptr);
    auto options = Make_DX11_Test_Options();
    options.window = scope.window;
    options.width = 16;
    options.height = 16;
    options.backbuffer_format = RHITextureFormat::BGRA8_UNorm;
    BOOST_REQUIRE(Initialize_Frame_Device(options));
    BOOST_CHECK(!Initialize_Frame_Device(options));
    auto* device = Shared_Frame_Device();
    BOOST_REQUIRE(device != nullptr);
    BOOST_CHECK(device->Get_Status() == RHIDeviceStatus::Ready);
    RHIAdapterInfo adapter;
    BOOST_REQUIRE(device->Get_Adapter_Info(adapter));
    BOOST_CHECK_NE(adapter.vendor_id, 0u);
    const auto limits = device->Texture_Limits();
    BOOST_CHECK_GE(limits.max_2d_extent, 32u);
    BOOST_CHECK_GE(limits.max_3d_extent, 32u);

    const auto draw_and_check = [&] {
        BOOST_REQUIRE(Graphics_DX11_Begin_Frame());
        BOOST_CHECK(!Resize_Frame_Device(24, 24, false));
        auto& commands = device->Immediate_Command_List();
        BOOST_REQUIRE(commands.Clear({1, 0.5f, 0, 1}, 0.25f));
        const auto target = device->Get_Swap_Chain().Backbuffer();
        std::vector<std::byte> pixels(target.width * target.height * 4);
        BOOST_REQUIRE(device->Readback_Texture(target.texture, pixels, target.width * 4));
        for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4) {
            BOOST_CHECK_EQUAL(std::to_integer<unsigned>(pixels[pixel]), 0u);
            BOOST_CHECK_EQUAL(std::to_integer<unsigned>(pixels[pixel + 1]), 128u);
            BOOST_CHECK_EQUAL(std::to_integer<unsigned>(pixels[pixel + 2]), 255u);
            BOOST_CHECK_EQUAL(std::to_integer<unsigned>(pixels[pixel + 3]), 255u);
        }
        const auto depth = device->Get_Swap_Chain().Depth_Target();
        std::vector<std::uint32_t> depth_pixels(depth.width * depth.height);
        BOOST_REQUIRE(device->Readback_Texture(depth.texture, std::as_writable_bytes(std::span(depth_pixels)), depth.width * 4));
        for (const auto pixel : depth_pixels) {
            BOOST_CHECK_LE(std::abs(static_cast<int>(pixel & 0x00ffffffu) - 0x00400000), 1);
        }
        BOOST_REQUIRE(Graphics_DX11_Execute_Queued_Draws());
        BOOST_REQUIRE(Graphics_DX11_End_Frame());
        BOOST_REQUIRE(Graphics_DX11_Present());
    };
    draw_and_check();
    const auto old_target = device->Get_Swap_Chain().Backbuffer().texture;
    const auto old_depth = device->Get_Swap_Chain().Depth_Target().texture;
    BOOST_REQUIRE(device->Retain_Texture(old_target));
    BOOST_REQUIRE(device->Retain_Texture(old_depth));
    BOOST_CHECK(!Resize_Frame_Device(32, 24, false));
    BOOST_CHECK(device->Get_Swap_Chain().Backbuffer().texture == old_target);
    BOOST_CHECK(device->Get_Swap_Chain().Depth_Target().texture == old_depth);
    draw_and_check();
    BOOST_REQUIRE(device->Destroy_Texture(old_target));
    BOOST_CHECK(!Resize_Frame_Device(32, 24, false));
    draw_and_check();
    BOOST_REQUIRE(device->Destroy_Texture(old_depth));
    BOOST_REQUIRE(Resize_Frame_Device(32, 24, false));
    BOOST_CHECK(Shared_Frame_Device() == device);
    BOOST_CHECK_EQUAL(device->Get_Swap_Chain().Backbuffer().width, 32u);
    BOOST_CHECK_EQUAL(device->Get_Swap_Chain().Backbuffer().height, 24u);
    BOOST_CHECK(device->Get_Swap_Chain().Backbuffer().texture != old_target);
    BOOST_CHECK(!device->Retain_Texture(old_target));
    BOOST_CHECK(!device->Retain_Texture(old_depth));
    BOOST_CHECK(device->Get_Status() == RHIDeviceStatus::Ready);
    RHIAdapterInfo resized_adapter;
    BOOST_REQUIRE(device->Get_Adapter_Info(resized_adapter));
    BOOST_CHECK_EQUAL(resized_adapter.vendor_id, adapter.vendor_id);
    BOOST_CHECK_EQUAL(resized_adapter.device_id, adapter.device_id);
    draw_and_check();
    Detach_Frame_Draw_Executor();
    BOOST_CHECK(Shared_Frame_Device() == device);
    draw_and_check();
}

BOOST_AUTO_TEST_CASE(recycled_native_storage_preserves_logical_bounds_and_handle_generations)
{
    DX11Device device(Make_DX11_Test_Options());
    const std::array<std::byte,12> contents{};
    for (auto usage : {RHIBufferUsage::Vertex,RHIBufferUsage::Index}) {
        const auto old = device.Create_Buffer_Initialized({12,usage,4},contents);
        BOOST_REQUIRE(old.Is_Valid());
        BOOST_REQUIRE(device.Destroy_Buffer(old));
        const auto replacement = device.Create_Buffer_Initialized({12,usage,4},contents);
        BOOST_REQUIRE(replacement.Is_Valid());
        BOOST_CHECK(old != replacement);
        BOOST_CHECK(!device.Destroy_Buffer(old));
        BOOST_CHECK(!device.Update_Buffer(old,0,contents));
        BOOST_CHECK(!device.Update_Buffer(replacement,12,std::span(contents).first(1)));
        BOOST_CHECK(device.Update_Buffer(replacement,0,contents));
        BOOST_REQUIRE(device.Destroy_Buffer(replacement));
    }
}

BOOST_AUTO_TEST_CASE(dx11_backend_exercises_the_public_rhi)
{
	DX11Device device(Make_DX11_Test_Options());
	BOOST_REQUIRE(device.Is_Valid());

	const RHIBufferHandle vertex_buffer = device.Create_Buffer({108, RHIBufferUsage::Vertex, 36});
	const RHIBufferHandle index_buffer = device.Create_Buffer({12, RHIBufferUsage::Index, 0});
	const RHIBufferHandle constants = device.Create_Buffer({16, RHIBufferUsage::Constant, 16});
	const RHIBufferHandle instances = device.Create_Buffer({96, RHIBufferUsage::Storage, 96});
	const RHITextureHandle texture = device.Create_Texture({4, 4, 1, RHITextureFormat::RGBA8_UNorm, static_cast<std::uint32_t>(RHITextureUsage::ShaderResource)});
	const RHITextureHandle color_target = device.Create_Texture({16, 16, 1, RHITextureFormat::RGBA8_UNorm, static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
	const RHITextureHandle depth_target = device.Create_Texture({16, 16, 1, RHITextureFormat::D24_UNorm_S8, static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
	const RHIPipelineHandle pipeline = device.Create_Pipeline({7});

	BOOST_REQUIRE(vertex_buffer != nullptr);
	BOOST_REQUIRE(index_buffer != nullptr);
	BOOST_REQUIRE(constants != nullptr);
	BOOST_REQUIRE(instances != nullptr);
	BOOST_REQUIRE(texture != nullptr);
	BOOST_REQUIRE(color_target != nullptr);
	BOOST_REQUIRE(depth_target != nullptr);
	BOOST_REQUIRE(pipeline != nullptr);
	BindlessResourceTable bindless_resources;
	const ResourceIndex instance_index = bindless_resources.Register_Buffer(instances);
	const ResourceIndex texture_index = bindless_resources.Register_Texture(TextureHandle(0, 1), texture);
	const ResourceIndex material_index = bindless_resources.Register_Material(MaterialHandle(0, 1), constants);
	BOOST_REQUIRE(instance_index.Is_Valid());
	BOOST_REQUIRE(texture_index.Is_Valid());
	BOOST_REQUIRE(material_index.Is_Valid());

	CommandList &command_list = device.Immediate_Command_List();
	BOOST_REQUIRE(command_list.Bind_Pipeline(pipeline));
	BOOST_REQUIRE(command_list.Set_Render_Targets(color_target, depth_target));
	BOOST_REQUIRE(command_list.Clear({0.1f, 0.2f, 0.3f, 1.0f}, 1.0f));
	BOOST_REQUIRE(command_list.Set_Viewport({0, 0, 16, 16, 0.0f, 1.0f}));
	BOOST_REQUIRE(command_list.Set_Bindless_Resources(bindless_resources.Resources()));
	BOOST_REQUIRE(command_list.Set_Vertex_Buffer(0, vertex_buffer, 36, 0));
	BOOST_REQUIRE(command_list.Set_Index_Buffer(index_buffer, RHIIndexFormat::UInt16, 0));
	BOOST_REQUIRE(command_list.Draw(3));
	BOOST_REQUIRE(command_list.Draw_Indexed(3));
	const RHISubmissionCounts counts = command_list.Submission_Counts();
	BOOST_CHECK_EQUAL(counts.draw_calls, 2u);
	BOOST_CHECK_EQUAL(counts.triangles, 2u);
	BOOST_CHECK_EQUAL(counts.vertex_invocations, 6u);

	BOOST_CHECK(device.Destroy_Pipeline(pipeline));
	BOOST_CHECK(device.Destroy_Texture(depth_target));
	BOOST_CHECK(device.Destroy_Texture(color_target));
	BOOST_CHECK(device.Destroy_Texture(texture));
	BOOST_CHECK(device.Destroy_Buffer(instances));
	BOOST_CHECK(device.Destroy_Buffer(constants));
	BOOST_CHECK(device.Destroy_Buffer(index_buffer));
	BOOST_CHECK(device.Destroy_Buffer(vertex_buffer));
}

BOOST_AUTO_TEST_CASE(dx11_draw_submission_counts_follow_successful_topologies_and_pixels)
{
	struct SubmissionTestVertex final
	{
		float position[3];
		float color[4];
		float uv[2];
	};
	const std::array<SubmissionTestVertex, 3> vertices{{
		{{-0.8f, -0.8f, 0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
		{{0.0f, 0.8f, 0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}},
		{{0.8f, -0.8f, 0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}}
	}};
	const std::array<std::uint16_t, 6> indices{{0, 1, 2, 0, 1, 2}};
	const std::array<float, 8> material_data{{1, 1, 1, 1, 0, 0, 0, 0}};
	DX11Device device(Make_DX11_Test_Options());
	BOOST_REQUIRE(device.Is_Valid());
	const RHIBufferHandle material_buffer = device.Create_Buffer_Initialized(
		{static_cast<std::uint32_t>(sizeof(material_data)), RHIBufferUsage::Constant, 16},
		std::as_bytes(std::span<const float>(material_data)));
	BOOST_REQUIRE(material_buffer.Is_Valid());
	BindlessResourceTable material_resources;
	BOOST_REQUIRE(material_resources.Register_Material(MaterialHandle(0, 1), material_buffer).Is_Valid());
	const RHIBufferHandle vertex_buffer = device.Create_Buffer_Initialized(
		{static_cast<std::uint32_t>(sizeof(vertices)), RHIBufferUsage::Vertex, static_cast<std::uint32_t>(sizeof(SubmissionTestVertex))},
		std::as_bytes(std::span<const SubmissionTestVertex>(vertices)));
	const RHIBufferHandle index_buffer = device.Create_Buffer_Initialized(
		{static_cast<std::uint32_t>(sizeof(indices)), RHIBufferUsage::Index, 0},
		std::as_bytes(std::span<const std::uint16_t>(indices)));
	const RHITextureHandle color_target = device.Create_Texture({16, 16, 1, RHITextureFormat::RGBA8_UNorm,
		static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
	const RHITextureHandle depth_target = device.Create_Texture({16, 16, 1, RHITextureFormat::D24_UNorm_S8,
		static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
	RHIPipeline strip_description{8};
	strip_description.topology = RHIPrimitiveTopology::TriangleStrip;
	RHIPipeline point_description{9};
	point_description.topology = RHIPrimitiveTopology::PointList;
	const RHIPipelineHandle list_pipeline = device.Create_Pipeline({7});
	const RHIPipelineHandle strip_pipeline = device.Create_Pipeline(strip_description);
	const RHIPipelineHandle point_pipeline = device.Create_Pipeline(point_description);
	BOOST_REQUIRE(vertex_buffer.Is_Valid());
	BOOST_REQUIRE(index_buffer.Is_Valid());
	BOOST_REQUIRE(color_target.Is_Valid());
	BOOST_REQUIRE(depth_target.Is_Valid());
	BOOST_REQUIRE(list_pipeline.Is_Valid());
	BOOST_REQUIRE(strip_pipeline.Is_Valid());
	BOOST_REQUIRE(point_pipeline.Is_Valid());

	CommandList &commands = device.Immediate_Command_List();
	BOOST_REQUIRE(commands.Set_Render_Targets(color_target, depth_target));
	BOOST_REQUIRE(commands.Set_Viewport({0, 0, 16, 16, 0.0f, 1.0f}));
	BOOST_REQUIRE(commands.Clear({0.0f, 0.0f, 1.0f, 1.0f}, 1.0f));
	BOOST_REQUIRE(commands.Set_Vertex_Buffer(0, vertex_buffer, static_cast<std::uint32_t>(sizeof(SubmissionTestVertex)), 0));
	BOOST_REQUIRE(commands.Set_Index_Buffer(index_buffer, RHIIndexFormat::UInt16, 0));
	BOOST_REQUIRE(commands.Bind_Pipeline(list_pipeline));
	BOOST_REQUIRE(commands.Set_Bindless_Resources(material_resources.Resources()));
	BOOST_REQUIRE(commands.Draw(3));
	BOOST_CHECK_EQUAL(commands.Submission_Counts().draw_calls, 1u);
	BOOST_CHECK_EQUAL(commands.Submission_Counts().triangles, 1u);
	BOOST_CHECK_EQUAL(commands.Submission_Counts().vertex_invocations, 3u);

	BOOST_REQUIRE(commands.Draw_Indexed(3, 0, 0, 2));
	const RHISubmissionCounts indexed_counts = commands.Submission_Counts();
	BOOST_CHECK_EQUAL(indexed_counts.draw_calls, 2u);
	BOOST_CHECK_EQUAL(indexed_counts.triangles, 3u);
	BOOST_CHECK_EQUAL(indexed_counts.vertex_invocations, 9u);
	BOOST_CHECK(!commands.Draw(0));
	BOOST_CHECK(!commands.Draw_Indexed(0));
	BOOST_CHECK(!commands.Draw(3, 0, 0));
	BOOST_CHECK(!commands.Draw_Indexed(3, 0, 0, 0));
	BOOST_CHECK(commands.Submission_Counts().draw_calls == indexed_counts.draw_calls);
	BOOST_CHECK(commands.Submission_Counts().triangles == indexed_counts.triangles);
	BOOST_CHECK(commands.Submission_Counts().vertex_invocations == indexed_counts.vertex_invocations);
	BOOST_REQUIRE(commands.Reset_State());
	BOOST_CHECK(commands.Submission_Counts().draw_calls == indexed_counts.draw_calls);
	BOOST_CHECK(commands.Submission_Counts().triangles == indexed_counts.triangles);
	BOOST_CHECK(commands.Submission_Counts().vertex_invocations == indexed_counts.vertex_invocations);
	BOOST_CHECK(!commands.Draw(3));
	BOOST_CHECK(commands.Submission_Counts().draw_calls == indexed_counts.draw_calls);

	BOOST_REQUIRE(commands.Set_Render_Targets(color_target, depth_target));
	BOOST_REQUIRE(commands.Set_Viewport({0, 0, 16, 16, 0.0f, 1.0f}));
	BOOST_REQUIRE(commands.Clear({0.0f, 0.0f, 1.0f, 1.0f}, 1.0f));
	BOOST_REQUIRE(commands.Set_Vertex_Buffer(0, vertex_buffer, static_cast<std::uint32_t>(sizeof(SubmissionTestVertex)), 0));
	BOOST_REQUIRE(commands.Bind_Pipeline(strip_pipeline));
	BOOST_REQUIRE(commands.Set_Bindless_Resources(material_resources.Resources()));
	BOOST_REQUIRE(commands.Draw(3, 0, 2));
	const RHISubmissionCounts strip_counts = commands.Submission_Counts();
	BOOST_CHECK_EQUAL(strip_counts.draw_calls, 3u);
	BOOST_CHECK_EQUAL(strip_counts.triangles, 5u);
	BOOST_CHECK_EQUAL(strip_counts.vertex_invocations, 15u);
	std::array<std::byte, 16 * 16 * 4> pixels{};
	BOOST_REQUIRE(device.Readback_Texture(color_target, pixels, 16 * 4));
	const std::size_t center = (8u * 16u + 8u) * 4u;
	BOOST_CHECK_GT(std::to_integer<unsigned>(pixels[center]), 200u);
	BOOST_CHECK_LT(std::to_integer<unsigned>(pixels[center + 1]), 32u);
	BOOST_CHECK_LT(std::to_integer<unsigned>(pixels[center + 2]), 32u);
	BOOST_CHECK_EQUAL(std::to_integer<unsigned>(pixels[center + 3]), 255u);

	BOOST_REQUIRE(commands.Bind_Pipeline(point_pipeline));
	BOOST_REQUIRE(commands.Draw(3, 0, 2));
	const RHISubmissionCounts point_counts = commands.Submission_Counts();
	BOOST_CHECK_EQUAL(point_counts.draw_calls, 4u);
	BOOST_CHECK_EQUAL(point_counts.triangles, 5u);
	BOOST_CHECK_EQUAL(point_counts.vertex_invocations, 21u);

	BOOST_CHECK(device.Destroy_Pipeline(point_pipeline));
	BOOST_CHECK(device.Destroy_Buffer(material_buffer));
	BOOST_CHECK(device.Destroy_Pipeline(strip_pipeline));
	BOOST_CHECK(device.Destroy_Pipeline(list_pipeline));
	BOOST_CHECK(device.Destroy_Texture(depth_target));
	BOOST_CHECK(device.Destroy_Texture(color_target));
	BOOST_CHECK(device.Destroy_Buffer(index_buffer));
	BOOST_CHECK(device.Destroy_Buffer(vertex_buffer));
}

BOOST_AUTO_TEST_CASE(dx11_frame_lifecycle_and_resize)
{
	constexpr char class_name[] = "GraphicsDX11FrameTestWindow";
	HWND window = Create_Frame_Test_Window();
	BOOST_REQUIRE(window != nullptr);

	DX11DeviceOptions options;
	options.use_warp = true;
	options.window = window;
	options.width = 16;
	options.height = 16;
	DX11Device device(options);
	BOOST_REQUIRE(device.Is_Valid());

	SwapChain &swap_chain = device.Get_Swap_Chain();
	BOOST_REQUIRE(swap_chain.Is_Valid());
	BOOST_CHECK_EQUAL(swap_chain.Backbuffer().width, 16);
	BOOST_CHECK_EQUAL(swap_chain.Backbuffer().height, 16);
	BOOST_CHECK(swap_chain.Backbuffer().texture.Is_Valid());
	BOOST_CHECK(swap_chain.Depth_Target().texture.Is_Valid());

	RenderGraph graph;
	const GraphResourceHandle color = graph.Create_Resource({GraphResourceKind::Texture});
	const GraphResourceHandle depth = graph.Create_Resource({GraphResourceKind::Texture});
	std::array<GraphResourceBinding, 2> bindings{};
	Frame frame;

	BOOST_REQUIRE(frame.Begin(graph, device, color, depth, bindings));
	BOOST_CHECK(!swap_chain.Present());
	BOOST_CHECK(!swap_chain.Resize(32, 24));
	BOOST_CHECK(bindings[0].texture == swap_chain.Backbuffer().texture);
	BOOST_CHECK(bindings[1].texture == swap_chain.Depth_Target().texture);
	BOOST_REQUIRE(frame.End(device));
	BOOST_REQUIRE(frame.Present(device));

	BOOST_REQUIRE(swap_chain.Resize(32, 24));
	BOOST_CHECK_EQUAL(swap_chain.Backbuffer().width, 32);
	BOOST_CHECK_EQUAL(swap_chain.Backbuffer().height, 24);
	BOOST_CHECK(swap_chain.Backbuffer().texture.Is_Valid());
	BOOST_CHECK(swap_chain.Depth_Target().texture.Is_Valid());
	BOOST_REQUIRE(frame.Begin(graph, device, color, depth, bindings));
	BOOST_CHECK(bindings[0].texture == swap_chain.Backbuffer().texture);
	BOOST_CHECK(bindings[1].texture == swap_chain.Depth_Target().texture);
	BOOST_REQUIRE(frame.End(device));
	BOOST_REQUIRE(frame.Present(device));

	DestroyWindow(window);
	UnregisterClassA(class_name, GetModuleHandleA(nullptr));
}

BOOST_AUTO_TEST_CASE(dx11_frame_executes_opaque_pass_to_swapchain)
{
	constexpr char class_name[] = "GraphicsDX11FrameTestWindow";
	HWND window = Create_Frame_Test_Window();
	BOOST_REQUIRE(window != nullptr);

	{
		DX11DeviceOptions options;
		options.use_warp = true;
		options.window = window;
		options.width = 16;
		options.height = 16;
		options.shader_directory = GRAPHICS_DX11_TEST_SHADER_DIRECTORY;
		DX11Device device(options);
		BOOST_REQUIRE(device.Is_Valid());

		const RHIBufferHandle instances = device.Create_Buffer({96, RHIBufferUsage::Storage, 96});
		const RHIPipelineHandle pipeline = device.Create_Pipeline({9});
		BOOST_REQUIRE(instances != nullptr);
		BOOST_REQUIRE(pipeline != nullptr);

		std::array<std::byte, 108> vertex_data{};
		std::array<std::byte, 6> index_data{};
		std::array<std::byte, 4> material_texture_data{};
		Mesh mesh{3, 3, 36, MeshIndexFormat::UInt16};
		mesh.vertex_data = vertex_data;
		mesh.index_data = index_data;
		Texture material_texture_resource{1, 1, 1, 1, TextureFormat::RGBA8_UNorm, TextureUsage::Sampled};
		material_texture_resource.pixel_data = material_texture_data;
		Material material;
		const MeshHandle mesh_handle(0, 1);
		const TextureHandle texture_handle(0, 1);
		const MaterialHandle material_handle(0, 1);
		material.textures[0] = texture_handle;
		GPUResourceResidency residency(device);
		BOOST_REQUIRE(residency.Upload_Mesh(mesh_handle, mesh));
		BOOST_REQUIRE(residency.Upload_Texture(texture_handle, material_texture_resource));
		BOOST_REQUIRE(residency.Upload_Material(material_handle, material));
		const GPUResidentMesh uploaded_mesh = residency.Mesh_Info(mesh_handle);
		const GPUResidentMaterial uploaded_material = residency.Material_Info(material_handle);
		BOOST_REQUIRE(uploaded_mesh.vertex_buffer.Is_Valid());
		BOOST_REQUIRE(uploaded_mesh.index_buffer.Is_Valid());
		BOOST_REQUIRE(uploaded_material.constants.Is_Valid());
		BOOST_REQUIRE(uploaded_material.textures[0].Is_Valid());
		BindlessResourceTable bindless_resources;
		const ResourceIndex instance_resource = bindless_resources.Register_Buffer(instances);
		const ResourceIndex texture_resource = bindless_resources.Register_Texture(texture_handle, uploaded_material.textures[0]);
		const ResourceIndex material_resource = bindless_resources.Register_Material(material_handle, uploaded_material.constants);
		BOOST_REQUIRE(instance_resource.Is_Valid());
		BOOST_REQUIRE(texture_resource.Is_Valid());
		BOOST_REQUIRE(material_resource.Is_Valid());
		mesh.Mark_Dirty();
		material_texture_resource.Mark_Dirty();
		material.Mark_Dirty();
		BOOST_REQUIRE(residency.Upload_Mesh(mesh_handle, mesh));
		BOOST_REQUIRE(residency.Upload_Texture(texture_handle, material_texture_resource));
		BOOST_REQUIRE(residency.Upload_Material(material_handle, material));

		RenderGraph graph;
		const GraphResourceHandle color_resource = graph.Create_Resource({GraphResourceKind::Texture});
		const GraphResourceHandle depth_resource = graph.Create_Resource({GraphResourceKind::Texture});
		const GraphPassHandle pass = OpaquePass::Add_To_Graph(graph, color_resource, depth_resource);
		BOOST_REQUIRE(pass != nullptr);

		std::array<GraphResourceBinding, 2> resources{};
		Frame frame;
		BOOST_REQUIRE(frame.Begin(graph, device, color_resource, depth_resource, resources));

		ExecutionPlan plan;
		BOOST_REQUIRE(plan.Compile(graph, resources));
		const std::array<DrawData, 1> draws = {DrawData{0, 2, 0, 1, pipeline, 0}};
		const std::array<OpaqueMeshBinding, 1> meshes = {
			OpaqueMeshBinding{uploaded_mesh.vertex_buffer, uploaded_mesh.index_buffer, RHIIndexFormat::UInt16, uploaded_mesh.vertex_stride, uploaded_mesh.index_count, 0, 0}
		};
		const OpaquePassInput input{
			draws,
			meshes,
			bindless_resources.Resources(),
			color_resource,
			depth_resource,
			{0, 0, 16, 16, 0.0f, 1.0f},
			{0.1f, 0.2f, 0.3f, 1.0f},
			1.0f
		};

		BOOST_REQUIRE(plan.Execute(graph, device.Immediate_Command_List(), [&](GraphPassHandle current_pass, CommandList &commands, const PassResources &pass_resources) noexcept {
			return current_pass == pass && OpaquePass::Execute(commands, pass_resources, input);
		}));
		BOOST_REQUIRE(frame.End(device));
		BOOST_REQUIRE(frame.Present(device));

		BOOST_CHECK(device.Destroy_Pipeline(pipeline));
		BOOST_CHECK(device.Destroy_Buffer(instances));
	}

	DestroyWindow(window);
	UnregisterClassA(class_name, GetModuleHandleA(nullptr));
}

BOOST_AUTO_TEST_CASE(texture_subresources_preserve_pitch_cube_faces_volume_slices_and_lifetime)
{
    DX11Device device(Make_DX11_Test_Options());
    BOOST_REQUIRE(device.Is_Valid());
    for (const auto dimension : {RHITextureDimension::Texture2D, RHITextureDimension::Cube, RHITextureDimension::Volume}) {
        RHITexture description{8, 8, 4, RHITextureFormat::RGBA8_UNorm};
        description.dimension = dimension;
        description.array_size = dimension == RHITextureDimension::Cube ? 6 : 1;
        description.depth = dimension == RHITextureDimension::Volume ? 4 : 1;
        const auto texture = device.Create_Texture(description);
        const auto copy = device.Create_Texture(description);
        BOOST_REQUIRE(texture.Is_Valid()); BOOST_REQUIRE(copy.Is_Valid());
        for (unsigned layer = 0; layer < description.array_size; ++layer)
            for (unsigned mip = 0; mip < 4; ++mip) {
                const unsigned width = (std::max)(1u, 8u >> mip), depth = (std::max)(1u, description.depth >> mip);
                const unsigned pitch = width * 4 + 8, slice_pitch = pitch * width + 16;
                std::vector<std::byte> source(slice_pitch * depth, std::byte{0xcd});
                for (unsigned z = 0; z < depth; ++z) for (unsigned y = 0; y < width; ++y)
                    for (unsigned x = 0; x < width * 4; ++x)
                        source[z * slice_pitch + y * pitch + x] = std::byte(layer * 30 + mip * 7 + z + x);
                BOOST_REQUIRE(device.Update_Texture(texture, {source, pitch, slice_pitch, mip, layer}));
            }
        BOOST_REQUIRE(device.Immediate_Command_List().Copy_Texture(texture, copy));
        BOOST_REQUIRE(device.Destroy_Texture(texture));
        BOOST_CHECK(!device.Retain_Texture(texture));
        for (unsigned layer = 0; layer < description.array_size; ++layer)
            for (unsigned mip = 0; mip < 4; ++mip) {
                const unsigned width = (std::max)(1u, 8u >> mip), depth = (std::max)(1u, description.depth >> mip);
                const unsigned pitch = width * 4 + 8, slice_pitch = pitch * width + 16;
                std::vector<std::byte> pixels(slice_pitch * depth, std::byte{0xcd});
                BOOST_REQUIRE(device.Readback_Texture_Subresource(copy, {pixels, pitch, slice_pitch, mip, layer}));
                for (unsigned z = 0; z < depth; ++z) for (unsigned y = 0; y < width; ++y) {
                    for (unsigned x = 0; x < width * 4; ++x)
                        BOOST_CHECK(pixels[z * slice_pitch + y * pitch + x] == std::byte(layer * 30 + mip * 7 + z + x));
                    BOOST_CHECK(pixels[z * slice_pitch + y * pitch + width * 4] == std::byte{0xcd});
                }
            }
        std::array<std::byte, 4> short_buffer{};
        BOOST_CHECK(!device.Update_Texture(copy, {short_buffer, 4, 4, 4, 0}));
        BOOST_CHECK(!device.Readback_Texture_Subresource(copy, {short_buffer, 4, 4, 0, description.array_size}));
        BOOST_CHECK(!device.Readback_Texture_Subresource(copy, {short_buffer, 4}));
        BOOST_REQUIRE(device.Destroy_Texture(copy));
    }
}

BOOST_AUTO_TEST_CASE(compressed_mip_tails_survive_gpu_transfer_without_touching_padding)
{
    DX11Device device(Make_DX11_Test_Options());
    BOOST_REQUIRE(device.Is_Valid());
    for (auto format : {RHITextureFormat::BC1_UNorm, RHITextureFormat::BC2_UNorm, RHITextureFormat::BC3_UNorm}) {
        const auto texture = device.Create_Texture({8, 4, 4, format});
        BOOST_REQUIRE(texture.Is_Valid());
        for (unsigned mip = 0; mip < 4; ++mip) {
            const unsigned row_bytes = (mip == 0 ? 2 : 1) * (format == RHITextureFormat::BC1_UNorm ? 8 : 16);
            std::vector<std::byte> source(row_bytes);
            for (unsigned i = 0; i < row_bytes; ++i) source[i] = std::byte(i + mip * 21);
            BOOST_REQUIRE(device.Update_Texture(texture, {source, row_bytes, 0, mip}));
            std::vector<std::byte> result(row_bytes + 8, std::byte{0xab});
            BOOST_REQUIRE(device.Readback_Texture_Subresource(texture, {result, row_bytes + 8, 0, mip}));
            BOOST_CHECK(std::equal(source.begin(), source.end(), result.begin()));
            BOOST_CHECK(result[row_bytes] == std::byte{0xab});
        }
        BOOST_CHECK(!device.Generate_Texture_Mips(texture));
        BOOST_REQUIRE(device.Destroy_Texture(texture));
    }
}

BOOST_AUTO_TEST_CASE(generated_mips_and_array_output_views_retain_the_selected_pixels)
{
    DX11Device device(Make_DX11_Test_Options());
    BOOST_REQUIRE(device.Is_Valid());
    RHITexture description{8, 8, 4, RHITextureFormat::RGBA8_UNorm};
    description.generate_mips = true;
    std::vector<std::byte> solid(8 * 8 * 4);
    for (unsigned p = 0; p < solid.size(); p += 4) {
        solid[p] = std::byte{200}; solid[p + 1] = std::byte{80}; solid[p + 2] = std::byte{20}; solid[p + 3] = std::byte{128};
    }
    const auto texture = device.Create_Texture_Initialized(description, {solid, 32});
    BOOST_REQUIRE(texture.Is_Valid());
    BOOST_REQUIRE(device.Generate_Texture_Mips(texture));
    std::array<std::byte, 4> pixel{};
    BOOST_REQUIRE(device.Readback_Texture_Subresource(texture, {pixel, 4, 0, 3}));
    BOOST_CHECK(std::equal(pixel.begin(), pixel.end(), solid.begin()));
    BOOST_REQUIRE(device.Destroy_Texture(texture));
    description = {8, 8, 1, RHITextureFormat::RGBA8_UNorm, static_cast<unsigned>(RHITextureUsage::RenderTarget)};
    description.dimension = RHITextureDimension::Cube; description.array_size = 6; description.output_layer = 5;
    const auto target = device.Create_Texture(description);
    BOOST_REQUIRE(target.Is_Valid());
    auto& commands = device.Immediate_Command_List();
    const auto depth = device.Create_Texture({8, 8, 1, RHITextureFormat::D16_UNorm, static_cast<unsigned>(RHITextureUsage::DepthStencil) | static_cast<unsigned>(RHITextureUsage::ShaderResource)});
    BOOST_REQUIRE(depth.Is_Valid()); BOOST_REQUIRE(commands.Set_Render_Targets(target, depth));
    BOOST_REQUIRE(commands.Clear({0, 1, 0, 0.5f}, 1));
    std::vector<std::byte> pixels(8 * 8 * 4);
    BOOST_REQUIRE(device.Readback_Texture_Subresource(target, {pixels, 32, 0, 0, 5}));
    BOOST_CHECK(pixels[0] == std::byte{0}); BOOST_CHECK(pixels[1] == std::byte{255}); BOOST_CHECK(pixels[3] == std::byte{128});
    BOOST_REQUIRE(device.Destroy_Texture(target)); BOOST_REQUIRE(device.Destroy_Texture(depth));
}

BOOST_AUTO_TEST_CASE(mapped_textures_retain_partial_edits_and_cancel_uncommitted_writes)
{
    DX11Device device(Make_DX11_Test_Options());
    for (auto dimension : {RHITextureDimension::Texture2D, RHITextureDimension::Cube, RHITextureDimension::Volume}) {
        RHITexture description{8, 8, 4, RHITextureFormat::RGBA8_UNorm};
        description.dimension = dimension;
        description.array_size = dimension == RHITextureDimension::Cube ? 6 : 1;
        description.depth = dimension == RHITextureDimension::Volume ? 4 : 1;
        const auto texture = device.Create_Texture(description);
        BOOST_REQUIRE(texture.Is_Valid());
        BOOST_REQUIRE(device.Retain_Texture(texture));
        BOOST_REQUIRE(device.Destroy_Texture(texture));
        for (unsigned layer = 0; layer < description.array_size; ++layer) {
            std::array<RHITextureMapping, 4> mappings;
            for (unsigned mip = 0; mip < 4; ++mip) {
                const unsigned width = (std::max)(1u, 8u >> mip), depth = (std::max)(1u, description.depth >> mip);
                std::vector<std::byte> source(width * width * depth * 4, std::byte{17});
                BOOST_REQUIRE(device.Update_Texture(texture, {source, width * 4, width * width * 4, mip, layer}));
                BOOST_REQUIRE(device.Map_Texture(texture, mip, layer, false, mappings[mip]));
                RHITextureMapping duplicate;
                BOOST_CHECK(!device.Map_Texture(texture, mip, layer, false, duplicate));
                mappings[mip].bytes[0] = std::byte(90 + mip + layer);
                if (depth > 1) mappings[mip].bytes[mappings[mip].slice_pitch] = std::byte{51};
            }
            // Commit in reverse order while other levels remain mapped.
            for (unsigned mip = 4; mip-- > 0;) {
                BOOST_REQUIRE(device.Unmap_Texture(texture, mip, layer));
                const unsigned width = (std::max)(1u, 8u >> mip), depth = (std::max)(1u, description.depth >> mip);
                std::vector<std::byte> result(width * width * depth * 4);
                BOOST_REQUIRE(device.Readback_Texture_Subresource(texture, {result, width * 4, width * width * 4, mip, layer}));
                BOOST_CHECK(result[0] == std::byte(90 + mip + layer)); BOOST_CHECK(result[1] == std::byte{17});
                if (depth > 1) BOOST_CHECK(result[width * width * 4] == std::byte{51});
                RHITextureMapping read_only;
                BOOST_REQUIRE(device.Map_Texture(texture, mip, layer, true, read_only));
                read_only.bytes[0] = std::byte{0};
                BOOST_REQUIRE(device.Unmap_Texture(texture, mip, layer));
                BOOST_REQUIRE(device.Readback_Texture_Subresource(texture, {result, width * 4, width * width * 4, mip, layer}));
                BOOST_CHECK(result[0] == std::byte(90 + mip + layer));
            }
        }
        RHITextureMapping abandoned;
        BOOST_REQUIRE(device.Map_Texture(texture, 0, 0, false, abandoned));
        BOOST_REQUIRE(device.Destroy_Texture(texture));
        BOOST_CHECK(!device.Unmap_Texture(texture, 0, 0));
        BOOST_CHECK(!device.Retain_Texture(texture));
    }
    for (auto format : {RHITextureFormat::BC1_UNorm, RHITextureFormat::BC2_UNorm, RHITextureFormat::BC3_UNorm}) {
        const auto texture = device.Create_Texture({8, 4, 4, format});
        BOOST_REQUIRE(texture.Is_Valid());
        for (unsigned mip = 0; mip < 4; ++mip) {
            RHITextureMapping mapped;
            BOOST_REQUIRE(device.Map_Texture(texture, mip, 0, false, mapped));
            const unsigned bytes = (mip == 0 ? 2 : 1) * (format == RHITextureFormat::BC1_UNorm ? 8 : 16);
            for (unsigned i = 0; i < bytes; ++i) mapped.bytes[i] = std::byte(i + mip * 13);
            BOOST_REQUIRE(device.Unmap_Texture(texture, mip, 0));
            std::vector<std::byte> result(bytes);
            BOOST_REQUIRE(device.Readback_Texture_Subresource(texture, {result, bytes, 0, mip}));
            for (unsigned i = 0; i < bytes; ++i) BOOST_CHECK(result[i] == std::byte(i + mip * 13));
        }
        BOOST_REQUIRE(device.Destroy_Texture(texture));
    }
}

BOOST_AUTO_TEST_CASE(explicit_attachment_clears_preserve_bound_targets_and_depth_stencil_values)
{
    DX11Device device(Make_DX11_Test_Options());
    const auto a = device.Create_Texture({4, 4, 1, RHITextureFormat::RGBA8_UNorm, static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto b = device.Create_Texture({4, 4, 1, RHITextureFormat::RGBA8_UNorm, static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth = device.Create_Texture({4, 4, 1, RHITextureFormat::D24_UNorm_S8, static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    auto& commands = device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(a, depth));
    BOOST_REQUIRE(commands.Clear_Color_Target(b, {0, 1, 0, 0.5f}));
    BOOST_REQUIRE(commands.Clear_Depth_Stencil_Target(depth, 0.25f, 77));
    BOOST_REQUIRE(commands.Clear({1, 0, 0, 1}, 0.5f));
    std::array<std::byte, 4 * 4 * 4> pixels{};
    BOOST_REQUIRE(device.Readback_Texture(b, pixels, 16));
    BOOST_CHECK(pixels[0] == std::byte{0}); BOOST_CHECK(pixels[1] == std::byte{255}); BOOST_CHECK(pixels[3] == std::byte{128});
    BOOST_REQUIRE(device.Readback_Texture(a, pixels, 16));
    BOOST_CHECK(pixels[0] == std::byte{255}); BOOST_CHECK(pixels[1] == std::byte{0});
    BOOST_REQUIRE(device.Readback_Texture(depth, pixels, 16));
    const unsigned packed = std::to_integer<unsigned>(pixels[0]) | std::to_integer<unsigned>(pixels[1]) << 8
        | std::to_integer<unsigned>(pixels[2]) << 16;
    BOOST_CHECK_SMALL(static_cast<int>(packed) - 0x800000, 2);
    BOOST_CHECK_EQUAL(std::to_integer<unsigned>(pixels[3]), 77u);
    BOOST_CHECK(!commands.Clear_Color_Target(depth, {1, 1, 1, 1}));
    BOOST_CHECK(!commands.Clear_Depth_Stencil_Target(a, 1, 0));
    device.Destroy_Texture(a); device.Destroy_Texture(b); device.Destroy_Texture(depth);
}
