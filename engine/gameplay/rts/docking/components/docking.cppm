export module engine.gameplay.rts.docking.components.docking;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

// A mover's dealings with a dock (the original's AIDockMachine, and the
// dock's books on it: the approach point it holds, whether it has reached
// it, when it joined the queue, whether it has the dock's turn). No dock:
// it is not docking. Whoever starts a docking sets the dock, the Approach
// phase and `entering` (the phase's start is still to run); the dock system
// walks it through the phases; the dock's business (DockPhase::Process) is
// done by whoever docks there, which sets `finished` when there is no more.
export namespace engine::gameplay
{
enum class DockPhase : std::uint8_t
{
	None,
	Approach,         // to its approach point
	WaitForClearance, // at it, until it has the dock's turn (or the point ahead is free)
	Advance,          // on to the approach point ahead
	MoveToEntry,      // into the dock
	MoveToDock,       // to where the business is done
	Process,          // doing it, one action per actionDelay ticks
	MoveToExit,       // out
};

struct Docking
{
	ecs::Entity dock;
	DockPhase phase{DockPhase::None};
	bool entering{false}; // the phase's start is still to run
	bool reached{false};  // it stands at its approach point
	bool granted{false};  // it has the dock's turn (the dock's active docker), until it has left
	bool finished{false}; // the business is done
	std::int8_t slot{-1}; // the approach point it holds
	bool left{false};     // its last docking ended with it leaving the dock (no dock: the original's DOCKING_ENDING look)
	std::uint8_t reserved{};
	std::uint64_t queued{0};      // 1 + the tick it first reached an approach point (the dock's ready queue); 0: not queued
	std::uint64_t since{0};       // the tick it began waiting for clearance
	std::uint64_t nextAction{0};  // the tick of its next action
	std::uint64_t actionDelay{0}; // ticks between actions
};

// A dock waiting for clearance gives up after this long (30 seconds at 30 ticks a second).
inline constexpr std::uint64_t DockClearanceTimeoutTicks = 30 * 30;

inline void StartDocking(Docking &docking, ecs::Entity dock, std::uint64_t actionDelay) noexcept
{
	docking = {};
	docking.dock = dock;
	docking.phase = DockPhase::Approach;
	docking.entering = true;
	docking.actionDelay = actionDelay;
}

inline bool IsDocking(const Docking &docking) noexcept { return docking.dock.IsValid(); }
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Docking>
{
	static constexpr std::string_view StableName = "engine.gameplay.docking";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Docking &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64((std::uint64_t{value.dock.index} << 32) | value.dock.generation);
		hasher.AppendU64((static_cast<std::uint64_t>(value.phase) << 40) | (value.entering ? 1ull << 32 : 0) | (value.reached ? 1ull << 33 : 0) |
			(value.granted ? 1ull << 34 : 0) | (value.finished ? 1ull << 35 : 0) | (value.left ? 1ull << 36 : 0) | static_cast<std::uint8_t>(value.slot));
		hasher.AppendU64(value.queued);
		hasher.AppendU64(value.since);
		hasher.AppendU64(value.nextAction);
		hasher.AppendU64(value.actionDelay);
	}
};
}
