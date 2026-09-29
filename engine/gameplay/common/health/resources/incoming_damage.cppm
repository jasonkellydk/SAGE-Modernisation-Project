export module engine.gameplay.common.health.resources.incoming_damage;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// The damage every entity takes this tick, sorted by target (then in the
// order it was dealt), so each target's share is one contiguous range the
// health system can find without locks.
export namespace engine::gameplay
{
struct DamageRecord
{
	ecs::Entity target;
	ecs::Entity source;
	Engine::Math::Fixed amount;
	std::uint32_t damageType{0};
	std::uint32_t deathType{0};
	// The damage type whose effects it shows (DamageInfo m_damageFXOverride); NoFxType: its own.
	static constexpr std::uint32_t NoFxType = 0xFFFFFFFFu;
	std::uint32_t fxType{NoFxType};
	// The player whose weapon dealt it as it was fired (DamageInfo m_sourcePlayerMask); NoPlayer: the source's owner.
	static constexpr std::uint32_t NoPlayer = 0xFFFFFFFFu;
	std::uint32_t sourcePlayer{NoPlayer};
	// DamageInfo m_damageStatusType: the object status bit a STATUS damage gives (NoStatus: none).
	static constexpr std::uint32_t NoStatus = 0xFFFFFFFFu;
	std::uint32_t statusType{NoStatus};
};

class IncomingDamage
{
public:
	void Clear() noexcept { m_records.clear(); }

	void Add(DamageRecord record) { m_records.push_back(record); }

	// Sorts by target; records for one target keep the order they were added.
	void Seal()
	{
		std::stable_sort(m_records.begin(), m_records.end(), [](const DamageRecord &a, const DamageRecord &b) {
			if (a.target.index != b.target.index)
				return a.target.index < b.target.index;
			return a.target.generation < b.target.generation;
		});
	}

	std::span<const DamageRecord> For(ecs::Entity target) const
	{
		const auto less = [](const DamageRecord &record, ecs::Entity entity) {
			return record.target.index != entity.index ? record.target.index < entity.index : record.target.generation < entity.generation;
		};
		const auto first = std::lower_bound(m_records.begin(), m_records.end(), target, less);
		auto last = first;
		while (last != m_records.end() && last->target == target)
			++last;
		return {first, last};
	}

	bool Empty() const noexcept { return m_records.empty(); }
	// For a pass that re-aims or drops records (sealed again after it).
	std::vector<DamageRecord> &Records() noexcept { return m_records; }
	std::span<const DamageRecord> All() const noexcept { return m_records; }

private:
	std::vector<DamageRecord> m_records;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::IncomingDamage>
{
	static constexpr std::string_view StableName = "engine.gameplay.incoming_damage";
};
}
