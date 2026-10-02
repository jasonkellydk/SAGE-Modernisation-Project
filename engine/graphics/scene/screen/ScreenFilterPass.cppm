export module Graphics.Scene.Screen.FilterPass;
import std;

export import Graphics.RenderGraph.Execution;
export import Graphics.Scene.Screen.Filters;

// A view's screen filter as a post pass (the original's W3DShaderManager render-to-texture filters: grey, motion
// blur): the frame drawn so far copied to a picture (or the last picture kept), then quads of that picture drawn back
// over the frame through the screen filter shader, each with its own parameters and blending. The copy and the draw are
// render graph passes, so the frame is read before it is drawn over.
namespace Graphics
{
export struct ScreenFilterQuadDraw final
{
	std::array<ScreenFilterVertex, 4> vertices{};
	ScreenFilterParameters parameters{};
	ScreenFilterStyle style{};
};

export class ScreenFilterPass final
{
public:
	~ScreenFilterPass() { Shutdown(); }

	bool Initialize(Device &device)
	{
		if (m_device == &device && m_graph)
			return true;
		Shutdown();
		m_graph = std::make_unique<RenderGraph>();
		m_graph->Reserve(4, 2, 5);
		m_source_resource = m_graph->Create_Resource({GraphResourceKind::Texture});
		m_picture_resource = m_graph->Create_Resource({GraphResourceKind::Texture});
		m_color_resource = m_graph->Create_Resource({GraphResourceKind::Texture});
		m_depth_resource = m_graph->Create_Resource({GraphResourceKind::Texture});
		const std::array<GraphResourceUse, 2> copy_uses = {GraphResourceUse::Read(m_source_resource), GraphResourceUse::Write(m_picture_resource)};
		const std::array<GraphResourceUse, 3> draw_uses = {GraphResourceUse::Read(m_picture_resource), GraphResourceUse::Write(m_color_resource),
			GraphResourceUse::Read(m_depth_resource)};
		m_copy_pass = m_graph->Add_Pass({40}, copy_uses);
		m_draw_pass = m_graph->Add_Pass({41}, draw_uses);
		if (!m_source_resource.Is_Valid() || !m_picture_resource.Is_Valid() || !m_color_resource.Is_Valid() || !m_depth_resource.Is_Valid()
			|| !m_copy_pass.Is_Valid() || !m_draw_pass.Is_Valid()) {
			m_graph.reset();
			return false;
		}
		m_device = &device;
		return true;
	}

	void Shutdown() noexcept
	{
		if (m_device != nullptr && m_picture.Is_Valid())
			m_device->Destroy_Texture(m_picture);
		m_picture = {};
		m_width = 0;
		m_height = 0;
		m_plan = {};
		m_graph.reset();
		m_device = nullptr;
	}

	// `keep_picture`: draw from the last frame's picture instead of copying this frame's (there must be one this size).
	bool Render(CommandList &commands, RHITextureHandle color_target, RHITextureHandle depth_target, RHIViewport viewport,
		std::span<const ScreenFilterQuadDraw> quads, bool keep_picture = false, RHITextureFormat color_format = RHITextureFormat::BGRA8_UNorm) noexcept
	{
		if (m_device == nullptr || !m_graph || quads.empty() || !color_target.Is_Valid() || !depth_target.Is_Valid() || viewport.width == 0
			|| viewport.height == 0)
			return quads.empty();
		const bool fresh = Ensure_Picture(static_cast<std::uint32_t>(viewport.width), static_cast<std::uint32_t>(viewport.height), color_format);
		if (!m_picture.Is_Valid())
			return false;
		const bool copy = fresh || !keep_picture;
		m_bindings[0] = GraphResourceBinding::Texture(m_source_resource, color_target);
		m_bindings[1] = GraphResourceBinding::Texture(m_picture_resource, m_picture);
		m_bindings[2] = GraphResourceBinding::Texture(m_color_resource, color_target);
		m_bindings[3] = GraphResourceBinding::Texture(m_depth_resource, depth_target);
		if (!m_plan.Is_Valid() && !m_plan.Compile(*m_graph, m_bindings))
			return false;
		auto &filters = Get_Screen_Filter_Renderer();
		return m_plan.Execute(*m_graph, commands, [&](GraphPassHandle pass, CommandList &command_list, const PassResources &resources) noexcept {
			if (pass == m_copy_pass)
				return !copy || command_list.Copy_Texture(resources.Texture(m_source_resource), resources.Texture(m_picture_resource));
			if (pass != m_draw_pass)
				return false;
			if (!command_list.Set_Render_Targets(resources.Texture(m_color_resource), resources.Texture(m_depth_resource))
				|| !command_list.Set_Viewport(viewport))
				return false;
			bool drawn = true;
			for (const ScreenFilterQuadDraw &quad : quads)
				drawn = filters.Draw(command_list, std::span<const ScreenFilterVertex, 4>(quad.vertices), quad.parameters, quad.style,
					resources.Texture(m_picture_resource)) && drawn;
			return drawn;
		});
	}

private:
	// The picture the size of the frame; true when it is new (nothing kept in it yet).
	bool Ensure_Picture(std::uint32_t width, std::uint32_t height, RHITextureFormat format) noexcept
	{
		if (m_picture.Is_Valid() && m_width == width && m_height == height && m_format == format)
			return false;
		if (m_picture.Is_Valid())
			m_device->Destroy_Texture(m_picture);
		m_picture = m_device->Create_Texture({width, height, 1, format, static_cast<std::uint32_t>(RHITextureUsage::ShaderResource)});
		m_width = width;
		m_height = height;
		m_format = format;
		m_plan = {};
		return true;
	}

	Device *m_device = nullptr;
	RHITextureHandle m_picture{};
	std::uint32_t m_width = 0;
	std::uint32_t m_height = 0;
	RHITextureFormat m_format = RHITextureFormat::BGRA8_UNorm;
	std::unique_ptr<RenderGraph> m_graph;
	ExecutionPlan m_plan;
	std::array<GraphResourceBinding, 4> m_bindings{};
	GraphResourceHandle m_source_resource{};
	GraphResourceHandle m_picture_resource{};
	GraphResourceHandle m_color_resource{};
	GraphResourceHandle m_depth_resource{};
	GraphPassHandle m_copy_pass{};
	GraphPassHandle m_draw_pass{};
};

// The screen's quad corners for a view (the original's v[0..3]: bottom right, top right, bottom left, top left) in
// clip space, each with its colour and texture coordinates.
export inline std::array<ScreenFilterVertex, 4> Screen_Filter_Corners(const std::array<std::array<float, 2>, 4> &uv, const std::array<float, 4> &color) noexcept
{
	constexpr std::array<std::array<float, 2>, 4> clip{{{1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, -1.0f}, {-1.0f, 1.0f}}};
	std::array<ScreenFilterVertex, 4> vertices{};
	for (std::size_t corner = 0; corner < 4; ++corner) {
		vertices[corner].position = {clip[corner][0], clip[corner][1], 0.0f};
		vertices[corner].color = color;
		vertices[corner].uv = uv[corner];
	}
	return vertices;
}
}
