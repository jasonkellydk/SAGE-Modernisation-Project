export module games.generalszh.presentation.camera.algorithms.camera_slave;
import std;

// CAMERA_ENABLE_SLAVE_MODE: W3DView::getCameraTransform takes the named unit's first model draw's
// getRenderObjectBoneTransform (the bone's transform in the world: its object's transform times the bone's in its
// model) as the whole camera transform, and moves the view's position to where the bone is (View::setPosition2D).
export namespace generalszh::presentation
{
// `world`: the object's transform (row-major 4x4); `bone`: the bone's in its model (row-major 3x4). The product,
// row-major 3x4.
inline std::array<float, 12> BoneInWorld(const std::array<float, 16> &world, const std::array<float, 12> &bone) noexcept
{
	std::array<float, 12> result{};
	for (std::size_t row = 0; row < 3; ++row)
		for (std::size_t column = 0; column < 4; ++column)
		{
			float value = column == 3 ? world[row * 4 + 3] : 0.0f;
			for (std::size_t k = 0; k < 3; ++k)
				value += world[row * 4 + k] * bone[k * 4 + column];
			result[row * 4 + column] = value;
		}
	return result;
}

// View::setPosition2D: the bone's x and y.
inline std::array<float, 2> SlavePosition(const std::array<float, 12> &boneInWorld) noexcept { return {boneInWorld[3], boneInWorld[7]}; }
}
