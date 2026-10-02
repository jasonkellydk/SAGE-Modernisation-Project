export module engine.gameplay.common.healing.resources.heal_pulses;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// Area healing this tick: what each healer offers whom (per chunk), then
// the same pulses gathered and ordered by target for the healing pass. The
// pulses that took are also what the presentation shows (heal sparkles).
export namespace engine::gameplay
{
struct HealPulse
{
	ecs::Entity target;
	ecs::Entity healer;
	Engine::Math::Fixed amount;
	std::uint64_t lockTicks{0}; // how long the target then accepts only this healer
	Engine::Math::FixedVector3 position;
};

struct HealOffers : ecs::ChunkOutputs<HealPulse>
{
};

class HealPulses
{
public:
	// From the offers, in their deterministic order, then by target (stable).
	void Gather(const HealOffers &offers)
	{
		m_pulses.clear();
		offers.ForEach([&](const HealPulse &pulse) { m_pulses.push_back(pulse); });
		std::stable_sort(m_pulses.begin(), m_pulses.end(), [](const HealPulse &a, const HealPulse &b) {
			return a.target.index != b.target.index ? a.target.index < b.target.index : a.target.generation < b.target.generation;
		});
	}

	// One more this tick (a builder repairing), kept in target order after those already there.
	void Add(const HealPulse &pulse)
	{
		const auto at = std::upper_bound(m_pulses.begin(), m_pulses.end(), pulse, [](const HealPulse &a, const HealPulse &b) {
			return a.target.index != b.target.index ? a.target.index < b.target.index : a.target.generation < b.target.generation;
		});
		m_pulses.insert(at, pulse);
	}

	std::span<const HealPulse> For(ecs::Entity target) const
	{
		const auto first = std::lower_bound(m_pulses.begin(), m_pulses.end(), target, [](const HealPulse &pulse, ecs::Entity entity) {
			return pulse.target.index != entity.index ? pulse.target.index < entity.index : pulse.target.generation < entity.generation;
		});
		auto last = first;
		while (last != m_pulses.end() && last->target == target)
			++last;
		return {first, last};
	}

	std::span<const HealPulse> All() const noexcept { return m_pulses; }

private:
	std::vector<HealPulse> m_pulses;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::HealOffers>
{
	static constexpr std::string_view StableName = "engine.gameplay.heal_offers";
};
template<>
struct ResourceTraits<engine::gameplay::HealPulses>
{
	static constexpr std::string_view StableName = "engine.gameplay.heal_pulses";
};
}
