module;

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <limits>

export module Engine.Core.Math.RandomVector3Generator;

import Engine.Core.Math.RandomStream;
import Engine.Core.Math.Vector3;

export namespace Engine::Math
{
enum class Vector3Distribution : std::uint8_t
{
	Box,
	SolidSphere,
	SphereSurface,
	Cylinder
};

// Generates points in origin-centered 3D shapes. Dimensions are half-extents
// for boxes, radius for spheres, and half-height/radius for cylinders.
class RandomVector3Generator final
{
public:
	RandomVector3Generator() noexcept
		: RandomVector3Generator(Vector3Distribution::Box, {}, 0) {}

	RandomVector3Generator(Vector3Distribution distribution, Vector3 dimensions,
		std::uint64_t seed) noexcept
		: distribution_(distribution), dimensions_(Clamp_Dimensions(distribution, dimensions)), stream_(seed) {}

	void Scale(float factor) noexcept
	{
		if (!(factor >= 0.0f) || !std::isfinite(factor)) factor = 0.0f;
		const float limit = (std::numeric_limits<float>::max)();
		const auto scaled = [factor, limit](float value) {
			const float result = value * factor;
			return std::isfinite(result) ? result : limit;
		};
		dimensions_ = {scaled(dimensions_.x), scaled(dimensions_.y), scaled(dimensions_.z)};
	}

	Vector3Distribution Distribution() const noexcept { return distribution_; }
	Vector3 Dimensions() const noexcept { return dimensions_; }
	void Set_Shape(Vector3Distribution distribution, Vector3 dimensions) noexcept
	{
		distribution_ = distribution;
		dimensions_ = Clamp_Dimensions(distribution, dimensions);
	}

	float Maximum_Extent() const noexcept
	{
		if (distribution_ == Vector3Distribution::Box)
			return (std::max)((std::max)(dimensions_.x, dimensions_.y), dimensions_.z);
		if (distribution_ == Vector3Distribution::Cylinder)
			return (std::max)(dimensions_.x, dimensions_.y);
		return dimensions_.x;
	}

	Vector3 Next() noexcept
	{
		switch (distribution_) {
		case Vector3Distribution::Box:
			return {Sample_Range(dimensions_.x), Sample_Range(dimensions_.y), Sample_Range(dimensions_.z)};
		case Vector3Distribution::SolidSphere:
			return Sample_Solid_Sphere();
		case Vector3Distribution::SphereSurface:
			return Sample_Sphere_Surface();
		case Vector3Distribution::Cylinder:
			return Sample_Cylinder();
		}
		return {};
	}

private:
	static Vector3 Clamp_Dimensions(Vector3Distribution distribution, Vector3 dimensions) noexcept
	{
		const auto non_negative = [](float value) {
			return value >= 0.0f && std::isfinite(value) ? value : 0.0f;
		};
		dimensions = {non_negative(dimensions.x), non_negative(dimensions.y), non_negative(dimensions.z)};
		if (distribution == Vector3Distribution::SolidSphere || distribution == Vector3Distribution::SphereSurface)
			dimensions.y = dimensions.z = dimensions.x;
		else if (distribution == Vector3Distribution::Cylinder)
			dimensions.z = dimensions.y;
		return dimensions;
	}

	float Sample_Range(float extent) noexcept { return stream_.NextFloat(-extent, extent); }

	Vector3 Sample_Solid_Sphere() noexcept
	{
		for (unsigned attempt = 0; attempt < 128; ++attempt) {
			const Vector3 unit_point{Sample_Range(1.0f), Sample_Range(1.0f), Sample_Range(1.0f)};
			const double length_squared = static_cast<double>(unit_point.x) * unit_point.x
				+ static_cast<double>(unit_point.y) * unit_point.y
				+ static_cast<double>(unit_point.z) * unit_point.z;
			if (length_squared <= 1.0)
				return unit_point * dimensions_.x;
		}
		return {};
	}

	Vector3 Sample_Sphere_Surface() noexcept
	{
		for (unsigned attempt = 0; attempt < 128; ++attempt) {
			const Vector3 point{Sample_Range(1.0f), Sample_Range(1.0f), Sample_Range(1.0f)};
			const double length_squared = static_cast<double>(point.x) * point.x
				+ static_cast<double>(point.y) * point.y
				+ static_cast<double>(point.z) * point.z;
			if (length_squared > 0.0f && length_squared <= 1.0f)
				return {static_cast<float>((point.x / std::sqrt(length_squared)) * dimensions_.x),
					static_cast<float>((point.y / std::sqrt(length_squared)) * dimensions_.x),
					static_cast<float>((point.z / std::sqrt(length_squared)) * dimensions_.x)};
		}
		return {};
	}

	Vector3 Sample_Cylinder() noexcept
	{
		for (unsigned attempt = 0; attempt < 128; ++attempt) {
			const float unit_y = Sample_Range(1.0f);
			const float unit_z = Sample_Range(1.0f);
			const double radial_length_squared = static_cast<double>(unit_y) * unit_y
				+ static_cast<double>(unit_z) * unit_z;
			if (radial_length_squared <= 1.0)
				return {Sample_Range(dimensions_.x), unit_y * dimensions_.y, unit_z * dimensions_.y};
		}
		return {};
	}

	Vector3Distribution distribution_;
	Vector3 dimensions_;
	RandomStream stream_;
};
}
