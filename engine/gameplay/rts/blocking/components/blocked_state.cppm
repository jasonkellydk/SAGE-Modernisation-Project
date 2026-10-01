export module engine.gameplay.rts.blocking.components.blocked_state;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
export import engine.ecs.core.entity;

// How a ground unit's AI stands with the units in its way (AIUpdateInterface's m_blockedFrames, m_isBlockedAndStuck,
// m_bumpSpeedLimit, m_ignoreCollisionsUntil and its queued path request, setQueueForPathTime):
//   frames: how many ticks running it has been held back by a unit in its way (0: not);
//   stuck: blocked by a unit that is not moving while it already faces its way: its route is planned again;
//   bumpLimit: the speed it creeps at while blocked, easing off by 5% a tick and back up by 5% a tick (at least a fifth of
//     its speed) once it is not (FastAsPossible: no limit);
//   ignoreUntil: until this tick it pays no heed to the units it runs into;
//   replanAt: its route is to be planned again this tick (0: no request waiting; a request too soon after the last plan
//     waits a second);
//   awayFrom, awayFromBefore: the units it last made way for (m_moveOutOfWay1, m_moveOutOfWay2);
//   throughUnits: it paths and moves through units (m_canPathThroughUnits: making way and stuck, it no longer weighs the
//     units it runs into, and its routes are not blocked by allies).
// Simulation state: checkpointed.
export namespace engine::gameplay
{
// FAST_AS_POSSIBLE.
inline constexpr Engine::Math::Fixed FastAsPossible = Engine::Math::Fixed::FromInt(999999);

struct BlockedState
{
	std::uint32_t frames{0};
	std::uint8_t stuck{0};
	std::uint8_t reserved[3]{}; // no padding: checkpoints hold its bytes
	Engine::Math::Fixed bumpLimit{FastAsPossible};
	std::uint64_t ignoreUntil{0};
	std::uint64_t replanAt{0};
	ecs::Entity awayFrom;
	ecs::Entity awayFromBefore;
	std::uint8_t throughUnits{0};
	std::uint8_t reserved2[7]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::BlockedState>
{
	static constexpr std::string_view StableName = "engine.gameplay.blocked_state";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::BlockedState &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.frames | static_cast<std::uint64_t>(value.stuck) << 32);
		hasher.AppendU64(static_cast<std::uint64_t>(value.bumpLimit.Raw()));
		hasher.AppendU64(value.ignoreUntil);
		hasher.AppendU64(value.replanAt);
		hasher.AppendU64(static_cast<std::uint64_t>(value.awayFrom.index) << 32 | value.awayFrom.generation);
		hasher.AppendU64(static_cast<std::uint64_t>(value.awayFromBefore.index) << 32 | value.awayFromBefore.generation);
		hasher.AppendU64(value.throughUnits);
	}
};
}
