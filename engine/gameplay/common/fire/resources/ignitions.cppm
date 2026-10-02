export module engine.gameplay.common.fire.resources.ignitions;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// Fire spreading this tick: what each burning thing sets alight (per chunk),
// gathered by target for the flammability pass (the first source wins); and
// each spread try, where embers fly (for the game to create them).
export namespace engine::gameplay
{
struct Ignition
{
	ecs::Entity target;
	ecs::Entity source;
};

struct IgnitionOffers : ecs::ChunkOutputs<Ignition>
{
};

class Ignitions
{
public:
	void Gather(const IgnitionOffers &offers)
	{
		m_ignitions.clear();
		offers.ForEach([&](const Ignition &ignition) { m_ignitions.push_back(ignition); });
		std::stable_sort(m_ignitions.begin(), m_ignitions.end(), [](const Ignition &a, const Ignition &b) { return Less(a.target, b.target); });
		m_ignitions.erase(std::unique(m_ignitions.begin(), m_ignitions.end(), [](const Ignition &a, const Ignition &b) { return a.target == b.target; }),
			m_ignitions.end());
	}

	// Who sets the target alight this tick, if anyone.
	std::optional<ecs::Entity> For(ecs::Entity target) const
	{
		const auto found = std::lower_bound(m_ignitions.begin(), m_ignitions.end(), target,
			[](const Ignition &ignition, ecs::Entity entity) { return Less(ignition.target, entity); });
		if (found != m_ignitions.end() && found->target == target)
			return found->source;
		return std::nullopt;
	}

	std::size_t Count() const noexcept { return m_ignitions.size(); }

private:
	static bool Less(ecs::Entity a, ecs::Entity b) noexcept { return a.index != b.index ? a.index < b.index : a.generation < b.generation; }

	std::vector<Ignition> m_ignitions;
};

struct SpreadTry
{
	ecs::Entity entity;
	Engine::Math::FixedVector3 position;
};

struct SpreadTries : ecs::ChunkOutputs<SpreadTry>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::IgnitionOffers>
{
	static constexpr std::string_view StableName = "engine.gameplay.ignition_offers";
};
template<>
struct ResourceTraits<engine::gameplay::Ignitions>
{
	static constexpr std::string_view StableName = "engine.gameplay.ignitions";
};
template<>
struct ResourceTraits<engine::gameplay::SpreadTries>
{
	static constexpr std::string_view StableName = "engine.gameplay.spread_tries";
};
}
