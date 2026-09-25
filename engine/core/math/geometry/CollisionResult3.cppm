module;

#include <cstdint>

export module Engine.Core.Math.CollisionResult3;

export import Engine.Core.Math.Vector3;

export namespace Engine::Math
{
// Best contact found by a segment or swept-volume query. The caller owns the
// result and may carry an existing maximum fraction into the next query.
struct CollisionResult3 final
{
	bool starts_overlapping = false;
	float fraction = 1.0f;
	Vector3 normal{};
	std::uint32_t surface_type = 0;
	bool compute_contact_point = false;
	Vector3 contact_point{};

	constexpr void Reset() noexcept
	{
		starts_overlapping = false;
		fraction = 1.0f;
		normal = {};
		surface_type = 0;
		compute_contact_point = false;
		contact_point = {};
	}
};
}
