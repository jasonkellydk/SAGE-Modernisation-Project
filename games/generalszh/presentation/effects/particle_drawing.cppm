export module games.generalszh.presentation.effects.particle_drawing;
import std;

export import engine.effects.particles.definitions.particle_system_definition;
export import engine.effects.particles.simulation.particle_world;
export import games.generalszh.presentation.effects.heat_haze;

// How the original draws a particle system (W3DParticleSystemManager::doParticles, PointGroupClass::RenderVolumeParticle):
// free functions the particle rendering applies per system, kept here so their numbers can be pinned.
export namespace generalszh::presentation
{
enum class ParticleDrawing : std::uint8_t
{
	None,     // not drawn
	Quads,    // a point group of camera-facing (or ground-aligned) quads
	Streak,   // a streak line through its particles
	HeatHaze, // screen smudges (heat_haze.cppm)
};

// doParticles: a system of drawables is skipped (isUsingDrawables; the original keeps the type only "for backwards
// compatibility when we supported drawables" and never makes or draws them); a SMUD-textured system is heat haze; a
// STREAK system a streak line; the rest quads (VOLUME_PARTICLE ones stacked: VolumeLayers).
inline ParticleDrawing DrawingOf(const engine::effects::ParticleSystemDefinition &definition)
{
	if (definition.kind == engine::effects::ParticleKind::Drawable)
		return ParticleDrawing::None;
	if (IsHeatHaze(definition))
		return ParticleDrawing::HeatHaze;
	if (definition.kind == engine::effects::ParticleKind::Streak)
		return ParticleDrawing::Streak;
	return ParticleDrawing::Quads;
}

// ParticleSystem::getVolumeParticleDepth (EA's): OPTIMUM_VOLUME_PARTICLE_DEPTH (6) layers for a VOLUME_PARTICLE system,
// none (drawn once) otherwise.
inline std::uint32_t VolumeLayers(const engine::effects::ParticleSystemDefinition &definition)
{
	return definition.kind == engine::effects::ParticleKind::Volume ? 6u : 1u;
}

// PointGroupClass::RenderVolumeParticle: layer `layer` of `layers` draws every billboarded point moved towards the
// camera by layer x firstSize x (0.1 / layers), where firstSize is the size of the system's first point drawn (the
// original reads *current_size, the array's first element, for every point); ground-aligned (not billboarded) points
// are not moved. Each layer is drawn whole, with the same colour and alpha.
inline std::array<float, 3> VolumeLayerPosition(const std::array<float, 3> &point, const std::array<float, 3> &camera, float firstSize,
	std::uint32_t layers, std::uint32_t layer, bool billboard)
{
	if (!billboard || layers <= 1)
		return point;
	const float shift = static_cast<float>(layer) * firstSize * (0.1f / static_cast<float>(layers));
	std::array<float, 3> toCamera{camera[0] - point[0], camera[1] - point[1], camera[2] - point[2]};
	const float length = std::sqrt(toCamera[0] * toCamera[0] + toCamera[1] * toCamera[1] + toCamera[2] * toCamera[2]);
	if (length > 0.0f)
		for (float &c : toCamera)
			c /= length;
	return {point[0] + toCamera[0] * shift, point[1] + toCamera[1] * shift, point[2] + toCamera[2] * shift};
}

// doParticles' m_fieldParticleCount: a drawn particle counts when its system is AREA_EFFECT and ground aligned.
inline bool CountsAsFieldParticle(const engine::effects::ParticleSystemDefinition &definition)
{
	return definition.priority == engine::effects::ParticlePriority::AreaEffect && definition.groundAligned;
}

// An axis-aligned box as AABoxClass keeps it: its centre and half extents.
struct VisibleBox
{
	std::array<float, 3> centre{};
	std::array<float, 3> extent{};
};

// The view frustum's 8 corners in the world, near ones first (0..3) and the far one at the end of each edge after them
// (4..7), from the row-major view-projection matrix (clip = matrix x world; depth 0 near, 1 far). None when it cannot
// be inverted.
inline std::optional<std::array<std::array<float, 3>, 8>> FrustumCorners(const std::array<float, 16> &viewProjection)
{
	std::array<double, 16> m{}, inverse{};
	for (std::size_t index = 0; index < 16; ++index)
	{
		m[index] = viewProjection[index];
		inverse[index] = index % 5 == 0 ? 1.0 : 0.0;
	}
	for (std::size_t column = 0; column < 4; ++column)
	{
		std::size_t pivot = column;
		for (std::size_t row = column + 1; row < 4; ++row)
			if (std::fabs(m[row * 4 + column]) > std::fabs(m[pivot * 4 + column]))
				pivot = row;
		if (std::fabs(m[pivot * 4 + column]) < 1e-12)
			return std::nullopt;
		for (std::size_t k = 0; k < 4; ++k)
		{
			std::swap(m[column * 4 + k], m[pivot * 4 + k]);
			std::swap(inverse[column * 4 + k], inverse[pivot * 4 + k]);
		}
		const double scale = 1.0 / m[column * 4 + column];
		for (std::size_t k = 0; k < 4; ++k)
		{
			m[column * 4 + k] *= scale;
			inverse[column * 4 + k] *= scale;
		}
		for (std::size_t row = 0; row < 4; ++row)
		{
			if (row == column)
				continue;
			const double factor = m[row * 4 + column];
			for (std::size_t k = 0; k < 4; ++k)
			{
				m[row * 4 + k] -= factor * m[column * 4 + k];
				inverse[row * 4 + k] -= factor * inverse[column * 4 + k];
			}
		}
	}
	std::array<std::array<float, 3>, 8> corners{};
	constexpr std::array<std::array<double, 2>, 4> edges{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
	for (std::size_t edge = 0; edge < 4; ++edge)
		for (std::size_t end = 0; end < 2; ++end)
		{
			const std::array<double, 4> clip{edges[edge][0], edges[edge][1], static_cast<double>(end), 1.0};
			std::array<double, 4> world{};
			for (std::size_t row = 0; row < 4; ++row)
				for (std::size_t k = 0; k < 4; ++k)
					world[row] += inverse[row * 4 + k] * clip[k];
			if (world[3] == 0.0)
				return std::nullopt;
			corners[edge + end * 4] = {static_cast<float>(world[0] / world[3]), static_cast<float>(world[1] / world[3]),
				static_cast<float>(world[2] / world[3])};
		}
	return corners;
}

// BaseHeightMapRenderObjClass::getMaximumVisibleBox: the box around the frustum's near corners and its far corners,
// each far corner pulled back along its edge to where the edge meets the plane at the terrain's lowest height
// (PlaneClass::Compute_Intersection: only where it crosses within the edge).
inline VisibleBox MaximumVisibleBox(const std::array<std::array<float, 3>, 8> &corners, float lowest)
{
	std::array<std::array<float, 3>, 8> clipped = corners;
	for (std::size_t edge = 0; edge < 4; ++edge)
	{
		const auto &from = corners[edge], &to = corners[edge + 4];
		const float across = to[2] - from[2];
		if (across == 0.0f)
			continue;
		const float fraction = (lowest - from[2]) / across;
		if (fraction < 0.0f || fraction > 1.0f)
			continue;
		for (std::size_t axis = 0; axis < 3; ++axis)
			clipped[edge + 4][axis] = from[axis] + (to[axis] - from[axis]) * fraction;
	}
	std::array<float, 3> low = clipped[0], high = clipped[0];
	for (const auto &corner : clipped)
		for (std::size_t axis = 0; axis < 3; ++axis)
		{
			low[axis] = (std::min)(low[axis], corner[axis]);
			high[axis] = (std::max)(high[axis], corner[axis]);
		}
	VisibleBox box;
	for (std::size_t axis = 0; axis < 3; ++axis)
	{
		box.centre[axis] = (low[axis] + high[axis]) * 0.5f;
		box.extent[axis] = (high[axis] - low[axis]) * 0.5f;
	}
	return box;
}

// doParticles' cull: a particle is drawn (and counted) unless it lies further from the box's centre than its extent
// plus the particle's size along some axis.
inline bool InVisibleBox(const VisibleBox &box, const std::array<float, 3> &at, float size)
{
	for (std::size_t axis = 0; axis < 3; ++axis)
		if (std::fabs(at[axis] - box.centre[axis]) > box.extent[axis] + size)
			return false;
	return true;
}

// doParticles for a STREAK system: its particles that pass the cull (the visible box, when there is one), oldest first,
// grouped by system, as (system, particle); the streak line runs through them in that order, so a culled particle is
// skipped and its neighbours joined.
inline std::vector<std::pair<std::uint32_t, std::uint32_t>> StreakParticles(const engine::effects::ParticleWorld &particles,
	const std::optional<VisibleBox> &box)
{
	std::vector<std::pair<std::uint32_t, std::uint32_t>> streak;
	const auto systems = particles.Systems();
	for (std::size_t index = 0; index < particles.ParticleCount(); ++index)
	{
		if (DrawingOf(particles.DefinitionOf(index)) != ParticleDrawing::Streak)
			continue;
		if (box && !InVisibleBox(*box, {particles.X()[index], particles.Y()[index], particles.Z()[index]}, particles.Size()[index]))
			continue;
		streak.emplace_back(systems[index], static_cast<std::uint32_t>(index));
	}
	std::stable_sort(streak.begin(), streak.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	return streak;
}
}
