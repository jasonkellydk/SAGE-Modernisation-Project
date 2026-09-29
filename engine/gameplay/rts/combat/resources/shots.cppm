export module engine.gameplay.rts.combat.resources.shots;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;
export import engine.ecs.core.entity_codec;

// Shots in flight. The weapon system emits the tick's shots per chunk; the
// impact system queues them by impact tick and resolves the due ones. The
// resolved impacts are kept for the client's effects.
export namespace engine::gameplay
{
struct Shot
{
	ecs::Entity source;
	ecs::Entity target;
	std::uint32_t weapon{0};
	std::uint32_t sourcePlayer{0};
	Engine::Math::FixedVector3 origin;
	Engine::Math::FixedVector3 aim;
	std::uint64_t fireTick{0};
	std::uint64_t impactTick{0};
	// Which way its barrel pointed (the launcher's facing plus its turret's turn; its turret's pitch).
	Engine::Math::TurnAngle launchYaw;
	Engine::Math::TurnAngle launchPitch;
	// The projectile that carried it to where it went off (its blast spares it).
	ecs::Entity carrier;
	// The weapon slot that fired it (PRIMARY 0, SECONDARY 1, TERTIARY 2).
	std::uint8_t slot{0};
	std::uint8_t reserved[3]{}; // no padding: checkpoints hold its bytes
	// What its firer rides in (a passenger allowed to fire): its shots never run into it.
	ecs::Entity shelter;
	std::uint32_t reserved2{0};
	// Its firer's weapon bonus when it fired (Weapon::computeBonus): damage and damage radius multipliers.
	Engine::Math::Fixed damageScale{Engine::Math::Fixed::One()};
	Engine::Math::Fixed radiusScale{Engine::Math::Fixed::One()};
	// Reach added to both damage radii after the bonus (a blast round something's edge rather than its middle).
	Engine::Math::Fixed radiusBonus;
};

// A shot whose projectile object carries it lands when (and where) that projectile detonates.
inline constexpr std::uint64_t LandsWithProjectile = ~std::uint64_t{0};

struct Impact
{
	ecs::Entity source;
	std::uint32_t weapon{0};
	Engine::Math::FixedVector3 position;
};

using FiredShots = ecs::ChunkOutputs<Shot>;

// Shots whose projectiles detonated this tick, per chunk, each with `aim` where it went off.
struct Detonations : ecs::ChunkOutputs<Shot>
{
};

// Shots whose guided missiles detonated this tick, per chunk, each with `aim` where it went off.
struct MissileDetonations : ecs::ChunkOutputs<Shot>
{
};

// Weapons fired on their own, not from a weapon set (WeaponStore::createAndFireTempWeapon: an object creation list's
// FireWeapon), waiting for the next tick's firing: the weapon system turns each into a shot from the source at the spot.
struct TemporaryWeaponFire
{
	ecs::Entity source;
	std::uint32_t weapon{0};
	std::uint32_t sourcePlayer{0};
	Engine::Math::FixedVector3 origin;
	Engine::Math::FixedVector3 aim;
};

class TemporaryWeaponFires
{
public:
	void Add(const TemporaryWeaponFire &fire) { m_pending.push_back(fire); }
	std::vector<TemporaryWeaponFire> Take() { return std::exchange(m_pending, {}); }
	bool Empty() const noexcept { return m_pending.empty(); }

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_pending.size()));
		for (const TemporaryWeaponFire &fire : m_pending)
		{
			ecs::WriteEntity(writer, fire.source);
			writer.U32(fire.weapon);
			writer.U32(fire.sourcePlayer);
			for (const Engine::Math::FixedVector3 &point : {fire.origin, fire.aim})
			{
				writer.I64(point.x.Raw());
				writer.I64(point.y.Raw());
				writer.I64(point.z.Raw());
			}
		}
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto count = reader.U32();
		if (!count || *count > 4096)
			return false;
		std::vector<TemporaryWeaponFire> pending;
		for (std::uint32_t index = 0; index < *count; ++index)
		{
			TemporaryWeaponFire fire;
			const auto source = ecs::ReadEntity(reader);
			const auto weapon = reader.U32();
			const auto player = reader.U32();
			if (!source || !weapon || !player)
				return false;
			fire.source = *source;
			fire.weapon = *weapon;
			fire.sourcePlayer = *player;
			for (Engine::Math::FixedVector3 *point : {&fire.origin, &fire.aim})
			{
				const auto x = reader.I64(), y = reader.I64(), z = reader.I64();
				if (!x || !y || !z)
					return false;
				*point = {Engine::Math::Fixed::FromRaw(*x), Engine::Math::Fixed::FromRaw(*y), Engine::Math::Fixed::FromRaw(*z)};
			}
			pending.push_back(fire);
		}
		m_pending = std::move(pending);
		return true;
	}

private:
	std::vector<TemporaryWeaponFire> m_pending;
};

class ShotQueue
{
public:
	// Adds shots; the queue stays ordered by impact tick, then by the order shots were fired.
	void Add(const Shot &shot)
	{
		const auto at = std::upper_bound(m_pending.begin(), m_pending.end(), shot.impactTick,
			[](std::uint64_t tick, const Shot &queued) { return tick < queued.impactTick; });
		m_pending.insert(at, shot);
	}

	// Removes and returns the shots landing at or before `tick`.
	std::vector<Shot> TakeDue(std::uint64_t tick)
	{
		const auto end = std::upper_bound(m_pending.begin(), m_pending.end(), tick,
			[](std::uint64_t now, const Shot &queued) { return now < queued.impactTick; });
		std::vector<Shot> due(m_pending.begin(), end);
		m_pending.erase(m_pending.begin(), end);
		return due;
	}

	const std::vector<Shot> &Pending() const noexcept { return m_pending; }

	// Checkpoints: the shots still in flight (impacts are this tick's only).
	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		const auto point = [&](const Engine::Math::FixedVector3 &value) {
			writer.I64(value.x.Raw());
			writer.I64(value.y.Raw());
			writer.I64(value.z.Raw());
		};
		writer.U32(static_cast<std::uint32_t>(m_pending.size()));
		for (const Shot &shot : m_pending)
		{
			ecs::WriteEntity(writer, shot.source);
			ecs::WriteEntity(writer, shot.target);
			writer.U32(shot.weapon);
			writer.U32(shot.sourcePlayer);
			point(shot.origin);
			point(shot.aim);
			writer.U64(shot.fireTick);
			writer.U64(shot.impactTick);
			writer.U32(shot.launchYaw.units);
			writer.U32(shot.launchPitch.units);
			ecs::WriteEntity(writer, shot.carrier);
			writer.U8(shot.slot);
			ecs::WriteEntity(writer, shot.shelter);
			writer.I64(shot.damageScale.Raw());
			writer.I64(shot.radiusScale.Raw());
			writer.I64(shot.radiusBonus.Raw());
		}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto fixed = [&] { return Engine::Math::Fixed::FromRaw(reader.I64().value_or(0)); };
		const auto point = [&] {
			const Engine::Math::Fixed x = fixed();
			const Engine::Math::Fixed y = fixed();
			return Engine::Math::FixedVector3{x, y, fixed()};
		};
		std::vector<Shot> pending;
		const auto count = reader.U32();
		for (std::uint32_t index = 0; count && index < *count && !reader.Failed(); ++index)
		{
			Shot shot;
			shot.source = ecs::ReadEntity(reader).value_or(ecs::Entity{});
			shot.target = ecs::ReadEntity(reader).value_or(ecs::Entity{});
			shot.weapon = reader.U32().value_or(0);
			shot.sourcePlayer = reader.U32().value_or(0);
			shot.origin = point();
			shot.aim = point();
			shot.fireTick = reader.U64().value_or(0);
			shot.impactTick = reader.U64().value_or(0);
			shot.launchYaw.units = reader.U32().value_or(0);
			shot.launchPitch.units = reader.U32().value_or(0);
			shot.carrier = ecs::ReadEntity(reader).value_or(ecs::Entity{});
			shot.slot = reader.U8().value_or(0);
			shot.shelter = ecs::ReadEntity(reader).value_or(ecs::Entity{});
			shot.damageScale = fixed();
			shot.radiusScale = fixed();
			shot.radiusBonus = fixed();
			pending.push_back(shot);
		}
		if (!count || reader.Failed())
			return false;
		m_pending = std::move(pending);
		m_impacts.clear();
		return true;
	}
	std::vector<Impact> &Impacts() noexcept { return m_impacts; }
	const std::vector<Impact> &Impacts() const noexcept { return m_impacts; }

private:
	std::vector<Shot> m_pending;
	std::vector<Impact> m_impacts;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::FiredShots>
{
	static constexpr std::string_view StableName = "engine.gameplay.fired_shots";
};

template<>
struct ResourceTraits<engine::gameplay::Detonations>
{
	static constexpr std::string_view StableName = "engine.gameplay.detonations";
};

template<>
struct ResourceTraits<engine::gameplay::MissileDetonations>
{
	static constexpr std::string_view StableName = "engine.gameplay.missile_detonations";
};

template<>
struct ResourceTraits<engine::gameplay::ShotQueue>
{
	static constexpr std::string_view StableName = "engine.gameplay.shot_queue";
};

template<>
struct ResourceTraits<engine::gameplay::TemporaryWeaponFires>
{
	static constexpr std::string_view StableName = "engine.gameplay.temporary_weapon_fires";
};
}
