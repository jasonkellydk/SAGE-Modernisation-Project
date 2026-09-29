export module games.generalszh.gameplay.powers.components.spy_vision;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
import engine.ecs.system.system;

// An object's SpyVisionUpdate modules (up to two: the Internet Center's satellite hacks), each as data: the tick its
// look through the enemy's eyes ends (m_deactivateFrame; never: ~0), the tick it is disabled until
// (m_disabledUntilFrame), the tick its update next runs (its wake frame; never: ~0), whether it is on
// (m_currentlyActive), whether its timers start again on its next update (m_resetTimersNextUpdate), whether its upgrade
// has come (the upgrade mux), and a turn-on asked for (an upgrade or a SpyVisionSpecialPower, for `activateTicks`; 0:
// for good) that its system carries out. Also the player it spies for, and whether it was disabled at the last look.
// SpyVisionEvents: the tick's spying turned on or off, by player, module and definition (applied after the step).
// Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct SpyVisionState
{
	static constexpr std::uint64_t Never = ~std::uint64_t{0};
	std::uint64_t deactivateTick{0};
	std::uint64_t disabledUntil{0};
	std::uint64_t wakeTick{Never};
	std::uint64_t activateTicks{0};
	std::uint8_t active{0};
	std::uint8_t resetTimers{0};
	std::uint8_t upgraded{0};
	std::uint8_t activateAsked{0};
	std::uint32_t reserved{0};
};

struct SpyVision
{
	std::array<SpyVisionState, 2> modules{};
	std::uint32_t player{0};
	std::uint8_t count{0};
	std::uint8_t wasDisabled{0};
	std::uint8_t reserved[2]{};
};

struct SpyVisionEvent
{
	ecs::Entity source;
	std::uint32_t player{0};     // who spies
	std::uint32_t definition{0}; // the spying object's kind (its modules' SpyOnKindof)
	std::uint8_t module{0};
	std::uint8_t setting{0};
	std::uint8_t reserved[6]{};
};

struct SpyVisionEvents : ecs::ChunkOutputs<SpyVisionEvent>
{
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::SpyVision>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.spy_vision";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::SpyVision &value, StateHasher &hasher) noexcept
	{
		for (const auto &module : value.modules)
		{
			hasher.AppendU64(module.deactivateTick);
			hasher.AppendU64(module.disabledUntil);
			hasher.AppendU64(module.wakeTick);
			hasher.AppendU64(module.activateTicks);
			hasher.AppendU64(std::uint64_t{module.active} | (std::uint64_t{module.resetTimers} << 8) | (std::uint64_t{module.upgraded} << 16) |
				(std::uint64_t{module.activateAsked} << 24));
		}
		hasher.AppendU64(value.player | (std::uint64_t{value.count} << 32) | (std::uint64_t{value.wasDisabled} << 40));
	}
};

template<>
struct ResourceTraits<generalszh::gameplay::SpyVisionEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.spy_vision_events";
};
}
