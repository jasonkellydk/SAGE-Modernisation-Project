export module games.generalszh.presentation.rendering.object_rendering;
import std;
export import games.generalszh.presentation.effects.light_pulses;

import engine.level.model.level;
export import games.generalszh.presentation.objects.resources.object_instance;
export import games.generalszh.presentation.objects.resources.look_models;
export import games.generalszh.presentation.models.model_library;
import Graphics.RHI;
export import games.generalszh.presentation.rendering.shroud_pixels;

// Draws the simulation's objects as their W3D models. Models load through
// the asset cache in the background the first time a definition is seen;
// an object shows once its model is ready. The interface stays light; the
// asset and prop renderer imports live in the implementation.
export namespace generalszh::presentation
{

class ObjectRendering
{
public:
	ObjectRendering();
	~ObjectRendering();
	ObjectRendering(const ObjectRendering &) = delete;
	ObjectRendering &operator=(const ObjectRendering &) = delete;

	// Draws the instances whose models the library has ready (their parts in each instance's pose).
	void Draw(Graphics::Device &device, Graphics::CommandList &commands, const std::array<float, 16> &viewProjection,
		const std::array<float, 3> &eye, const engine::level::LightingSet *lighting, std::span<const ObjectInstance> instances,
		ModelLibrary &library, std::span<const ShownLight> lights = {}, const ShroudBinding *shroud = nullptr, float infantryLightScale = 1.5f);

	// The instances that cast shadows, as directional shadow casters for this frame (before the shadow maps render).
	void SubmitShadows(Graphics::Device &device, std::span<const ObjectInstance> instances, ModelLibrary &library);

	// Binds the GPU side of the instances' ready models now (after the library waited for them at the load screen).
	void Preload(Graphics::Device &device, std::span<const ObjectInstance> instances, ModelLibrary &library);

	// Models that failed to bind for drawing, one per line.
	const std::string &Failures() const noexcept;

private:
	struct State;
	std::unique_ptr<State> m_state;
};
}
