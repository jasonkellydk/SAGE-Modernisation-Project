export module engine.gameplay.rts.combat.resources.historic_damage;
import std;

export import Engine.Core.Math.FixedVector;
export import engine.core.serialization.byte_stream;
export import engine.gameplay.common.weapons.definitions.weapon;
import engine.ecs.system.system;

// WeaponTemplate::m_historicDamage: per weapon with a historic bonus, where and when its recent hits landed (oldest
// first), kept at most HistoricDamageLimit (GameData) and cleared when the bonus fires. Simulation state: checkpointed.
export namespace engine::gameplay
{
class HistoricDamage
{
public:
	struct Hit
	{
		std::uint64_t tick{0};
		Engine::Math::FixedVector2 at;
	};

	HistoricDamage() = default;
	explicit HistoricDamage(std::uint64_t limitTicks) : m_limitTicks(limitTicks) {}

	std::uint64_t LimitTicks() const noexcept { return m_limitTicks; }
	std::vector<Hit> &For(std::uint32_t weapon) { return m_byWeapon[weapon]; }
	const std::map<std::uint32_t, std::vector<Hit>> &All() const noexcept { return m_byWeapon; }

	void Save(core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_byWeapon.size()));
		for (const auto &[weapon, hits] : m_byWeapon)
		{
			writer.U32(weapon);
			writer.U32(static_cast<std::uint32_t>(hits.size()));
			for (const Hit &hit : hits)
			{
				writer.U64(hit.tick);
				writer.I64(hit.at.x.Raw());
				writer.I64(hit.at.y.Raw());
			}
		}
	}
	bool Load(core::serialization::ByteReader &reader)
	{
		const auto weapons = reader.U32();
		if (!weapons)
			return false;
		std::map<std::uint32_t, std::vector<Hit>> loaded;
		for (std::uint32_t index = 0; index < *weapons; ++index)
		{
			const auto weapon = reader.U32();
			const auto count = reader.U32();
			if (!weapon || !count || *count > 65536)
				return false;
			auto &hits = loaded[*weapon];
			for (std::uint32_t hit = 0; hit < *count; ++hit)
			{
				const auto tick = reader.U64();
				const auto x = reader.I64(), y = reader.I64();
				if (!tick || !x || !y)
					return false;
				hits.push_back({*tick, {Engine::Math::Fixed::FromRaw(*x), Engine::Math::Fixed::FromRaw(*y)}});
			}
		}
		m_byWeapon = std::move(loaded);
		return true;
	}

private:
	std::uint64_t m_limitTicks{0};
	std::map<std::uint32_t, std::vector<Hit>> m_byWeapon;
};

// WeaponTemplate::processHistoricDamage as the retail game has it (RETAIL_COMPATIBLE_CRC: trimOldHistoricDamage by
// HistoricDamageLimit, then the E3 plug): a hit of `weapon` at `at` on tick `now`. Hits older than the limit are
// dropped; those within HistoricBonusTime and HistoricBonusRadius (2D) of it are counted, and with HistoricBonusCount - 1
// of them (itself counts too) the bonus weapon fires and the weapon's history is cleared; else the hit joins it. The
// original's frame arithmetic is 32-bit unsigned: early in a game (before the limit, or the bonus time) the subtraction
// wraps, so every hit is dropped, or none counts, then. True when the bonus weapon fires.
inline bool ProcessHistoricDamage(HistoricDamage &history, std::uint32_t weapon, const WeaponDefinition &definition, Engine::Math::FixedVector2 at,
	std::uint64_t now)
{
	if (definition.historicBonusCount == 0 || definition.historicBonusWeapon == weapon)
		return false;
	std::vector<HistoricDamage::Hit> &hits = history.For(weapon);
	const std::uint32_t frame = static_cast<std::uint32_t>(now);
	const std::uint32_t expiration = frame - static_cast<std::uint32_t>(history.LimitTicks());
	std::size_t expired = 0;
	while (expired < hits.size() && static_cast<std::uint32_t>(hits[expired].tick) <= expiration)
		++expired;
	hits.erase(hits.begin(), hits.begin() + static_cast<std::ptrdiff_t>(expired));
	const std::uint32_t oldest = frame - static_cast<std::uint32_t>(definition.historicBonusTicks);
	const Engine::Math::Fixed reach = definition.historicBonusRadius * definition.historicBonusRadius;
	std::int64_t count = 0;
	for (const HistoricDamage::Hit &hit : hits)
		if (static_cast<std::uint32_t>(hit.tick) >= oldest && Engine::Math::DistanceSquared(at, hit.at) <= reach)
			++count;
	if (count >= static_cast<std::int64_t>(definition.historicBonusCount) - 1)
	{
		hits.clear();
		return true;
	}
	hits.push_back({now, at});
	return false;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::HistoricDamage>
{
	static constexpr std::string_view StableName = "engine.gameplay.historic_damage";
};
}
