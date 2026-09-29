export module games.generalszh.gameplay.powers.components.launcher_door;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// A superweapon's launch door (the original's MissileLauncherBuildingUpdate): its state, the state it goes to at
// `timeoutTick` (0: none), and what it does per kind of building (LauncherDoorConfig, definition data).
export namespace generalszh::gameplay
{
enum class LauncherDoorState : std::uint8_t
{
	Closed,
	Opening,
	Open,
	WaitingToClose,
	Closing,
};

struct LauncherDoor
{
	std::uint64_t timeoutTick{0};
	LauncherDoorState state{LauncherDoorState::Closed};
	LauncherDoorState timeoutState{LauncherDoorState::Closed};
	std::uint8_t reserved[6]{}; // no padding: checkpoints hold its bytes
};

// Per kind of building: the power (a SpecialPowerRules index), the door's times, and each state's effect
// (DeathEffectKind::Effect ids; none: 0xFFFFFFFF), in LauncherDoorState order.
struct LauncherDoorConfig
{
	bool present{false};
	std::uint32_t power{0xFFFFFFFFu};
	std::uint64_t openTicks{0};
	std::uint64_t waitOpenTicks{0};
	std::uint64_t closeTicks{0};
	std::array<std::uint32_t, 5> effects{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
	std::string openIdleAudio;
};

// The tick's door effects to play (FXList::doFXPos where the building stands), for the presentation; cleared as each
// tick starts.
struct LauncherDoorEffects
{
	struct Played
	{
		std::uint32_t effect{0xFFFFFFFFu};
		Engine::Math::FixedVector3 at;
	};
	std::vector<Played> played;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::LauncherDoor>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.launcher_door";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<generalszh::gameplay::LauncherDoorEffects>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.launcher_door_effects";
};
}
