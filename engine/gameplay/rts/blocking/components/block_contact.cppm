export module engine.gameplay.rts.blocking.components.block_contact;
import std;

export import engine.ecs.core.component_registry;
export import engine.gameplay.rts.blocking.components.blocked_state;

// What the tick's collisions found for a ground unit's AI (AIUpdateInterface::processCollision's writes, kept apart from
// its BlockedState so every unit's collisions are weighed at once against the others' state as the tick left it):
//   blocked: a unit is in its way (m_isBlocked): its locomotor creeps next tick;
//   maxSpeed: the fastest it may go without running into it (m_curMaxBlockedSpeed: the least over the units in its way);
//   stuck: blocked by a unit that is not moving while it faces its way (m_isBlockedAndStuck);
//   frames: what it does to the count of ticks it has been blocked: FramesAtLeastOne (a first block counts one),
//     FramesOne (it is turning: the count starts over at one).
// Read and cleared the next tick: frames and stuck by its move (before it decides to plan again), blocked and maxSpeed by
// its locomotor. Simulation state: checkpointed.
export namespace engine::gameplay
{
namespace block_frames
{
inline constexpr std::uint8_t Keep = 0;
inline constexpr std::uint8_t AtLeastOne = 1;
inline constexpr std::uint8_t One = 2;
}

struct BlockContact
{
	Engine::Math::Fixed maxSpeed{FastAsPossible};
	std::uint8_t blocked{0};
	std::uint8_t stuck{0};
	std::uint8_t frames{block_frames::Keep};
	std::uint8_t reserved[5]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::BlockContact>
{
	static constexpr std::string_view StableName = "engine.gameplay.block_contact";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::BlockContact &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.maxSpeed.Raw()));
		hasher.AppendU64(value.blocked | static_cast<std::uint64_t>(value.stuck) << 8 | static_cast<std::uint64_t>(value.frames) << 16);
	}
};
}
