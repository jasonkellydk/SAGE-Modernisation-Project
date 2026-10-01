export module games.generalszh.presentation.camera.resources.camera_shakers;
import std;

import engine.ecs.system.system;

// The camera shakers scripts add (the original's CameraShakeSystemClass, CAMERA_ADD_SHAKER_AT): each a sinusoidal roll of
// the camera about its three axes, from a point, fading with distance to its radius and over its time. Structure of
// arrays, oldest first (the original's list order). Presentation only: its randomness never touches the simulation.
export namespace generalszh::presentation
{
struct CameraShakers
{
	std::vector<std::array<float, 3>> positions;
	std::vector<float> radii;
	std::vector<float> durations;   // seconds
	std::vector<float> intensities; // radians of amplitude
	std::vector<float> elapsed;     // seconds
	std::vector<std::array<float, 3>> omegas; // each axis's starting angular speed (radians a second)
	std::vector<std::array<float, 3>> phases; // each axis's phase (radians)
	// This frame's summed rotation (radians about x, y and z), composed into the camera's transform.
	std::array<float, 3> angles{};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::CameraShakers>
{
	static constexpr std::string_view StableName = "generalszh.presentation.camera_shakers";
};
}
