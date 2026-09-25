module;

#include <algorithm>
#include <cmath>

export module Engine.Core.Math.QuaternionInterpolator;

import Engine.Core.Math.Quaternion;

export namespace Engine::Math
{
// Cached angle-weighted interpolation for repeatedly sampling one orientation
// pair. The historical sin(angle)/angle weights and input magnitudes are kept
// for particle-emission trajectories; callers that need unit quaternions should
// normalize before and after interpolation.
class QuaternionInterpolator final
{
public:
	QuaternionInterpolator(Quaternion first, Quaternion second) noexcept
		: first_(first), second_(second)
	{
		float cosine = first.Dot(second);
		flip_ = cosine < 0.0f;
		if (flip_) cosine = -cosine;
		cosine = (std::clamp)(cosine, 0.0f, 1.0f);
		linear_ = 1.0f - cosine < 0.001f;
		if (!linear_) angle_ = std::acos(cosine);
	}

	Quaternion Sample(float fraction) const noexcept
	{
		float first_weight;
		float second_weight;
		if (linear_) {
			first_weight = 1.0f - fraction;
			second_weight = fraction;
		} else {
			const float inverse_angle = 1.0f / angle_;
			first_weight = std::sin(angle_ - fraction * angle_) * inverse_angle;
			second_weight = std::sin(fraction * angle_) * inverse_angle;
		}
		if (flip_) second_weight = -second_weight;
		return {
			first_weight * first_.x + second_weight * second_.x,
			first_weight * first_.y + second_weight * second_.y,
			first_weight * first_.z + second_weight * second_.z,
			first_weight * first_.w + second_weight * second_.w};
	}

private:
	Quaternion first_;
	Quaternion second_;
	float angle_ = 0.0f;
	bool flip_ = false;
	bool linear_ = true;
};
}
