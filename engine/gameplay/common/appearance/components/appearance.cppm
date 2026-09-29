export module engine.gameplay.common.appearance.components.appearance;
import std;

import engine.ecs.system.system;

// How an entity currently looks, as condition bits the presentation picks
// models, animations and effects by (moving, firing, damaged, ...). The
// game names the bits and a game system sets them each tick from the rest
// of the state; the simulation never reads them back.
export namespace engine::gameplay
{
struct Appearance
{
	static constexpr std::uint32_t Bits = 128;
	std::array<std::uint64_t, 2> flags{};

	bool Test(std::uint32_t bit) const noexcept { return bit < Bits && (flags[bit / 64] >> (bit % 64) & 1u) != 0; }

	void Set(std::uint32_t bit, bool on = true) noexcept
	{
		if (bit >= Bits)
			return;
		const std::uint64_t mask = std::uint64_t{1} << (bit % 64);
		flags[bit / 64] = on ? (flags[bit / 64] | mask) : (flags[bit / 64] & ~mask);
	}

	bool operator==(const Appearance &) const = default;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Appearance>
{
	static constexpr std::string_view StableName = "engine.gameplay.appearance";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
