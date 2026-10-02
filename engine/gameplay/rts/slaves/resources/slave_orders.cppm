export module engine.gameplay.rts.slaves.resources.slave_orders;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
export import engine.gameplay.common.healing.resources.heal_pulses;
import engine.ecs.system.system;

// What slaves asked of their own locomotors this tick, in order (the game carries them out between ticks: choosing a
// locomotor set needs its templates), and the welds a repairing drone started (for the presentation's sparks).
// SlavedUpdate's endRepair (its normal set, neither ultra-accurate nor at a precise height), doRepairLogic (coming
// to its master: a precise height once within twice its master's bounding sphere) and moveToNewRepairSpot (its panic
// set, ultra-accurate, at a precise height). `heals`: what repairing slaves heal their masters this tick (for
// SlaveRepairSystem to pass on). Cleared as the slaves' pass starts.
export namespace engine::gameplay
{
enum class SlaveLocomotorSet : std::uint8_t
{
	Keep,
	Normal,
	Panic,
};

struct SlaveLocomotorOrder
{
	ecs::Entity slave;
	Engine::Math::Fixed preciseHeight; // its goal's height (with preciseZ)
	SlaveLocomotorSet set{SlaveLocomotorSet::Keep};
	std::uint8_t preciseZ{0};
	// 0 / 1: ultra-accurate off / on; Keep: as it is.
	static constexpr std::uint8_t Keep = 2;
	std::uint8_t ultraAccurate{Keep};
};

// setRepairState(REPAIRSTATE_WELDING): sparks at the drone's weld bone (RepairWeldingSys at RepairWeldingFXBone) whose
// particles live `lifetime` frames (the weld's frames times LOGICFRAMES_PER_SECOND, as the original), with the repair
// sparks sound.
struct RepairWeld
{
	ecs::Entity slave;
	std::uint32_t lifetime{0};
};

struct SlaveOrders
{
	std::vector<SlaveLocomotorOrder> locomotors;
	std::vector<RepairWeld> welds;
	std::vector<HealPulse> heals;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::SlaveOrders>
{
	static constexpr std::string_view StableName = "engine.gameplay.slave_orders";
};
}
