module;

#include <array>
#include <cstddef>
#include <cstdint>

export module Engine.Core.Math.Index3;

export namespace Engine::Math
{
template <class Component>
struct Index3 final
{
	std::array<Component, 3> values{};

	constexpr Component &operator[](std::size_t index) noexcept { return values[index]; }
	constexpr const Component &operator[](std::size_t index) const noexcept { return values[index]; }
	friend constexpr bool operator==(const Index3 &left, const Index3 &right) noexcept
	{
		return left.values[0] == right.values[0]
			&& left.values[1] == right.values[1]
			&& left.values[2] == right.values[2];
	}
};

using Index3i = Index3<std::int32_t>;
using Index3u16 = Index3<std::uint16_t>;

static_assert(sizeof(Index3i) == 3 * sizeof(std::int32_t));
static_assert(sizeof(Index3u16) == 3 * sizeof(std::uint16_t));
}
