export module engine.gameplay.common.random.resources.random_seed;
import std;

import engine.ecs.system.system;

// The match's random seed, the same on every peer. Systems derive their
// deterministic streams from it (salted per system, keyed by tick and
// entity), so no system holds random state.
export namespace engine::gameplay
{
struct RandomSeed
{
	std::uint64_t value{0};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::RandomSeed>
{
	static constexpr std::string_view StableName = "engine.gameplay.random_seed";
};
}
