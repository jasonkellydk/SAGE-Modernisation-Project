export module games.generalszh.presentation.objects.algorithms.laser_beams;
import std;

export import games.generalszh.presentation.effects.laser_looks;
export import games.generalszh.presentation.objects.resources.presentation_resources;

// W3DLaserDraw::doDrawModule: a laser object's beams from `start` to `end` for this frame, NumBeams from the inner to
// the outer width and colour (the original's colour ramp scaled by the inner alpha), each width scaled by its
// LaserUpdate's width scale (LaserRadiusUpdate), over its segments (arched by ArcHeight on a cosine, never below 2 over
// the ground), their texture tiled by length over width and scrolled by its age.
export namespace generalszh::presentation
{
inline void AppendLaserBeams(LaserFrame &out, const content::LaserLook &l, const std::array<float, 3> &start, const std::array<float, 3> &end,
	float widthScale, float age, const std::function<float(float, float)> &ground)
{
	const std::uint32_t segments = std::max<std::uint32_t>(l.segments, 1);
	for (std::uint32_t segment = 0; segment < segments; ++segment)
	{
		std::array<float, 3> a = start, b = end;
		if (l.arcHeight > 0.0f && segments > 1)
		{
			const std::array<float, 3> line{end[0] - start[0], end[1] - start[1], end[2] - start[2]};
			const float length = std::sqrt(line[0] * line[0] + line[1] * line[1] + line[2] * line[2]);
			const float half = std::max(length * 0.5f, 0.001f);
			float from = static_cast<float>(segment) / static_cast<float>(segments), to = (segment + 1.0f) / static_cast<float>(segments);
			if (segment > 0)
				from -= l.segmentOverlap;
			if (segment + 1 < segments)
				to += l.segmentOverlap;
			const auto point = [&](float t) {
				std::array<float, 3> p{start[0] + line[0] * t, start[1] + line[1] * t, start[2] + line[2] * t};
				const float fromMiddle = std::abs(t - 0.5f) * length;
				p[2] += std::cos(fromMiddle / half * 1.5707963f) * l.arcHeight;
				p[2] = std::max(p[2], 2.0f + (ground ? ground(p[0], p[1]) : 0.0f));
				return p;
			};
			a = point(from);
			b = point(to);
		}
		const float length = std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));
		for (int beam = static_cast<int>(l.beams) - 1; beam >= 0; --beam)
		{
			float width, alpha;
			std::array<float, 4> color;
			if (l.beams == 1)
			{
				width = l.innerWidth * widthScale;
				alpha = l.innerColor[3];
				color = {l.innerColor[0] * alpha, l.innerColor[1] * alpha, l.innerColor[2] * alpha, 1.0f};
			}
			else
			{
				const float scale = static_cast<float>(beam) / (static_cast<float>(l.beams) - 1.0f);
				width = (l.innerWidth + scale * (l.outerWidth - l.innerWidth)) * widthScale;
				alpha = l.innerColor[3] + scale * (l.outerColor[3] - l.innerColor[3]);
				const float inner = l.innerColor[3];
				color = {l.innerColor[0] + scale * (l.outerColor[0] - l.innerColor[0]) * inner, l.innerColor[1] + scale * (l.outerColor[1] - l.innerColor[1]) * inner,
					l.innerColor[2] + scale * (l.outerColor[2] - l.innerColor[2]) * inner, 1.0f};
			}
			if (!(width > 0.0f && alpha > 0.0f))
				continue;
			const float tiles = l.tile ? std::abs(length / width * l.tilingScalar) : 1.0f;
			out.beams.push_back({a, b, width, color, l.texture, tiles, age * l.scrollRate / 1000.0f});
		}
	}
}

// LaserRadiusUpdate::updateRadius as a drawn frame sees it (the logic frame `now`): decaying, 1 less the part of the
// decay gone (not below 0); widening, the part of the widening done (not above 1); else whole.
inline float LaserWidthScale(bool widening, std::uint64_t widenStart, std::uint64_t widenFinish, bool decaying, std::uint64_t decayStart,
	std::uint64_t decayFinish, std::uint64_t now) noexcept
{
	const auto part = [now](std::uint64_t from, std::uint64_t to) {
		return (static_cast<float>(now) - static_cast<float>(from)) / (static_cast<float>(to) - static_cast<float>(from));
	};
	if (decaying)
		return std::max(0.0f, 1.0f - part(decayStart, decayFinish));
	if (widening)
		return std::min(1.0f, part(widenStart, widenFinish));
	return 1.0f;
}
}
