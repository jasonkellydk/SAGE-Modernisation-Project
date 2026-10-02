export module games.generalszh.presentation.objects.algorithms.wave_sprays;
import std;

export import games.generalszh.gameplay.waveguide.algorithms.wave_shape;
import Engine.Core.Math.FixedPresentation;

// A flood wave's riding particle systems (WaveGuideUpdate, GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/
// WaveGuideUpdate.cpp):
//   WaveSprayLayout (initWaveGuide): for each front point in order, WaveSpray01 then WaveSpray02 at the point in the
//     wave's frame (setPosition, attachToObject), and WaveSpray03 at every fifth point (i % 5 == 0);
//   SprayHeight (doShapeEffects): a spray's local height becomes the ground's height under its point when that is below
//     PreferredHeight, else it stays as it was;
//   WavePlace (ParticleSystem::update for an attached system): the wave's transform (its position, turned by its yaw)
//     times the system's place in its frame.
export namespace generalszh::presentation
{
struct WaveSpray
{
	std::string_view system;
	std::array<float, 3> local{};
};

inline std::vector<WaveSpray> WaveSprayLayout(const gameplay::WaveShape &shape)
{
	std::vector<WaveSpray> sprays;
	for (std::uint32_t point = 0; point < shape.count; ++point)
	{
		const std::array<float, 3> local{Engine::Math::ToFloat(shape.points[point].x), Engine::Math::ToFloat(shape.points[point].y), 0.0f};
		sprays.push_back({"WaveSpray01", local});
		sprays.push_back({"WaveSpray02", local});
		if (point % 5 == 0)
			sprays.push_back({"WaveSpray03", local});
	}
	return sprays;
}

inline float SprayHeight(float local, float ground, float preferred) noexcept { return ground < preferred ? ground : local; }

inline std::array<float, 3> WavePlace(const std::array<float, 3> &at, float yaw, const std::array<float, 3> &local) noexcept
{
	const float c = std::cos(yaw), s = std::sin(yaw);
	return {at[0] + c * local[0] - s * local[1], at[1] + s * local[0] + c * local[1], at[2] + local[2]};
}
}
