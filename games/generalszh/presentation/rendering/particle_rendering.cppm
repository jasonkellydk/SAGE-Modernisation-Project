export module games.generalszh.presentation.rendering.particle_rendering;
import std;

import engine.effects.particles.simulation.particle_world;
export import games.generalszh.presentation.effects.snow;
import Graphics.RHI;

// Draws the effects' particles with the graphics particle renderer: one
// emitter per texture and blend, each particle between presentation frames
// (moved `alpha` of its per-frame motion), textures loaded from the asset
// cache once.
export namespace generalszh::presentation
{
// A beam to draw this frame (a laser's, in additive light): from start to end, its width and colour, its texture
// tiled `uvScale` times along it and scrolled by `uvOffset`.
struct BeamSegment
{
	std::array<float, 3> start{};
	std::array<float, 3> end{};
	float width{0.0f};
	std::array<float, 4> color{};
	std::string_view texture;
	float uvScale{1.0f};
	float uvOffset{0.0f};
	bool additive{true}; // false: blended by its alpha (a rope's lines)
	bool depthTest{true}; // false: drawn over everything (W3DWaypointBuffer's lines: PASS_ALWAYS)
};

class ParticleRendering
{
public:
	ParticleRendering();
	~ParticleRendering();
	ParticleRendering(const ParticleRendering &) = delete;
	ParticleRendering &operator=(const ParticleRendering &) = delete;

	// `terrainLowest`: the map's lowest height (BaseHeightMapRenderObjClass::m_minHeight), which bounds the box of what the
	// camera can see that particles are culled to (and their field count taken in); none: not culled.
	// `snow`: the frame's snow flakes (ExtractSnow), drawn after the effects' particles as W3DParticleSystemManager does
	// (TheSnowManager->render): alpha blended (_PresetAlphaShader), white, its texture; point sprites sized in pixels,
	// else quads facing the camera.
	void Draw(Graphics::Device &device, const engine::effects::ParticleWorld &particles, const std::array<float, 16> &view,
		const std::array<float, 16> &projection, const std::array<float, 3> &eye, float alpha, std::span<const BeamSegment> lasers = {},
		const SnowFlakes *snow = nullptr, float terrainLowest = std::numeric_limits<float>::quiet_NaN());

	std::size_t DrawnParticles() const noexcept;
	std::size_t DrawnStreakSegments() const noexcept;
	// The last Draw's on-screen ground-aligned AREA_EFFECT particles (W3DParticleSystemManager's m_fieldParticleCount),
	// for ParticleWorld::SetFieldParticleCount.
	std::size_t FieldParticles() const noexcept;

private:
	struct State;
	std::unique_ptr<State> m_state;
};
}
