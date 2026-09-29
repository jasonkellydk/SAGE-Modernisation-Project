export module engine.gameplay.rts.combat.resources.attack_priorities;
import std;

export import engine.core.serialization.byte_stream;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// The scripts' attack priority sets (the original's AttackPriorityInfo, ScriptEngine::findAttackInfo): each a name, a
// default priority (1) and priorities by definition (SET_ATTACK_PRIORITY_THING / _KIND_OF, SET_DEFAULT_ATTACK_PRIORITY).
// A unit given a set (Aggression::prioritySet: the set's index + 1; 0 none) picks its targets by them (AI::findClosestEnemy:
// nearest first, a priority less one per `distanceModifier` of distance, at least 1, the best kept, 0 never attacked).
// Simulation state: checkpointed and hashed with the session's resources.
export namespace engine::gameplay
{
class AttackPriorities
{
public:
	struct Set
	{
		std::string name;
		std::int32_t defaultPriority{1};
		std::vector<std::pair<std::uint32_t, std::int32_t>> priorities; // by definition, sorted
	};

	Engine::Math::Fixed distanceModifier; // AIData AttackPriorityDistanceModifier (0: distance does not count)

	// findAttackInfo(name, create): the set's index + 1 (0: none, and none made).
	std::uint16_t Find(std::string_view name, bool create)
	{
		for (std::size_t index = 0; index < m_sets.size(); ++index)
			if (m_sets[index].name == name)
				return static_cast<std::uint16_t>(index + 1);
		if (!create || name.empty() || m_sets.size() >= 0xFFFE)
			return 0;
		m_sets.push_back({std::string(name)});
		return static_cast<std::uint16_t>(m_sets.size());
	}
	std::uint16_t Find(std::string_view name) const
	{
		for (std::size_t index = 0; index < m_sets.size(); ++index)
			if (m_sets[index].name == name)
				return static_cast<std::uint16_t>(index + 1);
		return 0;
	}
	void SetPriority(std::uint16_t set, std::uint32_t definition, std::int32_t priority)
	{
		if (set == 0 || set > m_sets.size())
			return;
		auto &list = m_sets[set - 1].priorities;
		const auto at = std::ranges::lower_bound(list, definition, {}, &std::pair<std::uint32_t, std::int32_t>::first);
		if (at != list.end() && at->first == definition)
			at->second = priority;
		else
			list.insert(at, {definition, priority});
	}
	void SetDefault(std::uint16_t set, std::int32_t priority)
	{
		if (set != 0 && set <= m_sets.size())
			m_sets[set - 1].defaultPriority = priority;
	}
	// AttackPriorityInfo::getPriority.
	std::int32_t Priority(std::uint16_t set, std::uint32_t definition) const noexcept
	{
		if (set == 0 || set > m_sets.size())
			return 1;
		const Set &info = m_sets[set - 1];
		const auto at = std::ranges::lower_bound(info.priorities, definition, {}, &std::pair<std::uint32_t, std::int32_t>::first);
		return at != info.priorities.end() && at->first == definition ? at->second : info.defaultPriority;
	}
	const std::vector<Set> &Sets() const noexcept { return m_sets; }

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.I64(distanceModifier.Raw());
		writer.U32(static_cast<std::uint32_t>(m_sets.size()));
		for (const Set &set : m_sets)
		{
			writer.Text(set.name);
			writer.I64(set.defaultPriority);
			writer.U32(static_cast<std::uint32_t>(set.priorities.size()));
			for (const auto &[definition, priority] : set.priorities)
			{
				writer.U32(definition);
				writer.I64(priority);
			}
		}
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto modifier = reader.I64();
		const auto count = reader.U32();
		if (!modifier || !count)
			return false;
		std::vector<Set> sets;
		for (std::uint32_t index = 0; index < *count; ++index)
		{
			Set set;
			const auto name = reader.Text();
			const auto fallback = reader.I64();
			const auto entries = reader.U32();
			if (!name || !fallback || !entries)
				return false;
			set.name = *name;
			set.defaultPriority = static_cast<std::int32_t>(*fallback);
			for (std::uint32_t entry = 0; entry < *entries; ++entry)
			{
				const auto definition = reader.U32();
				const auto priority = reader.I64();
				if (!definition || !priority)
					return false;
				set.priorities.emplace_back(*definition, static_cast<std::int32_t>(*priority));
			}
			sets.push_back(std::move(set));
		}
		distanceModifier = Engine::Math::Fixed::FromRaw(*modifier);
		m_sets = std::move(sets);
		return true;
	}

private:
	std::vector<Set> m_sets;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::AttackPriorities>
{
	static constexpr std::string_view StableName = "engine.gameplay.attack_priorities";
};
}
