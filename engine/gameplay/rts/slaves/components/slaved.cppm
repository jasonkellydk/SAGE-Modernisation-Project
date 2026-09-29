export module engine.gameplay.rts.slaves.components.slaved;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;

// A unit that serves a master (the original's SlavedUpdate: drones): how far
// it guards, attacks and scouts around its master (and wanders within those),
// the master it serves (none until enslaved), its guard point's offset from
// the master and how many ticks until it next looks (it looks every
// SlavedUpdateRate ticks).
export namespace engine::gameplay
{
struct SlavedDefinition
{
	Engine::Math::Fixed guardMaxRange;    // GuardMaxRange
	Engine::Math::Fixed guardWanderRange; // GuardWanderRange
	Engine::Math::Fixed attackRange;      // AttackRange
	Engine::Math::Fixed attackWanderRange; // AttackWanderRange
	Engine::Math::Fixed scoutRange;       // ScoutRange
	Engine::Math::Fixed scoutWanderRange; // ScoutWanderRange
	// DistToTargetToGrantRangeBonus: this close to its master's victim, the master gets `spottingBonus`
	// (weapon bonus condition bits the game chooses; none: no spotting).
	Engine::Math::Fixed spottingRange;
	std::uint32_t spottingBonus{0};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
};

struct Slaved
{
	SlavedDefinition definition;
	ecs::Entity master;
	Engine::Math::FixedVector2 guardOffset;
	std::uint32_t waitTicks{0};
	std::uint32_t enslaved{0}; // it has had a master (lost: it crashes)
	// Bounding circles (GeometryInfo::getBoundingCircleRadius): its own and its master's, for the
	// FROM_BOUNDINGSPHERE_2D distances.
	Engine::Math::Fixed radius;
	Engine::Math::Fixed masterRadius;
};

// SLAVED_UPDATE_RATE: a quarter second.
inline constexpr std::uint32_t SlavedUpdateTicks = 7;
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Slaved>
{
	static constexpr std::string_view StableName = "engine.gameplay.slaved";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Slaved &value, StateHasher &hasher) noexcept
	{
		const auto &d = value.definition;
		for (const Engine::Math::Fixed field : {d.guardMaxRange, d.guardWanderRange, d.attackRange, d.attackWanderRange, d.scoutRange, d.scoutWanderRange,
				 d.spottingRange})
			hasher.AppendU64(static_cast<std::uint64_t>(field.Raw()));
		hasher.AppendU64(d.spottingBonus);
		hasher.AppendU64((static_cast<std::uint64_t>(value.master.index) << 32) | value.master.generation);
		hasher.AppendU64(static_cast<std::uint64_t>(value.guardOffset.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.guardOffset.y.Raw()));
		hasher.AppendU64((static_cast<std::uint64_t>(value.waitTicks) << 32) | value.enslaved);
		hasher.AppendU64(static_cast<std::uint64_t>(value.radius.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.masterRadius.Raw()));
	}
};
}
