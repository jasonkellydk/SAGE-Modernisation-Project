module;

#include <array>
#include <cmath>

export module Assets.Math;

namespace Assets
{

export struct Vector2f final
{
	float x = 0.0f;
	float y = 0.0f;
};

export struct Vector3f final
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

export struct Color4f final
{
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
	float a = 1.0f;
};

export struct Bounds3f final
{
	Vector3f minimum{};
	Vector3f maximum{};

	bool Is_Valid() const noexcept;
};

}

namespace Assets
{

bool Bounds3f::Is_Valid() const noexcept
{
	return std::isfinite(minimum.x) && std::isfinite(minimum.y) && std::isfinite(minimum.z) &&
		std::isfinite(maximum.x) && std::isfinite(maximum.y) && std::isfinite(maximum.z) &&
		minimum.x <= maximum.x && minimum.y <= maximum.y && minimum.z <= maximum.z;
}

}
