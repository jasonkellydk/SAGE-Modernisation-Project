export module engine.audio.mixing.spatial;
import std;

// How loud a world sound is where the listener stands, and from which side:
// full volume inside its minimum range, falling off with the inverse of the
// distance beyond it (as the original's 3D provider), and silent past its
// maximum range; panned by the direction to it across the listener's right
// vector with an equal-power law.
export namespace engine::audio
{
struct Vec3
{
	float x{0.0f};
	float y{0.0f};
	float z{0.0f};
};

struct Listener
{
	Vec3 position;
	// Unit vector to the listener's right (the camera's right, on the ground plane).
	Vec3 right{1.0f, 0.0f, 0.0f};
};

struct StereoGain
{
	float left{0.0f};
	float right{0.0f};
};

float DistanceGain(float distance, float minRange, float maxRange) noexcept
{
	if (distance > maxRange)
		return 0.0f;
	if (distance <= minRange || distance <= 0.0f)
		return 1.0f;
	return minRange > 0.0f ? std::clamp(minRange / distance, 0.0f, 1.0f) : 1.0f - distance / maxRange;
}

StereoGain Pan(float pan) noexcept
{
	constexpr float quarterPi = 0.78539816339f;
	const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * quarterPi;
	return {std::cos(angle), std::sin(angle)};
}

StereoGain Spatialize(const Listener &listener, Vec3 emitter, float minRange, float maxRange) noexcept
{
	const float dx = emitter.x - listener.position.x;
	const float dy = emitter.y - listener.position.y;
	const float dz = emitter.z - listener.position.z;
	const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
	const float gain = DistanceGain(distance, minRange, maxRange);
	if (gain <= 0.0f)
		return {};
	// Close sounds are centred; the pan grows with how far to the side it is.
	const float flat = std::sqrt(dx * dx + dy * dy);
	const float side = flat > 1.0f ? (dx * listener.right.x + dy * listener.right.y) / flat : 0.0f;
	const StereoGain pan = Pan(side * std::min(1.0f, flat / std::max(minRange, 1.0f)));
	// Equal power keeps the centre at ~0.707 per side: normalize it to 1.
	constexpr float centre = 1.41421356f;
	return {std::min(1.0f, pan.left * centre) * gain, std::min(1.0f, pan.right * centre) * gain};
}
}
