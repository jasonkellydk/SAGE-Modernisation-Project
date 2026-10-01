export module games.generalszh.presentation.camera.algorithms.camera_shaking;
import std;

export import games.generalszh.presentation.camera.resources.camera_shakers;

// CameraShakeSystemClass (W3DDevice CameraShakeSystem.cpp), as free functions over the CameraShakers resource: artists
// asked for sinusoidal motion, rotation rather than translation, and more pitch than yaw. W3DView::getCameraTransform
// steps the shakers by 1/30 of a second each time it builds the camera (every drawn frame while one shakes, 30 a
// second), then sums their rotations at the camera's position; the port steps them by the frame's real seconds.
export namespace generalszh::presentation
{
namespace camera_shaking
{
inline constexpr float Pi = std::numbers::pi_v<float>;
inline constexpr float MinOmega = (12.5f * 360.0f) * Pi / 180.0f;
inline constexpr float MaxOmega = (15.0f * 360.0f) * Pi / 180.0f;
inline constexpr float EndOmega = 360.0f * Pi / 180.0f;
inline constexpr float MinPhi = 0.0f;
inline constexpr float MaxPhi = 360.0f * Pi / 180.0f;
inline constexpr std::array<float, 3> AxisRotation{7.5f * Pi / 180.0f, 15.0f * Pi / 180.0f, 5.0f * Pi / 180.0f};

template<typename Random>
float Uniform(Random &random, float low, float high)
{
	return std::uniform_real_distribution<float>(low, high)(random);
}
}

// CameraShakeSystemClass::Add_Camera_Shake(position, radius, duration, power): power is degrees of amplitude; each axis
// gets a random starting speed (12.5 to 15 turns a second) and phase.
template<typename Random>
void AddCameraShake(CameraShakers &shakers, std::array<float, 3> position, float radius, float durationSeconds, float powerDegrees, Random &random)
{
	using namespace camera_shaking;
	shakers.positions.push_back(position);
	shakers.radii.push_back(radius);
	shakers.durations.push_back(durationSeconds);
	shakers.intensities.push_back(powerDegrees * Pi / 180.0f);
	shakers.elapsed.push_back(0.0f);
	std::array<float, 3> omega{};
	for (float &axis : omega)
		axis = Uniform(random, MinOmega, MaxOmega);
	std::array<float, 3> phase{};
	for (float &axis : phase)
		axis = Uniform(random, MinPhi, MaxPhi);
	shakers.omegas.push_back(omega);
	shakers.phases.push_back(phase);
}

// CameraShakeSystemClass::Timestep: every shaker `seconds` older; those whose time is up (elapsed >= duration) go.
inline void StepCameraShakers(CameraShakers &shakers, float seconds)
{
	std::size_t kept = 0;
	for (std::size_t index = 0; index < shakers.elapsed.size(); ++index)
	{
		const float elapsed = shakers.elapsed[index] + seconds;
		if (elapsed >= shakers.durations[index])
			continue;
		shakers.positions[kept] = shakers.positions[index];
		shakers.radii[kept] = shakers.radii[index];
		shakers.durations[kept] = shakers.durations[index];
		shakers.intensities[kept] = shakers.intensities[index];
		shakers.elapsed[kept] = elapsed;
		shakers.omegas[kept] = shakers.omegas[index];
		shakers.phases[kept] = shakers.phases[index];
		++kept;
	}
	shakers.positions.resize(kept);
	shakers.radii.resize(kept);
	shakers.durations.resize(kept);
	shakers.intensities.resize(kept);
	shakers.elapsed.resize(kept);
	shakers.omegas.resize(kept);
	shakers.phases.resize(kept);
}

inline bool CameraShaking(const CameraShakers &shakers) noexcept { return !shakers.elapsed.empty(); }

// CameraShakeSystemClass::Update_Camera_Shaker with CameraShakerClass::Compute_Rotations: at the camera's position,
// each shaker within its radius adds, per axis, axis amplitude x intensity x sin(omega(t) t + phi), where intensity is
// its power x (1 - distance / radius) x (1 - elapsed / duration) and omega(t) slides from its start toward one turn a
// second; and, per axis (three times per shaker, as the original's loop does), a random fudge of up to half the
// intensity on all three axes.
template<typename Random>
std::array<float, 3> ShakerAngles(const CameraShakers &shakers, std::array<float, 3> camera, Random &random)
{
	using namespace camera_shaking;
	std::array<float, 3> angles{};
	for (std::size_t index = 0; index < shakers.elapsed.size(); ++index)
	{
		const std::array<float, 3> &at = shakers.positions[index];
		const float dx = camera[0] - at[0], dy = camera[1] - at[1], dz = camera[2] - at[2];
		const float length2 = dx * dx + dy * dy + dz * dz;
		const float radius = shakers.radii[index];
		if (length2 > radius * radius)
			continue;
		const float elapsed = shakers.elapsed[index];
		const float intensity = shakers.intensities[index] * (1.0f - std::sqrt(length2) / radius) * (1.0f - elapsed / shakers.durations[index]);
		for (std::size_t axis = 0; axis < 3; ++axis)
		{
			const float omega = shakers.omegas[index][axis] + (EndOmega - shakers.omegas[index][axis]) * elapsed;
			angles[axis] += AxisRotation[axis] * intensity * std::sin(omega * elapsed + shakers.phases[index][axis]);
			const float minor = intensity * 0.5f;
			for (float &angle : angles)
				angle += Uniform(random, -minor, minor);
		}
	}
	return angles;
}
}
