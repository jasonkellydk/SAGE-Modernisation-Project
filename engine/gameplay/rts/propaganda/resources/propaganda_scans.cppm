export module engine.gameplay.rts.propaganda.resources.propaganda_scans;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.healing.resources.heal_pulses;
import engine.ecs.system.system;

// This tick's propaganda scans (PropagandaTowerBehavior::doScan): which towers scanned, whom each took in (sorted by
// the one taken in, then tower), and the pulse effects they played (for presentation). Remade every tick.
export namespace engine::gameplay
{
struct PropagandaPulse
{
	ecs::Entity tower;
	std::uint32_t effect{0};
	Engine::Math::FixedVector3 position;
};

struct PropagandaScans
{
	std::vector<ecs::Entity> scanned;                          // towers, sorted
	std::vector<std::pair<std::uint64_t, ecs::Entity>> takenIn; // (unit key, tower), sorted
	std::vector<PropagandaPulse> pulses;

	static std::uint64_t Key(ecs::Entity entity) noexcept { return (static_cast<std::uint64_t>(entity.index) << 32) | entity.generation; }

	void Clear() noexcept
	{
		scanned.clear();
		takenIn.clear();
		pulses.clear();
	}

	bool Scanned(ecs::Entity tower) const noexcept
	{
		return std::binary_search(scanned.begin(), scanned.end(), tower, [](ecs::Entity a, ecs::Entity b) { return Key(a) < Key(b); });
	}

	// The towers that took `unit` in this tick (by tower, in order).
	std::span<const std::pair<std::uint64_t, ecs::Entity>> TakenIn(ecs::Entity unit) const noexcept
	{
		const std::uint64_t key = Key(unit);
		const auto first = std::lower_bound(takenIn.begin(), takenIn.end(), key, [](const auto &entry, std::uint64_t value) { return entry.first < value; });
		auto last = first;
		while (last != takenIn.end() && last->first == key)
			++last;
		return {first, last};
	}
};

// The heals the towers give this tick, per chunk, before they join the tick's heal pulses.
struct PropagandaHeals : ecs::ChunkOutputs<HealPulse>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::PropagandaScans>
{
	static constexpr std::string_view StableName = "engine.gameplay.propaganda_scans";
};
template<>
struct ResourceTraits<engine::gameplay::PropagandaHeals>
{
	static constexpr std::string_view StableName = "engine.gameplay.propaganda_heals";
};
}
