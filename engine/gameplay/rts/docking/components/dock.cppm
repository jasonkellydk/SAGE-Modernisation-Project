export module engine.gameplay.rts.docking.components.dock;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// A place movers dock with (the original's DockUpdate): where they wait
// their turn (approach points, queued nearest the dock first), where they
// enter, do their business and leave (all in the world, placed with the
// dock), and how it takes them. Which mover holds which approach point and
// whose turn it is are the movers' own state (Docking): the dock itself only
// changes when it is crippled or closed.
//   A dock with no entry point is boneless: movers stay where they reach it
//   (a far one on a dock that draws movers in is pulled in to touch it).
//   A dock with no approach points of its own places them just outside
//   itself, toward whoever comes; a dynamic one takes any number of movers.
export namespace engine::gameplay
{
struct Dock
{
	static constexpr std::uint32_t MaxApproaches = 16;
	// Waiting movers a dynamic dock holds (the original grows without bound).
	static constexpr std::uint32_t MaxDynamicApproaches = 32;

	std::array<Engine::Math::FixedVector2, MaxApproaches> approach{}; // DockWaitingNN, in the world
	Engine::Math::FixedVector2 enter;                                 // DockStart
	Engine::Math::FixedVector2 action;                                // DockAction
	Engine::Math::FixedVector2 exit;                                  // DockEnd
	Engine::Math::Fixed majorRadius;                                  // its footprint (approach points toward the mover)
	std::uint8_t approachCount{0};  // NumberApproachPositions (not dynamic)
	std::uint8_t approachPoints{0}; // how many of them are its own (bones); none: placed toward the mover
	bool dynamic{false};            // NumberApproachPositions -1: any number wait
	bool passthrough{true};         // AllowsPassthrough: movers drive through it between its points
	bool boneless{true};            // no entry point: movers stay where they reach it
	bool drawsIn{false};            // a boneless dock pulls a far mover in to touch it (KINDOF_SUPPLY_SOURCE)
	bool crippled{false};           // gives no one a turn
	bool open{true};                // turns movers away
	// A dock that carries its movers off (RailedTransportDockUpdate::isClearToEnter: its container must have room for
	// the mover): the slots it has free; a mover needing more waits for clearance. Others: unlimited.
	std::uint32_t room{0xFFFFFFFFu};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Dock>
{
	static constexpr std::string_view StableName = "engine.gameplay.dock";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Dock &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64((std::uint64_t{value.approachCount} << 32) | (std::uint64_t{value.approachPoints} << 24) | (value.dynamic ? 1u : 0u) |
			(value.passthrough ? 2u : 0u) | (value.boneless ? 4u : 0u) | (value.drawsIn ? 8u : 0u) | (value.crippled ? 16u : 0u) | (value.open ? 32u : 0u));
		hasher.AppendU64(value.room);
		for (const auto point : {value.enter, value.action, value.exit})
		{
			hasher.AppendU64(static_cast<std::uint64_t>(point.x.Raw()));
			hasher.AppendU64(static_cast<std::uint64_t>(point.y.Raw()));
		}
	}
};
}
