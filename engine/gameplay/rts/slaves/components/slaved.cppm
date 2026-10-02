export module engine.gameplay.rts.slaves.components.slaved;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;

// A unit that serves a master (the original's SlavedUpdate: drones): how far
// it guards, attacks and scouts around its master (and wanders within those),
// how it repairs its master (a battle drone), the master it serves (none until
// enslaved), its guard point's offset from the master, how many ticks until it
// next looks (it looks every SlavedUpdateRate ticks; every tick while it is in a
// repair state) and its repair state.
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
	// Repairing its master: RepairRange (how far about the master it moves between welds), RepairMinAltitude /
	// RepairMaxAltitude (its hover over the master or the ground), RepairRatePerSecond (per tick here: / 30; none: it
	// never repairs), RepairWhenBelowHealth% (at or below this its master's repair comes first), RepairMin/MaxReadyTime
	// and RepairMin/MaxWeldTime (ticks), StayOnSameLayerAsMaster.
	Engine::Math::Fixed repairRange;
	Engine::Math::Fixed repairMinAltitude;
	Engine::Math::Fixed repairMaxAltitude;
	Engine::Math::Fixed repairPerTick;
	std::int32_t repairBelowPercent{0};
	std::uint32_t minReadyTicks{0};
	std::uint32_t maxReadyTicks{0};
	std::uint32_t minWeldTicks{0};
	std::uint32_t maxWeldTicks{0};
	std::uint8_t stayOnMasterLayer{0};
	std::uint8_t reservedRepair[3]{}; // no padding: checkpoints hold its bytes
};

// SlavedUpdate's RepairStates (REPAIRSTATE_PACKING is never entered).
enum class SlaveRepairState : std::uint8_t
{
	None,
	Unpacking,
	Ready,
	Extending,
	Welding,
	Retracting,
};

// The repair arm's model condition (setRepairModelConditionStates): PACKING from creation (onObjectCreated, for a drone
// that repairs) and whenever a repair ends, then UNPACKING, FIRING_B (extending, welding) and FIRING_C (retracting).
enum class SlaveRepairLook : std::uint8_t
{
	Packing,
	Unpacking,
	FiringB,
	FiringC,
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
	SlaveRepairState repairState{SlaveRepairState::None};
	std::uint8_t repairing{0}; // m_repairing: its first sparks have flown, it heals its master while close
	SlaveRepairLook repairLook{SlaveRepairLook::Packing};
	std::uint8_t reservedState[5]{}; // no padding: checkpoints hold its bytes
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
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Slaved &value, StateHasher &hasher) noexcept
	{
		const auto &d = value.definition;
		for (const Engine::Math::Fixed field : {d.guardMaxRange, d.guardWanderRange, d.attackRange, d.attackWanderRange, d.scoutRange, d.scoutWanderRange,
				 d.spottingRange, d.repairRange, d.repairMinAltitude, d.repairMaxAltitude, d.repairPerTick})
			hasher.AppendU64(static_cast<std::uint64_t>(field.Raw()));
		hasher.AppendU64(d.spottingBonus);
		hasher.AppendU64((static_cast<std::uint64_t>(static_cast<std::uint32_t>(d.repairBelowPercent)) << 32) | d.stayOnMasterLayer);
		hasher.AppendU64((static_cast<std::uint64_t>(d.minReadyTicks) << 32) | d.maxReadyTicks);
		hasher.AppendU64((static_cast<std::uint64_t>(d.minWeldTicks) << 32) | d.maxWeldTicks);
		hasher.AppendU64((static_cast<std::uint64_t>(value.master.index) << 32) | value.master.generation);
		hasher.AppendU64(static_cast<std::uint64_t>(value.guardOffset.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.guardOffset.y.Raw()));
		hasher.AppendU64((static_cast<std::uint64_t>(value.waitTicks) << 32) | value.enslaved);
		hasher.AppendU64(static_cast<std::uint64_t>(value.radius.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.masterRadius.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.repairState) | static_cast<std::uint64_t>(value.repairing) << 8 |
			static_cast<std::uint64_t>(value.repairLook) << 16);
	}
};
}
