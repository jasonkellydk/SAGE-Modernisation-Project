export module games.generalszh.presentation.effects.heat_haze;
import std;

import engine.effects.particles.simulation.particle_world;

// Heat haze: the screen smudges W3DParticleSystemManager::doParticles makes of particles. A system whose texture's
// name starts with "SMUD" (the original's "temporary hack": its first four bytes, case and all; the INI's Type =
// SMUDGE is never consulted, and the retail templates are typed PARTICLE) is a smudge system: its particles are never
// drawn as particles, and while UseHeatEffects is on each becomes a smudge this frame: at the particle, its size and
// alpha, with a fresh offset into the background (GameClientRandomValueReal: -0.06..0.06 across, then -0.03..0.03 up)
// that W3DSmudgeManager::render pulls the smudge's centre by. Drawables never smudge.
export namespace generalszh::presentation
{
// This frame's smudges, column by column (Smudge's m_pos, m_offset, m_size, m_opacity).
struct HeatHaze
{
	std::vector<float> x, y, z;
	std::vector<float> offsetX, offsetY;
	std::vector<float> size;    // the particle's size (the smudge spans half of it either side of the particle)
	std::vector<float> opacity; // the particle's alpha (the smudge's centre's)

	std::size_t Size() const noexcept { return x.size(); }
	void Clear() noexcept
	{
		for (auto *column : {&x, &y, &z, &offsetX, &offsetY, &size, &opacity})
			column->clear();
	}
};

// A smudge system's texture (the DWORD 0x44554D53 compare: "SMUD").
inline bool IsHeatHazeTexture(std::string_view texture) noexcept
{
	return texture.size() >= 4 && texture.substr(0, 4) == "SMUD";
}

inline bool IsHeatHaze(const engine::effects::ParticleSystemDefinition &definition) noexcept
{
	return definition.kind != engine::effects::ParticleKind::Drawable && IsHeatHazeTexture(definition.texture);
}

// The frame's smudges (none while heat effects are off): each smudge system's particle, moved `alpha` of its
// per-frame motion as the particles draw. `draw(low, high)` is the client's random real in [low, high].
template<class Draw>
void ExtractHeatHaze(const engine::effects::ParticleWorld &particles, float alpha, bool heatEffects, Draw &&draw, HeatHaze &haze)
{
	haze.Clear();
	if (!heatEffects)
		return;
	for (std::size_t index = 0; index < particles.ParticleCount(); ++index)
	{
		if (!IsHeatHaze(particles.DefinitionOf(index)))
			continue;
		haze.x.push_back(particles.X()[index] + particles.MotionX()[index] * alpha);
		haze.y.push_back(particles.Y()[index] + particles.MotionY()[index] * alpha);
		haze.z.push_back(particles.Z()[index] + particles.MotionZ()[index] * alpha);
		const float across = draw(-0.06f, 0.06f);
		haze.offsetX.push_back(across);
		haze.offsetY.push_back(draw(-0.03f, 0.03f));
		haze.size.push_back(particles.Size()[index]);
		haze.opacity.push_back(particles.Alpha()[index]);
	}
}
}
