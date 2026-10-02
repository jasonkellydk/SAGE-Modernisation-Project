export module games.generalszh.presentation.rendering.tracer_rendering;
import std;

export import games.generalszh.presentation.effects.tracers;
import Graphics.Scene.Lines.Tracer;
import Graphics.Scene.Props.Renderer;
import Graphics.RHI;

// The tracers as W3DTracerDraw draws them: each a box its length along where it heads (local X from 0) and its width
// across, of its colour, at its opacity (see through below 1). One mesh per length, width and colour, made once.
export namespace generalszh::presentation
{
class TracerRendering
{
public:
	TracerRendering() = default;
	TracerRendering(const TracerRendering &) = delete;
	TracerRendering &operator=(const TracerRendering &) = delete;
	~TracerRendering()
	{
		for (auto &[description, renderer] : m_renderers)
			renderer->Release(Graphics::Get_Prop_Renderer());
	}

	bool Draw(Graphics::CommandList &commands, const std::array<float, 16> &viewProjection, const std::array<float, 16> &view,
		const std::array<float, 3> &eye, const Tracers &tracers)
	{
		bool drawn = true;
		for (std::size_t row = 0; row < tracers.Size(); ++row)
		{
			const Graphics::TracerDescription description{tracers.lengths[row], tracers.widths[row],
				{tracers.colors[row][0], tracers.colors[row][1], tracers.colors[row][2], 1.0f}};
			Graphics::TracerRenderer *renderer = RendererFor(description);
			if (renderer == nullptr)
				continue;
			Graphics::TracerDrawData data;
			data.view_projection = viewProjection;
			data.view = view;
			data.world = World(tracers.positions[row], tracers.directions[row]);
			data.camera_position = {eye[0], eye[1], eye[2], 1.0f};
			data.camera_depth = {view[8], view[9], view[10], view[11]};
			data.opacity = tracers.Opacity(row);
			drawn = renderer->Draw(Graphics::Get_Prop_Renderer(), commands, data) && drawn;
		}
		return drawn;
	}

private:
	// Transform::From_Unit_Forward_Direction: local X along `forward` from `at` (row-major, the basis in columns).
	static std::array<float, 16> World(const std::array<float, 3> &at, const std::array<float, 3> &forward)
	{
		std::array<float, 3> side{-forward[1], forward[0], 0.0f}; // up x forward, flattened
		float length = std::sqrt(side[0] * side[0] + side[1] * side[1]);
		if (length < 1e-6f)
			side = {0.0f, 1.0f, 0.0f}, length = 1.0f;
		for (float &axis : side)
			axis /= length;
		const std::array<float, 3> up{forward[1] * side[2] - forward[2] * side[1], forward[2] * side[0] - forward[0] * side[2],
			forward[0] * side[1] - forward[1] * side[0]};
		return {forward[0], side[0], up[0], at[0], forward[1], side[1], up[1], at[1], forward[2], side[2], up[2], at[2], 0.0f, 0.0f, 0.0f, 1.0f};
	}

	Graphics::TracerRenderer *RendererFor(const Graphics::TracerDescription &description)
	{
		for (auto &[known, renderer] : m_renderers)
			if (known.length == description.length && known.width == description.width && known.color == description.color)
				return renderer.get();
		auto renderer = std::make_unique<Graphics::TracerRenderer>();
		if (!renderer->Set_Description(Graphics::Get_Prop_Renderer(), description))
			return nullptr;
		m_renderers.emplace_back(description, std::move(renderer));
		return m_renderers.back().second.get();
	}

	std::vector<std::pair<Graphics::TracerDescription, std::unique_ptr<Graphics::TracerRenderer>>> m_renderers;
};
}
