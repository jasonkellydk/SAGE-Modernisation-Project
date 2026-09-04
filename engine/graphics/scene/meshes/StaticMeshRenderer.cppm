module;

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <vector>

export module Graphics.Scene.StaticMeshes;

export import Graphics.Passes.Opaque;
export import Graphics.Resources.Bindless.BindlessResourceTable;
export import Graphics.Resources.Materials.Material;
export import Graphics.Resources.Residency.GPUResourceResidency;
export import Graphics.Scene.DrawGeneration;
export import Graphics.Scene.GPUScene;
export import Graphics.Scene.Attachments;
export import Graphics.Scene.Models.ModelInstance;
export import Graphics.Scene.Models.ModelVisibility;
export import Graphics.Scene.Models.Skinning;
export import Graphics.Scene.Views.View;
export import Graphics.Scene.Visibility;
export import Graphics.Shaders.Library;
export import Graphics.RHI.Frame;

namespace Graphics
{

export struct StaticMeshVertex final
{
	float position[3]{};
	float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
	float uv[2]{};
};

static_assert(sizeof(StaticMeshVertex) == 36);

export struct SkinnedMeshVertex final
{
	float position[3]{};
	float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
	float uv[2]{};
	MeshSkinningData skinning{};
};

static_assert(sizeof(SkinnedMeshVertex) == 60);
static_assert(offsetof(SkinnedMeshVertex, skinning) == 36);

export struct StaticMeshSource final
{
	std::uint32_t vertex_count = 0;
	std::uint32_t index_count = 0;
	std::uint32_t vertex_stride = sizeof(StaticMeshVertex);
	MeshIndexFormat index_format = MeshIndexFormat::None;
	std::span<const std::byte> vertex_data{};
	std::span<const std::byte> index_data{};
	std::array<float, 3> bounds_center{};
	float bounds_radius = 0.0f;
	std::span<const MeshPart> parts{};
	MeshVertexFormat vertex_format = MeshVertexFormat::Position3Color4UV2;
	std::uint32_t skin_bone_count = 0;
};

export struct StaticMeshLODSource final
{
	StaticMeshSource source{};
	float max_screen_size = 0.0f;
};

export bool Validate_Static_Mesh_Source(const StaticMeshSource &source) noexcept
{
	const std::uint32_t expected_stride = source.vertex_format == MeshVertexFormat::Position3Color4UV2
		? static_cast<std::uint32_t>(sizeof(StaticMeshVertex))
		: static_cast<std::uint32_t>(sizeof(SkinnedMeshVertex));
	if (source.vertex_count == 0 || source.index_count == 0 || source.vertex_stride != expected_stride || source.index_format == MeshIndexFormat::None)
		return false;
	if (source.vertex_format == MeshVertexFormat::Position3Color4UV2Skinned
		&& (source.skin_bone_count == 0 || source.skin_bone_count > std::numeric_limits<std::uint16_t>::max() + 1u))
		return false;
	if (source.vertex_format == MeshVertexFormat::Position3Color4UV2 && source.skin_bone_count != 0)
		return false;

	const std::size_t index_stride = source.index_format == MeshIndexFormat::UInt16 ? sizeof(std::uint16_t) : sizeof(std::uint32_t);
	if (source.vertex_count > std::numeric_limits<std::size_t>::max() / source.vertex_stride
		|| source.index_count > std::numeric_limits<std::size_t>::max() / index_stride)
		return false;

	return source.vertex_data.size() == static_cast<std::size_t>(source.vertex_count) * source.vertex_stride
		&& source.index_data.size() == static_cast<std::size_t>(source.index_count) * index_stride
		&& source.vertex_data.size() <= std::numeric_limits<std::uint32_t>::max()
		&& source.index_data.size() <= std::numeric_limits<std::uint32_t>::max()
		&& source.bounds_radius >= 0.0f
		&& source.parts.size() <= Max_Model_Part_Count;
}

export struct alignas(16) GPUViewData final
{
	std::array<float, 16> view_projection{};
};

static_assert(sizeof(GPUViewData) == 64);

export class StaticMeshRenderer final
{
public:
	bool Initialize(Device &device, const std::filesystem::path &shader_directory, std::size_t max_meshes = 4096, std::size_t max_instances = 16384, std::size_t max_bone_matrices = 262144)
	{
		if (m_device != nullptr || !device.Is_Valid() || max_meshes == 0 || max_instances == 0 || max_bone_matrices == 0
			|| max_instances > std::numeric_limits<std::uint32_t>::max() / sizeof(GPUInstanceData)
			|| max_bone_matrices > std::numeric_limits<std::uint32_t>::max() / sizeof(GPUBoneMatrixData))
			return false;

		m_device = &device;
		m_mesh_sources.reserve(max_meshes);
		m_scene.Reserve(max_instances);
		m_attachments.Reserve(max_instances);
		m_skeletons.Reserve(max_meshes);
		m_animations.Reserve(max_meshes);
		m_bone_matrices.Reserve(max_instances, max_bone_matrices);
		m_meshes.Reserve(max_meshes);
		m_materials.Reserve(1);
		m_gpu_scene.Reserve(max_instances, max_meshes, 1);
		m_visible_storage.resize(max_instances);
		m_lod_storage.resize(max_instances);
		m_lod_history_storage.resize(max_instances);
		m_draw_storage.resize(max_instances);
		m_visible = std::make_unique<VisibleSet>(m_visible_storage);
		m_lod = std::make_unique<LODSet>(m_lod_storage);
		m_lod_history = std::make_unique<LODHistory>(m_lod_history_storage);
		m_draws = std::make_unique<DrawSet>(m_draw_storage);
		m_mesh_bindings.resize(max_meshes);
		m_mesh_part_bindings.reserve(max_meshes * Max_Model_Part_Count);
		m_dirty_instances.reserve(max_instances);
		m_instance_capacity = max_instances;

		m_residency = std::make_unique<GPUResourceResidency>(device);
		m_shader = m_shaders.Load_Basic_Opaque(shader_directory);
		if (!m_shader.Is_Valid()) {
			Shutdown();
			return false;
		}

		const PipelineDesc pipeline_description = m_shaders.Make_Pipeline_Description(m_shader, Make_Basic_Opaque_Pipeline());
		m_pipeline = m_shaders.Create_Pipeline(device, m_shader, pipeline_description);
		if (!m_pipeline.Is_Valid()) {
			Shutdown();
			return false;
		}

		m_skinned_shader = m_shaders.Load_Skinned_Basic_Opaque(shader_directory);
		if (m_skinned_shader.Is_Valid()) {
			const PipelineDesc skinned_pipeline_description = m_shaders.Make_Pipeline_Description(
				m_skinned_shader, Make_Skinned_Basic_Opaque_Pipeline());
			m_skinned_pipeline = m_shaders.Create_Pipeline(device, m_skinned_shader, skinned_pipeline_description);
			if (!m_skinned_pipeline.Is_Valid()) {
				m_shaders.Destroy(m_skinned_shader);
				m_skinned_shader = {};
			}
		}

		m_instance_buffer = device.Create_Buffer({
			static_cast<std::uint32_t>(max_instances * sizeof(GPUInstanceData)),
			RHIBufferUsage::Storage,
			static_cast<std::uint32_t>(sizeof(GPUInstanceData))
		});
		if (!m_instance_buffer.Is_Valid()) {
			Shutdown();
			return false;
		}

		m_bone_buffer = device.Create_Buffer({
			static_cast<std::uint32_t>(max_bone_matrices * sizeof(GPUBoneMatrixData)),
			RHIBufferUsage::Storage,
			static_cast<std::uint32_t>(sizeof(GPUBoneMatrixData))
		});
		if (!m_bone_buffer.Is_Valid()) {
			Shutdown();
			return false;
		}

		m_bindless.Reserve(5, 4, 0, 0, 1);
		if (!m_bindless.Register_Buffer(m_instance_buffer).Is_Valid()) {
			Shutdown();
			return false;
		}

		const GPULightData empty_light{};
		m_light_buffer = device.Create_Buffer_Initialized(
			{static_cast<std::uint32_t>(sizeof(GPULightData)), RHIBufferUsage::Storage, static_cast<std::uint32_t>(sizeof(GPULightData))},
			std::as_bytes(std::span<const GPULightData>(&empty_light, 1)));
		if (!m_light_buffer.Is_Valid() || !m_bindless.Register_Buffer(m_light_buffer).Is_Valid()) {
			Shutdown();
			return false;
		}

		m_view_buffer = device.Create_Buffer_Initialized(
			{static_cast<std::uint32_t>(sizeof(GPUViewData)), RHIBufferUsage::Storage, static_cast<std::uint32_t>(sizeof(GPUViewData))},
			std::as_bytes(std::span<const GPUViewData>(&m_gpu_view, 1)));
		if (!m_view_buffer.Is_Valid() || !m_bindless.Register_Buffer(m_view_buffer).Is_Valid()) {
			Shutdown();
			return false;
		}
		if (!m_bindless.Register_Buffer(m_bone_buffer).Is_Valid()) {
			Shutdown();
			return false;
		}

		Material material;
		material.shader = m_shader;
		material.parameters.values[0] = 1.0f;
		material.parameters.values[1] = 0.0f;
		material.parameters.values[2] = 1.0f;
		material.parameters.values[3] = 1.0f;
		m_default_material = m_materials.Create(material);
		if (!m_default_material.Is_Valid() || !m_residency->Upload_Material(m_default_material, material)) {
			Shutdown();
			return false;
		}
		const GPUResidentMaterial resident_material = m_residency->Material_Info(m_default_material);
		if (!resident_material.constants.Is_Valid() || !m_bindless.Register_Material(m_default_material, resident_material.constants).Is_Valid()) {
			Shutdown();
			return false;
		}

		m_graph.Reserve(2, 1, 2);
		m_color_resource = m_graph.Create_Resource({GraphResourceKind::Texture});
		m_depth_resource = m_graph.Create_Resource({GraphResourceKind::Texture});
		m_opaque_pass = OpaquePass::Add_To_Graph(m_graph, m_color_resource, m_depth_resource, 10);
		if (!m_color_resource.Is_Valid() || !m_depth_resource.Is_Valid() || !m_opaque_pass.Is_Valid()) {
			Shutdown();
			return false;
		}

		m_view = View(Matrix4x4::Identity(), Matrix4x4::Identity(), {}, {});
		Set_View(m_view);
		m_scene_dirty = true;
		return true;
	}

	void Shutdown() noexcept
	{
		if (m_device != nullptr) {
			for (const std::unique_ptr<MeshStorage> &storage : m_mesh_sources) {
				if (storage != nullptr && storage->handle.Is_Valid())
					Destroy_Mesh(storage->handle);
			}
			if (m_default_material.Is_Valid()) {
				m_bindless.Destroy_Material(m_default_material);
				m_residency->Destroy_Material(m_default_material);
				m_materials.Destroy(m_default_material);
			}
			if (m_instance_buffer.Is_Valid())
				m_device->Destroy_Buffer(m_instance_buffer);
			if (m_light_buffer.Is_Valid())
				m_device->Destroy_Buffer(m_light_buffer);
			if (m_view_buffer.Is_Valid())
				m_device->Destroy_Buffer(m_view_buffer);
			if (m_bone_buffer.Is_Valid())
				m_device->Destroy_Buffer(m_bone_buffer);
			if (m_pipeline.Is_Valid())
				m_device->Destroy_Pipeline(m_pipeline);
			if (m_skinned_pipeline.Is_Valid())
				m_device->Destroy_Pipeline(m_skinned_pipeline);
		}

		m_bindless.Clear();
		m_residency.reset();
		m_shaders.Destroy(m_shader);
		m_shaders.Destroy(m_skinned_shader);
		m_shader = {};
		m_skinned_shader = {};
		m_pipeline = {};
		m_skinned_pipeline = {};
		m_default_material = {};
		m_instance_buffer = {};
		m_light_buffer = {};
		m_view_buffer = {};
		m_bone_buffer = {};
		m_mesh_sources.clear();
		m_attachments.Clear();
		m_skeletons = {};
		m_animations = {};
		m_bone_matrices.Clear();
		m_visible_storage.clear();
		m_lod_storage.clear();
		m_lod_history_storage.clear();
		m_draw_storage.clear();
		m_mesh_bindings.clear();
		m_mesh_part_bindings.clear();
		m_dirty_instances.clear();
		m_graph = {};
		m_plan = {};
		m_device = nullptr;
		m_instance_capacity = 0;
		m_lod_history.reset();
		m_lod_hysteresis = 0.1f;
		m_graph_compiled = false;
		m_scene_dirty = true;
		m_view_dirty = false;
	}

	bool Is_Initialized() const noexcept
	{
		return m_device != nullptr;
	}

	MeshHandle Create_Mesh(const StaticMeshSource &source)
	{
		if (!Is_Initialized() || !Validate_Static_Mesh_Source(source))
			return {};

		auto storage = std::make_unique<MeshStorage>();
		storage->vertex_data.assign(source.vertex_data.begin(), source.vertex_data.end());
		storage->index_data.assign(source.index_data.begin(), source.index_data.end());
		storage->parts.assign(source.parts.begin(), source.parts.end());
		if (storage->parts.empty())
			storage->parts.push_back({0, source.index_count, 0});
		for (const MeshPart &part : storage->parts) {
			if (part.index_count == 0 || static_cast<std::uint64_t>(part.first_index) + part.index_count > source.index_count)
				return {};
		}
		Mesh resource;
		resource.vertex_count = source.vertex_count;
		resource.index_count = source.index_count;
		resource.vertex_stride = source.vertex_stride;
		resource.index_format = source.index_format;
		resource.vertex_data = std::span<const std::byte>(storage->vertex_data);
		resource.index_data = std::span<const std::byte>(storage->index_data);
		resource.parts = std::span<const MeshPart>(storage->parts);
		resource.vertex_format = source.vertex_format;
		resource.skin_bone_count = source.skin_bone_count;
		const MeshHandle handle = m_meshes.Create(std::move(resource));
		if (!handle.Is_Valid())
			return {};

		if (handle.Get_Index() >= m_mesh_sources.size())
			m_mesh_sources.resize(static_cast<std::size_t>(handle.Get_Index()) + 1);
		storage->handle = handle;
		m_mesh_sources[handle.Get_Index()] = std::move(storage);
		if (!m_residency->Upload_Mesh(handle, m_meshes)) {
			m_mesh_sources[handle.Get_Index()] = {};
			m_meshes.Destroy(handle);
			return {};
		}

		m_scene_dirty = true;
		return handle;
	}

	bool Configure_Mesh_LODs(MeshHandle base_mesh, std::span<const MeshLod> lods) noexcept
	{
		if (!Is_Initialized() || !base_mesh.Is_Valid() || lods.size() > Mesh::MaxLodCount)
			return false;

		Mesh *mesh = m_meshes.Resolve(base_mesh);
		if (mesh == nullptr)
			return false;
		for (const MeshLod &lod : lods) {
			if (!lod.mesh.Is_Valid() || lod.mesh == base_mesh || m_meshes.Resolve(lod.mesh) == nullptr
				|| std::isnan(lod.max_screen_size) || lod.max_screen_size < 0.0f)
				return false;
		}

		mesh->lods.fill({});
		for (std::size_t index = 0; index < lods.size(); ++index)
			mesh->lods[index] = lods[index];
		mesh->lod_count = static_cast<std::uint8_t>(lods.size());
		mesh->Mark_Dirty();
		m_scene_dirty = true;
		return true;
	}

	SkeletonHandle Create_Skeleton(std::span<const SkeletonBone> bones, std::span<const SkeletonAttachment> attachments = {})
	{
		if (!Is_Initialized())
			return {};
		return Graphics::Create_Skeleton(m_skeletons, {bones, attachments});
	}

	bool Destroy_Skeleton(SkeletonHandle handle) noexcept
	{
		return Is_Initialized() && m_skeletons.Destroy(handle);
	}

	bool Is_Skeleton_Valid(SkeletonHandle handle) const noexcept
	{
		return Is_Initialized() && m_skeletons.Resolve(handle) != nullptr;
	}

	const SkeletonPool &Skeletons() const noexcept
	{
		return m_skeletons;
	}

	AnimationClipHandle Create_Animation_Clip(const AnimationClipDescription &description)
	{
		if (!Is_Initialized())
			return {};
		return Graphics::Create_Animation_Clip(m_animations, description);
	}

	bool Destroy_Animation_Clip(AnimationClipHandle handle) noexcept
	{
		return Is_Initialized() && m_animations.Destroy(handle);
	}

	bool Is_Animation_Valid(AnimationClipHandle handle) const noexcept
	{
		return Is_Initialized() && m_animations.Resolve(handle) != nullptr;
	}

	bool Is_Animation_Valid_For_Skeleton(AnimationClipHandle animation, SkeletonHandle skeleton) const noexcept
	{
		const AnimationClip *clip = m_animations.Resolve(animation);
		const Skeleton *resource = m_skeletons.Resolve(skeleton);
		return clip != nullptr && resource != nullptr && clip->Bone_Count() == resource->Bone_Count();
	}

	const AnimationClipPool &Animations() const noexcept
	{
		return m_animations;
	}

	PoseHandle Create_Pose(std::span<const RenderTransform> matrices)
	{
		return Is_Initialized() ? m_bone_matrices.Create(matrices) : PoseHandle{};
	}

	bool Update_Pose(PoseHandle handle, std::span<const RenderTransform> matrices) noexcept
	{
		return Is_Initialized() && m_bone_matrices.Update(handle, matrices);
	}

	bool Destroy_Pose(PoseHandle handle) noexcept
	{
		return Is_Initialized() && m_bone_matrices.Destroy(handle);
	}

	bool Is_Pose_Valid(PoseHandle handle) const noexcept
	{
		return Is_Initialized() && m_bone_matrices.Is_Valid(handle);
	}

	BoneMatrixRange Pose_Range(PoseHandle handle) const noexcept
	{
		return Is_Initialized() ? m_bone_matrices.Range(handle) : BoneMatrixRange{};
	}

	bool Destroy_Mesh(MeshHandle handle) noexcept
	{
		if (!handle.Is_Valid() || m_device == nullptr || m_meshes.Resolve(handle) == nullptr)
			return false;

		m_residency->Destroy_Mesh(handle);
		if (!m_meshes.Destroy(handle))
			return false;
		if (handle.Get_Index() < m_mesh_sources.size())
			m_mesh_sources[handle.Get_Index()] = {};
		m_scene_dirty = true;
		return true;
	}

	MaterialHandle Default_Material() const noexcept
	{
		return m_default_material;
	}

	InstanceHandle Create_Instance(const RenderInstance &instance)
	{
		if (!Is_Initialized() || !instance.mesh.Is_Valid() || m_meshes.Resolve(instance.mesh) == nullptr || instance.material != m_default_material
			|| (instance.skeleton.Is_Valid() && m_skeletons.Resolve(instance.skeleton) == nullptr)
			|| (instance.pose.Is_Valid() && !m_bone_matrices.Is_Valid(instance.pose)))
			return {};

		const InstanceHandle handle = m_scene.Create(instance);
		m_scene_dirty = true;
		return handle;
	}

	bool Update_Instance(InstanceHandle handle, const RenderInstance &instance) noexcept
	{
		if (!Is_Initialized() || instance.material != m_default_material || !m_meshes.Resolve(instance.mesh)
			|| (instance.skeleton.Is_Valid() && m_skeletons.Resolve(instance.skeleton) == nullptr)
			|| (instance.pose.Is_Valid() && !m_bone_matrices.Is_Valid(instance.pose)) || !m_scene.Update(handle, instance))
			return false;

		return Record_Dirty_Instance(handle);
	}

	bool Update_Instance_Visibility(InstanceHandle handle, SubmeshVisibilityMask visibility_mask) noexcept
	{
		if (!Is_Initialized() || !m_scene.Update_Visibility(handle, visibility_mask))
			return false;

		if (!m_scene_dirty && m_dirty_instances.size() < m_dirty_instances.capacity())
			m_dirty_instances.push_back(handle);
		return true;
	}

	bool Update_Instance_Pose(InstanceHandle handle, PoseHandle pose) noexcept
	{
		if (!Is_Initialized() || (pose.Is_Valid() && !m_bone_matrices.Is_Valid(pose))
			|| !m_scene.Update_Pose(handle, pose))
			return false;

		return Record_Dirty_Instance(handle);
	}

	AttachmentLinkHandle Attach_Instance(InstanceHandle child, InstanceHandle parent,
		const AttachmentTarget &target, const RenderTransform &child_local_transform = Identity_Render_Transform())
	{
		if (!Is_Initialized())
			return {};
		return m_attachments.Attach(m_scene, child, parent, target, child_local_transform);
	}

	bool Detach_Instance(AttachmentLinkHandle handle) noexcept
	{
		return Is_Initialized() && m_attachments.Detach(handle);
	}

	bool Update_Attachment_Target(AttachmentLinkHandle handle, const AttachmentTarget &target) noexcept
	{
		return Is_Initialized() && m_attachments.Update_Target(handle, target);
	}

	bool Update_Attachment_Transform(AttachmentLinkHandle handle, const RenderTransform &transform) noexcept
	{
		return Is_Initialized() && m_attachments.Update_Child_Local_Transform(handle, transform);
	}

	bool Update_Attachments() noexcept
	{
		if (!Is_Initialized() || !m_attachments.Update(m_scene, m_skeletons, m_bone_matrices))
			return false;
		for (const InstanceHandle child : m_attachments.Changed_Children())
			Record_Dirty_Instance(child);
		m_attachments.Clear_Changed();
		return true;
	}

	const AttachmentGraph &Attachments() const noexcept
	{
		return m_attachments;
	}

	std::size_t Mesh_Part_Count(MeshHandle handle) const noexcept
	{
		const Mesh *mesh = m_meshes.Resolve(handle);
		return mesh == nullptr ? 0 : mesh->parts.size();
	}

	bool Destroy_Instance(InstanceHandle handle) noexcept
	{
		m_attachments.Remove_Instance(handle);
		if (!m_scene.Destroy(handle))
			return false;

		m_scene_dirty = true;
		m_dirty_instances.clear();
		return true;
	}

	void Set_View(const View &view) noexcept
	{
		m_view = view;
		m_gpu_view.view_projection = Multiply(view.projection_matrix, view.view_matrix);
		m_view_dirty = true;
	}

	void Set_LOD_Hysteresis(float hysteresis) noexcept
	{
		if (!std::isfinite(hysteresis) || hysteresis < 0.0f)
			m_lod_hysteresis = 0.0f;
		else if (hysteresis > 0.99f)
			m_lod_hysteresis = 0.99f;
		else
			m_lod_hysteresis = hysteresis;
	}

	bool Render(CommandList &command_list, const FrameTargets &targets, bool clear_targets = true) noexcept
	{
		if (!Is_Initialized() || !targets.backbuffer.texture.Is_Valid() || !targets.depth.texture.Is_Valid())
			return false;
		if (!Update_Attachments())
			return false;
		if (!Sync_GPU_Data())
			return false;

		if (!Build_Visible_Set(m_scene, m_view, *m_visible))
			return false;
		if (!Build_LOD_Set(m_scene, m_meshes, *m_visible, m_view, *m_lod, *m_lod_history, {m_lod_hysteresis}))
			return false;
		if (!Build_Draw_Data(*m_lod, m_gpu_scene, {0, m_pipeline, 0, m_skinned_pipeline}, *m_draws))
			return false;

		m_bindings[0] = GraphResourceBinding::Texture(m_color_resource, targets.backbuffer.texture);
		m_bindings[1] = GraphResourceBinding::Texture(m_depth_resource, targets.depth.texture);
		if (!m_graph_compiled) {
			if (!m_plan.Compile(m_graph, m_bindings))
				return false;
			m_graph_compiled = true;
		}

		const RHIViewport viewport{
			targets.backbuffer.width != 0 ? 0u : 0u,
			targets.backbuffer.height != 0 ? 0u : 0u,
			targets.backbuffer.width,
			targets.backbuffer.height,
			0.0f,
			1.0f
		};
		const OpaquePassInput input{
			m_draws->Records(),
			{m_mesh_bindings.data(), m_gpu_scene.Meshes().size()},
			m_bindless.Resources(),
			m_color_resource,
			m_depth_resource,
			viewport,
			{0.035f, 0.045f, 0.075f, 1.0f},
			1.0f,
			clear_targets,
			clear_targets,
			{m_mesh_part_bindings.data(), m_mesh_part_bindings.size()}
		};
		return m_plan.Execute(m_graph, command_list, [&input](GraphPassHandle, CommandList &commands, const PassResources &resources) noexcept {
			return OpaquePass::Execute(commands, resources, input);
		});
	}

private:
	struct MeshStorage final
	{
		MeshHandle handle{};
		std::vector<std::byte> vertex_data;
		std::vector<std::byte> index_data;
		std::vector<MeshPart> parts;
	};

	static std::array<float, 16> Multiply(const Matrix4x4 &left, const Matrix4x4 &right) noexcept
	{
		std::array<float, 16> result{};
		for (std::size_t row = 0; row < 4; ++row)
			for (std::size_t column = 0; column < 4; ++column)
				for (std::size_t element = 0; element < 4; ++element)
					result[row * 4 + column] += left(row, element) * right(element, column);
		return result;
	}

	bool Record_Dirty_Instance(InstanceHandle handle) noexcept
	{
		if (!m_scene_dirty) {
			if (m_dirty_instances.size() < m_dirty_instances.capacity())
				m_dirty_instances.push_back(handle);
			else {
				m_scene_dirty = true;
				m_dirty_instances.clear();
			}
		}
		return true;
	}

	bool Sync_GPU_Data() noexcept
	{
		if (!Sync_Bone_Data())
			return false;

		if (m_view_dirty && !m_device->Update_Buffer(m_view_buffer, 0, std::as_bytes(std::span<const GPUViewData>(&m_gpu_view, 1))))
			return false;
		m_view_dirty = false;

		if (m_scene_dirty) {
			if (!m_gpu_scene.Build(m_scene, m_meshes, m_textures, m_samplers, m_materials, &m_bone_matrices))
				return false;
			if (!m_device->Update_Buffer(m_instance_buffer, 0, std::as_bytes(m_gpu_scene.Instances())))
				return m_gpu_scene.Instances().empty();
			if (!Build_Mesh_Bindings())
				return false;
			m_scene_dirty = false;
			m_dirty_instances.clear();
			m_gpu_scene.Clear_Dirty();
			m_bone_matrices.Clear_Dirty();
			return true;
		}

		if (m_dirty_instances.empty()) {
			m_bone_matrices.Clear_Dirty();
			return true;
		}
		for (const InstanceHandle handle : m_dirty_instances) {
			if (!m_gpu_scene.Sync_Instance(handle, m_scene, &m_bone_matrices))
				return false;
		}
		const std::span<const GPUInstanceData> instances = m_gpu_scene.Instances();
		if (!instances.empty() && !m_device->Update_Buffer(m_instance_buffer, 0, std::as_bytes(instances)))
			return false;
		m_dirty_instances.clear();
		m_gpu_scene.Clear_Dirty();
		m_bone_matrices.Clear_Dirty();
		return true;
	}

	bool Sync_Bone_Data() noexcept
	{
		const std::span<const GPUBoneMatrixData> matrices = m_bone_matrices.Matrices();
		for (const BoneMatrixDirtyRange &range : m_bone_matrices.Dirty_Ranges()) {
			if (range.first_matrix == Invalid_Bone_Matrix_Index || range.count == 0
				|| static_cast<std::uint64_t>(range.first_matrix) + range.count > matrices.size())
				return false;
			const std::span<const GPUBoneMatrixData> update(matrices.data() + range.first_matrix, range.count);
			if (!m_device->Update_Buffer(m_bone_buffer,
				range.first_matrix * static_cast<std::uint32_t>(sizeof(GPUBoneMatrixData)),
				std::as_bytes(update)))
				return false;
		}
		return true;
	}

	bool Build_Mesh_Bindings() noexcept
	{
		const std::span<const GPUMeshData> gpu_meshes = m_gpu_scene.Meshes();
		if (gpu_meshes.size() > m_mesh_bindings.size())
			return false;
		for (OpaqueMeshBinding &binding : m_mesh_bindings)
			binding = {};
		m_mesh_part_bindings.clear();

		bool complete = true;
		m_meshes.For_Each([&](MeshHandle handle, const Mesh &mesh) noexcept {
			const std::uint32_t gpu_index = m_gpu_scene.Mesh_Index(handle);
			const GPUResidentMesh resident = m_residency->Mesh_Info(handle);
			if (gpu_index >= gpu_meshes.size() || !resident.vertex_buffer.Is_Valid() || !resident.index_buffer.Is_Valid()) {
				complete = false;
				return;
			}

			OpaqueMeshBinding &binding = m_mesh_bindings[gpu_index];
			binding.vertex_buffer = resident.vertex_buffer;
			binding.index_buffer = resident.index_buffer;
			binding.index_format = resident.index_format == MeshIndexFormat::UInt16 ? RHIIndexFormat::UInt16 : RHIIndexFormat::UInt32;
			binding.vertex_stride = resident.vertex_stride;
			binding.index_count = resident.index_count;
			binding.vertex_format = mesh.vertex_format == MeshVertexFormat::Position3Color4UV2Skinned
				? RHIVertexFormat::Position3Color4UV2Skinned : RHIVertexFormat::Position3Color4UV2;
			binding.submesh_offset = static_cast<std::uint32_t>(m_mesh_part_bindings.size());
			binding.submesh_count = static_cast<std::uint32_t>(mesh.parts.size());
			for (const MeshPart &part : mesh.parts)
				m_mesh_part_bindings.push_back({part.first_index, part.index_count, part.base_vertex, 0});
		});
		return complete;
	}

	Device *m_device = nullptr;
	std::size_t m_instance_capacity = 0;
	MeshPool m_meshes;
	SkeletonPool m_skeletons;
	AnimationClipPool m_animations;
	BoneMatrixTable m_bone_matrices;
	AttachmentGraph m_attachments;
	TexturePool m_textures;
	SamplerPool m_samplers;
	MaterialPool m_materials;
	RenderScene m_scene;
	GPUScene m_gpu_scene;
	ShaderLibrary m_shaders;
	BindlessResourceTable m_bindless;
	std::unique_ptr<GPUResourceResidency> m_residency;
	std::vector<std::unique_ptr<MeshStorage>> m_mesh_sources;
	std::vector<InstanceHandle> m_dirty_instances;
	std::vector<InstanceHandle> m_visible_storage;
	std::vector<LODSelection> m_lod_storage;
	std::vector<LODHistoryEntry> m_lod_history_storage;
	std::vector<DrawData> m_draw_storage;
	std::vector<OpaqueMeshBinding> m_mesh_bindings;
	std::vector<OpaqueSubmeshBinding> m_mesh_part_bindings;
	std::unique_ptr<VisibleSet> m_visible;
	std::unique_ptr<LODSet> m_lod;
	std::unique_ptr<LODHistory> m_lod_history;
	std::unique_ptr<DrawSet> m_draws;
	View m_view{};
	GPUViewData m_gpu_view{};
	RHIBufferHandle m_instance_buffer{};
	RHIBufferHandle m_light_buffer{};
	RHIBufferHandle m_view_buffer{};
	RHIBufferHandle m_bone_buffer{};
	ShaderHandle m_shader{};
	ShaderHandle m_skinned_shader{};
	PipelineHandle m_pipeline{};
	PipelineHandle m_skinned_pipeline{};
	MaterialHandle m_default_material{};
	RenderGraph m_graph;
	ExecutionPlan m_plan;
	std::array<GraphResourceBinding, 2> m_bindings{};
	GraphResourceHandle m_color_resource{};
	GraphResourceHandle m_depth_resource{};
	GraphPassHandle m_opaque_pass{};
	bool m_scene_dirty = true;
	bool m_view_dirty = false;
	float m_lod_hysteresis = 0.1f;
	bool m_graph_compiled = false;
};

export class StaticMeshBinding final
{
public:
	bool Replace(StaticMeshRenderer &renderer, const StaticMeshSource &source, const RenderTransform &transform,
		const RenderBounds &bounds, MaterialHandle material, RenderInstanceFlags flags,
		SubmeshVisibilityMask visibility_mask = All_Submeshes_Visible, SkeletonHandle skeleton = {},
		AnimationClipHandle animation = {}, AnimationPlaybackMode animation_mode = AnimationPlaybackMode::Loop,
		float animation_time = 0.0f)
	{
		const StaticMeshLODSource lod_source{source, 0.0f};
		return Replace_LODs(renderer, std::span<const StaticMeshLODSource>(&lod_source, 1), transform, bounds,
			material, flags, visibility_mask, skeleton, animation, animation_mode, animation_time);
	}

	bool Replace_LODs(StaticMeshRenderer &renderer, std::span<const StaticMeshLODSource> lod_sources,
		const RenderTransform &transform, const RenderBounds &bounds, MaterialHandle material, RenderInstanceFlags flags,
		SubmeshVisibilityMask visibility_mask = All_Submeshes_Visible, SkeletonHandle skeleton = {},
		AnimationClipHandle animation = {}, AnimationPlaybackMode animation_mode = AnimationPlaybackMode::Loop,
		float animation_time = 0.0f)
	{
		if (!renderer.Is_Initialized() || lod_sources.empty() || lod_sources.size() > Mesh::MaxLodCount + 1u || !material.Is_Valid()
			|| (skeleton.Is_Valid() && !renderer.Is_Skeleton_Valid(skeleton))
			|| (lod_sources[0].source.vertex_format == MeshVertexFormat::Position3Color4UV2Skinned && !skeleton.Is_Valid())
			|| (animation.Is_Valid() && !std::isfinite(animation_time))
			|| (animation.Is_Valid() && !renderer.Is_Animation_Valid_For_Skeleton(animation, skeleton)))
			return false;

		const MeshVertexFormat vertex_format = lod_sources[0].source.vertex_format;
		const std::uint32_t skin_bone_count = lod_sources[0].source.skin_bone_count;
		for (const StaticMeshLODSource &lod_source : lod_sources) {
			if (!Validate_Static_Mesh_Source(lod_source.source)
				|| lod_source.source.vertex_format != vertex_format
				|| lod_source.source.skin_bone_count != skin_bone_count
				|| std::isnan(lod_source.max_screen_size) || lod_source.max_screen_size < 0.0f)
				return false;
		}

		std::array<MeshHandle, Mesh::MaxLodCount + 1> new_lod_meshes{};
		std::size_t new_lod_count = 0;
		auto destroy_new_meshes = [&]() noexcept {
			for (std::size_t index = 0; index < new_lod_count; ++index) {
				if (new_lod_meshes[index].Is_Valid())
					renderer.Destroy_Mesh(new_lod_meshes[index]);
			}
		};
		for (; new_lod_count < lod_sources.size(); ++new_lod_count) {
			new_lod_meshes[new_lod_count] = renderer.Create_Mesh(lod_sources[new_lod_count].source);
			if (!new_lod_meshes[new_lod_count].Is_Valid()) {
				destroy_new_meshes();
				return false;
			}
		}

		std::array<MeshLod, Mesh::MaxLodCount> mesh_lods{};
		for (std::size_t index = 1; index < new_lod_count; ++index)
			mesh_lods[index - 1] = {new_lod_meshes[index], lod_sources[index].max_screen_size};
		if (!renderer.Configure_Mesh_LODs(new_lod_meshes[0], {mesh_lods.data(), new_lod_count - 1})) {
			destroy_new_meshes();
			return false;
		}

		const MeshHandle new_mesh = new_lod_meshes[0];

		PoseHandle new_pose;
		auto cleanup_new_resources = [&]() noexcept {
			if (new_pose.Is_Valid())
				renderer.Destroy_Pose(new_pose);
			if (animation.Is_Valid() && animation != m_animation && animation != m_secondary_animation)
				renderer.Destroy_Animation_Clip(animation);
			destroy_new_meshes();
			if (skeleton.Is_Valid() && skeleton != m_model_instance.skeleton)
				renderer.Destroy_Skeleton(skeleton);
		};

		ModelInstance prepared_model_instance;
		prepared_model_instance.skeleton = skeleton;
		prepared_model_instance.transform = transform;
		if (animation.Is_Valid()
			&& !prepared_model_instance.Set_Animation(renderer.Skeletons(), renderer.Animations(), animation,
				animation_mode, animation_time)) {
			cleanup_new_resources();
			return false;
		}

		std::vector<RenderTransform> rest_pose;
		if (vertex_format == MeshVertexFormat::Position3Color4UV2Skinned) {
			const Graphics::Skeleton *skeleton_resource = renderer.Skeletons().Resolve(skeleton);
			if (skeleton_resource == nullptr) {
				cleanup_new_resources();
				return false;
			}
			if (!animation.Is_Valid()) {
				rest_pose.resize(skeleton_resource->Bone_Count());
				for (BoneIndex bone = 0; bone < skeleton_resource->Bone_Count(); ++bone) {
					if (!skeleton_resource->Rest_Transform(skeleton_resource->Bone(bone), rest_pose[bone])) {
						cleanup_new_resources();
						return false;
					}
				}
			}
			const std::span<const RenderTransform> matrices = animation.Is_Valid()
				? prepared_model_instance.pose.World_Transforms()
				: std::span<const RenderTransform>(rest_pose);
			new_pose = renderer.Create_Pose(matrices);
			if (!new_pose.Is_Valid()) {
				cleanup_new_resources();
				return false;
			}
		}
		const RenderInstance instance = Make_Instance(new_mesh, material, transform, bounds, flags, visibility_mask, new_pose, skeleton);
		if (m_instance.Is_Valid()) {
			if (!renderer.Update_Instance(m_instance, instance)) {
				cleanup_new_resources();
				return false;
			}
			for (std::size_t index = 0; index < m_lod_count; ++index) {
				if (m_lod_meshes[index].Is_Valid())
					renderer.Destroy_Mesh(m_lod_meshes[index]);
			}
		} else {
			const InstanceHandle new_instance = renderer.Create_Instance(instance);
			if (!new_instance.Is_Valid()) {
				cleanup_new_resources();
				return false;
			}
			m_instance = new_instance;
		}

		const SkeletonHandle old_skeleton = m_model_instance.skeleton;
		const PoseHandle old_pose = m_pose;
		m_mesh = new_mesh;
		m_lod_meshes = new_lod_meshes;
		m_lod_count = static_cast<std::uint8_t>(new_lod_count);
		m_material = material;
		m_bounds = bounds;
		m_flags = flags;
		m_visibility_mask = visibility_mask;
		m_skinned = vertex_format == MeshVertexFormat::Position3Color4UV2Skinned;
		m_model_instance = std::move(prepared_model_instance);
		m_pose = new_pose;
		const AnimationClipHandle old_animation = m_animation;
		const AnimationClipHandle old_secondary_animation = m_secondary_animation;
		m_animation = animation;
		m_secondary_animation = {};
		if (old_skeleton.Is_Valid() && old_skeleton != skeleton)
			renderer.Destroy_Skeleton(old_skeleton);
		if (old_animation.Is_Valid() && old_animation != animation)
			renderer.Destroy_Animation_Clip(old_animation);
		if (old_secondary_animation.Is_Valid() && old_secondary_animation != animation
			&& old_secondary_animation != old_animation)
			renderer.Destroy_Animation_Clip(old_secondary_animation);
		if (old_pose.Is_Valid() && old_pose != new_pose)
			renderer.Destroy_Pose(old_pose);
		m_active = true;
		return true;
	}

	bool Update(StaticMeshRenderer &renderer, const RenderTransform &transform, RenderInstanceFlags flags) noexcept
	{
		if (!renderer.Is_Initialized() || !m_active || !m_instance.Is_Valid() || !m_mesh.Is_Valid() || !m_material.Is_Valid())
			return false;

		const RenderInstance instance = Make_Instance(m_mesh, m_material, transform, m_bounds, flags, m_visibility_mask, m_pose,
			m_model_instance.skeleton);
		if (!renderer.Update_Instance(m_instance, instance))
			return false;

		m_flags = flags;
		m_model_instance.transform = transform;
		return true;
	}

	bool Set_Animation(StaticMeshRenderer &renderer, AnimationClipHandle animation,
		AnimationPlaybackMode mode, float time_seconds = 0.0f)
	{
		if (!renderer.Is_Initialized() || !m_active || !m_model_instance.skeleton.Is_Valid()
			|| !renderer.Is_Animation_Valid_For_Skeleton(animation, m_model_instance.skeleton))
			return false;
		if (!m_model_instance.Set_Animation(renderer.Skeletons(), renderer.Animations(), animation, mode, time_seconds))
			return false;
		if (m_skinned && !Sync_Pose(renderer))
			return false;
		const AnimationClipHandle old_animation = m_animation;
		m_animation = animation;
		if (old_animation.Is_Valid() && old_animation != animation)
			renderer.Destroy_Animation_Clip(old_animation);
		return true;
	}

	bool Set_Animation_Blend(StaticMeshRenderer &renderer,
		AnimationClipHandle first_animation, AnimationPlaybackMode first_mode, float first_time,
		AnimationClipHandle second_animation, AnimationPlaybackMode second_mode, float second_time,
		float weight)
	{
		if (!renderer.Is_Initialized() || !m_active || !m_model_instance.skeleton.Is_Valid()
			|| !renderer.Is_Animation_Valid_For_Skeleton(first_animation, m_model_instance.skeleton)
			|| !renderer.Is_Animation_Valid_For_Skeleton(second_animation, m_model_instance.skeleton))
			return false;
		if (!m_model_instance.Set_Animation_Blend(renderer.Skeletons(), renderer.Animations(),
			first_animation, first_mode, first_time, second_animation, second_mode, second_time, weight))
			return false;
		if (m_skinned && !Sync_Pose(renderer))
			return false;

		const AnimationClipHandle old_animation = m_animation;
		const AnimationClipHandle old_secondary_animation = m_secondary_animation;
		m_animation = first_animation;
		m_secondary_animation = second_animation;
		if (old_animation.Is_Valid() && old_animation != first_animation && old_animation != second_animation)
			renderer.Destroy_Animation_Clip(old_animation);
		if (old_secondary_animation.Is_Valid() && old_secondary_animation != first_animation
			&& old_secondary_animation != second_animation && old_secondary_animation != old_animation)
			renderer.Destroy_Animation_Clip(old_secondary_animation);
		return true;
	}

	bool Set_Animation_Blend_State(StaticMeshRenderer &renderer,
		float first_time, float second_time, float weight) noexcept
	{
		if (!renderer.Is_Initialized() || !m_active || !m_secondary_animation.Is_Valid()
			|| !m_pose.Is_Valid())
			return false;
		if (!m_model_instance.Set_Animation_Blend_State(renderer.Skeletons(), renderer.Animations(),
			first_time, second_time, weight))
			return false;
		return !m_skinned || Sync_Pose(renderer);
	}

	bool Clear_Animation(StaticMeshRenderer &renderer) noexcept
	{
		if (!renderer.Is_Initialized())
			return false;
		if (m_skinned && !renderer.Update_Instance_Pose(m_instance, m_pose))
			return false;
		const AnimationClipHandle old_animation = m_animation;
		const AnimationClipHandle old_secondary_animation = m_secondary_animation;
		m_animation = {};
		m_secondary_animation = {};
		m_model_instance.Clear_Animation();
		if (old_animation.Is_Valid())
			renderer.Destroy_Animation_Clip(old_animation);
		if (old_secondary_animation.Is_Valid() && old_secondary_animation != old_animation)
			renderer.Destroy_Animation_Clip(old_secondary_animation);
		return true;
	}

	bool Update_Animation(StaticMeshRenderer &renderer, float delta_seconds) noexcept
	{
		return renderer.Is_Initialized() && m_active && m_animation.Is_Valid()
			&& m_model_instance.Advance_Animation(renderer.Skeletons(), renderer.Animations(), delta_seconds)
			&& (!m_skinned || Sync_Pose(renderer));
	}

	bool Set_Animation_Time(StaticMeshRenderer &renderer, float time_seconds) noexcept
	{
		return renderer.Is_Initialized() && m_active && m_animation.Is_Valid()
			&& m_model_instance.Set_Animation_Time(renderer.Skeletons(), renderer.Animations(), time_seconds)
			&& (!m_skinned || Sync_Pose(renderer));
	}

	bool Set_Animation_Mode(StaticMeshRenderer &renderer, AnimationPlaybackMode mode) noexcept
	{
		return renderer.Is_Initialized() && m_active && m_animation.Is_Valid()
			&& m_model_instance.Set_Animation_Mode(renderer.Skeletons(), renderer.Animations(), mode)
			&& (!m_skinned || Sync_Pose(renderer));
	}

	bool Set_Submesh_Visibility(StaticMeshRenderer &renderer, SubmeshVisibilityMask visibility_mask) noexcept
	{
		if (!renderer.Is_Initialized() || !m_instance.Is_Valid() || !m_mesh.Is_Valid() || !m_material.Is_Valid())
			return false;
		if (!renderer.Update_Instance_Visibility(m_instance, visibility_mask))
			return false;

		m_visibility_mask = visibility_mask;
		return true;
	}

	bool Set_Submesh_Visible(StaticMeshRenderer &renderer, ModelPartId part, bool visible) noexcept
	{
		if (part >= renderer.Mesh_Part_Count(m_mesh))
			return false;

		return Set_Submesh_Visibility(renderer, Graphics::Set_Submesh_Visible(m_visibility_mask, part, visible));
	}

	bool Suspend(StaticMeshRenderer &renderer, const RenderTransform &transform) noexcept
	{
		if (!renderer.Is_Initialized() || !m_instance.Is_Valid() || !m_mesh.Is_Valid() || !m_material.Is_Valid())
			return false;

		const RenderInstance instance = Make_Instance(m_mesh, m_material, transform,
			m_bounds, m_flags | RenderInstanceFlags::Hidden, m_visibility_mask, m_pose, m_model_instance.skeleton);
		if (!renderer.Update_Instance(m_instance, instance))
			return false;

		m_active = false;
		m_model_instance.transform = transform;
		return true;
	}

	void Destroy(StaticMeshRenderer &renderer) noexcept
	{
		if (renderer.Is_Initialized()) {
			if (m_instance.Is_Valid())
				renderer.Destroy_Instance(m_instance);
			for (std::size_t index = 0; index < m_lod_count; ++index) {
				if (m_lod_meshes[index].Is_Valid())
					renderer.Destroy_Mesh(m_lod_meshes[index]);
			}
			if (m_model_instance.skeleton.Is_Valid())
				renderer.Destroy_Skeleton(m_model_instance.skeleton);
			if (m_animation.Is_Valid())
				renderer.Destroy_Animation_Clip(m_animation);
			if (m_secondary_animation.Is_Valid() && m_secondary_animation != m_animation)
				renderer.Destroy_Animation_Clip(m_secondary_animation);
			if (m_pose.Is_Valid())
				renderer.Destroy_Pose(m_pose);
		}
		Reset();
	}

	void Reset() noexcept
	{
		m_mesh = {};
		m_lod_meshes.fill({});
		m_lod_count = 0;
		m_material = {};
		m_instance = {};
		m_bounds = {};
		m_flags = RenderInstanceFlags::None;
		m_visibility_mask = All_Submeshes_Visible;
		m_model_instance = {};
		m_animation = {};
		m_secondary_animation = {};
		m_pose = {};
		m_skinned = false;
		m_active = false;
	}

	bool Is_Active() const noexcept
	{
		return m_active;
	}

	bool Has_Instance() const noexcept
	{
		return m_instance.Is_Valid();
	}

	MeshHandle Mesh() const noexcept
	{
		return m_mesh;
	}

	std::span<const MeshHandle> Mesh_LODs() const noexcept
	{
		return {m_lod_meshes.data(), m_lod_count};
	}

	MaterialHandle Material() const noexcept
	{
		return m_material;
	}

	InstanceHandle Instance() const noexcept
	{
		return m_instance;
	}

	const RenderBounds &Bounds() const noexcept
	{
		return m_bounds;
	}

	SubmeshVisibilityMask Visibility() const noexcept
	{
		return m_visibility_mask;
	}

	SkeletonHandle Skeleton() const noexcept
	{
		return m_model_instance.skeleton;
	}

	AnimationClipHandle Animation() const noexcept
	{
		return m_animation;
	}

	AnimationClipHandle Secondary_Animation() const noexcept
	{
		return m_secondary_animation;
	}

	float Blend_Weight() const noexcept
	{
		return m_model_instance.blend_weight;
	}

	PoseHandle Pose() const noexcept
	{
		return m_pose;
	}

	bool Get_Bone_Transform(const StaticMeshRenderer &renderer, BoneHandle bone, RenderTransform &result) const noexcept
	{
		return m_model_instance.Get_Bone_Transform(renderer.Skeletons(), bone, result);
	}

	bool Get_Attachment_Transform(const StaticMeshRenderer &renderer, AttachmentHandle attachment, RenderTransform &result) const noexcept
	{
		return m_model_instance.Get_Attachment_Transform(renderer.Skeletons(), attachment, result);
	}

private:
	bool Sync_Pose(StaticMeshRenderer &renderer) noexcept
	{
		return m_pose.Is_Valid() && renderer.Update_Pose(m_pose, m_model_instance.pose.World_Transforms());
	}

	static RenderInstance Make_Instance(MeshHandle mesh, MaterialHandle material, const RenderTransform &transform,
		const RenderBounds &bounds, RenderInstanceFlags flags, SubmeshVisibilityMask visibility_mask, PoseHandle pose = {},
		SkeletonHandle skeleton = {}) noexcept
	{
		RenderInstance instance;
		instance.transform = transform;
		instance.bounds = bounds;
		instance.mesh = mesh;
		instance.material = material;
		instance.skeleton = skeleton;
		instance.pose = pose;
		instance.flags = flags;
		instance.visibility_mask = visibility_mask;
		return instance;
	}

	MeshHandle m_mesh{};
	std::array<MeshHandle, Mesh::MaxLodCount + 1> m_lod_meshes{};
	std::uint8_t m_lod_count = 0;
	MaterialHandle m_material{};
	InstanceHandle m_instance{};
	RenderBounds m_bounds{};
	RenderInstanceFlags m_flags = RenderInstanceFlags::None;
	SubmeshVisibilityMask m_visibility_mask = All_Submeshes_Visible;
	ModelInstance m_model_instance{};
	AnimationClipHandle m_animation{};
	AnimationClipHandle m_secondary_animation{};
	PoseHandle m_pose{};
	bool m_skinned = false;
	bool m_active = false;
};

export StaticMeshRenderer &GetStaticMeshRenderer() noexcept
{
	static StaticMeshRenderer renderer;
	return renderer;
}

}
