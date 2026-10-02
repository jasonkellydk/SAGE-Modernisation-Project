export module games.generalszh.presentation.effects.tracers;
import std;

import engine.ecs.system.system;

// Tracers (an FX list's Tracer: TracerFXNugget making a GenericTracer drawable, drawn by W3DTracerDraw), on frame time:
// a box `length` long along where it heads and `width` across, of its colour, starting where the FX played, heading
// for the FX's second point, moving `speed` a second along it; it fades linearly (W3DTracerDraw::doDrawModule: its
// opacity less its opacity over the frames left, each frame) and is gone at its expiry (its drawable's expiration
// date). Kept as columns, one row per tracer.
export namespace generalszh::presentation
{
struct Tracers
{
	std::vector<std::array<float, 3>> positions;
	std::vector<std::array<float, 3>> directions; // unit
	std::vector<float> speeds;                    // a second
	std::vector<float> lengths;
	std::vector<float> widths;
	std::vector<std::array<float, 3>> colors;
	std::vector<float> ages;  // seconds
	std::vector<float> lives; // seconds

	std::size_t Size() const noexcept { return positions.size(); }

	void Add(const std::array<float, 3> &at, const std::array<float, 3> &direction, float speed, float length, float width,
		const std::array<float, 3> &color, float life)
	{
		positions.push_back(at);
		directions.push_back(direction);
		speeds.push_back(speed);
		lengths.push_back(length);
		widths.push_back(width);
		colors.push_back(color);
		ages.push_back(0.0f);
		lives.push_back(life);
	}

	// Its opacity now (1 new, 0 at its expiry).
	float Opacity(std::size_t row) const noexcept { return lives[row] > 0.0f ? std::clamp(1.0f - ages[row] / lives[row], 0.0f, 1.0f) : 0.0f; }

	// `seconds` on: each moves along where it heads and ages; the expired go (the last row moved into their place).
	void Advance(float seconds)
	{
		for (std::size_t row = 0; row < Size();)
		{
			ages[row] += seconds;
			if (ages[row] >= lives[row])
			{
				Remove(row);
				continue;
			}
			for (std::size_t axis = 0; axis < 3; ++axis)
				positions[row][axis] += directions[row][axis] * speeds[row] * seconds;
			++row;
		}
	}

private:
	void Remove(std::size_t row)
	{
		const std::size_t last = Size() - 1;
		positions[row] = positions[last];
		directions[row] = directions[last];
		speeds[row] = speeds[last];
		lengths[row] = lengths[last];
		widths[row] = widths[last];
		colors[row] = colors[last];
		ages[row] = ages[last];
		lives[row] = lives[last];
		positions.pop_back();
		directions.pop_back();
		speeds.pop_back();
		lengths.pop_back();
		widths.pop_back();
		colors.pop_back();
		ages.pop_back();
		lives.pop_back();
	}
};

// TracerFXNugget::doFXPos from `primary` toward `secondary` (a speed of 0: the FX's caller's, a frame's travel), its
// life the frames it takes to cover the distance less its length (1 when that is negative or it has no speed) times
// DecayAt, rounded up.
inline void MakeTracer(Tracers &tracers, const std::array<float, 3> &primary, const std::array<float, 3> &secondary, float nuggetSpeedPerFrame,
	float callerSpeedPerFrame, float decayAt, float length, float width, const std::array<float, 3> &color)
{
	std::array<float, 3> toward{secondary[0] - primary[0], secondary[1] - primary[1], secondary[2] - primary[2]};
	const float distance = std::sqrt(toward[0] * toward[0] + toward[1] * toward[1] + toward[2] * toward[2]);
	if (distance > 0.0f)
		for (float &axis : toward)
			axis /= distance;
	else
		toward = {1.0f, 0.0f, 0.0f};
	const float speed = nuggetSpeedPerFrame == 0.0f ? callerSpeedPerFrame : nuggetSpeedPerFrame;
	const float travel = distance - length;
	const float frames = travel >= 0.0f && speed > 0.0f ? travel / speed : 1.0f;
	const float lifeFrames = std::ceil(frames * decayAt);
	tracers.Add(primary, toward, speed * 30.0f, length, width, color, lifeFrames / 30.0f);
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::Tracers>
{
	static constexpr std::string_view StableName = "generalszh.presentation.tracers";
};
}
