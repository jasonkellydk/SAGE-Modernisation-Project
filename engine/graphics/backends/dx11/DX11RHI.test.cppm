module;

#define BOOST_TEST_MODULE GraphicsDX11RHITests

#include <boost/test/included/unit_test.hpp>

#include <array>
#include <cstdint>
#include <windows.h>

export module Graphics.Backends.DX11.Tests;

#ifndef GRAPHICS_DX11_TEST_SHADER_DIRECTORY
#define GRAPHICS_DX11_TEST_SHADER_DIRECTORY "."
#endif

import Graphics.Backends.DX11;
import Graphics.Backends.DX11.Coexistence;
import Graphics.RenderGraph.Frame;
import Graphics.Passes.Opaque;
import Graphics.Resources.Bindless.BindlessResourceTable;
import Graphics.Resources.Residency.GPUResourceResidency;

using namespace Graphics;

BOOST_AUTO_TEST_CASE(dx11_coexistence_requires_a_shared_device)
{
	Graphics_DX11_Shutdown_Shared_Frame();
	BOOST_CHECK(!Register_Graphics_Phase_Executor(nullptr, nullptr));
	BOOST_CHECK(!Graphics_DX11_Begin_Frame());
	BOOST_CHECK(!Graphics_DX11_Begin_Graphics_Phase());
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

	BOOST_CHECK(device.Destroy_Pipeline(pipeline));
	BOOST_CHECK(device.Destroy_Texture(depth_target));
	BOOST_CHECK(device.Destroy_Texture(color_target));
	BOOST_CHECK(device.Destroy_Texture(texture));
	BOOST_CHECK(device.Destroy_Buffer(instances));
	BOOST_CHECK(device.Destroy_Buffer(constants));
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
