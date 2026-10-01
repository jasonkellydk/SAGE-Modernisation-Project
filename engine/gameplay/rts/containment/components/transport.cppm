export module engine.gameplay.rts.containment.components.transport;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.gameplay.rts.containment.definitions.transport;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;

// Carrying: a transport's load and unloading state; a passenger inside one;
// a unit on its way to board one.
export namespace engine::gameplay
{
enum class TransportState : std::uint8_t
{
	Idle,
	Unloading,
};

struct Transport
{
	TransportDefinition definition;
	std::uint32_t occupied{0};
	TransportState state{TransportState::Idle};
	std::uint8_t reserved[3]{}; // no padding: checkpoints hold its bytes
	std::uint64_t nextExitTick{0};
	// Preferred height while not landing to unload.
	Engine::Math::Fixed cruiseHeight;
	bool landing{false};
	// Climbing straight back to its cruise height after landing (ChinookAIUpdate TAKING_OFF).
	bool takingOff{false};
	// Flying to the clear spot it lands on (ChinookTakeoffOrLandingState: findPositionAround), then coming straight down.
	bool seekingSpot{false};
	// Taking nobody in (a garrisoned building too damaged to hold anyone: GarrisonContain::isValidContainerFor).
	bool closed{false};
	// Told to come down and stay down (a scripted evacuation's landing: ChinookTakeoffOrLandingState), until told otherwise.
	bool landRequested{false};
	std::uint8_t reserved2[3]{};
	Engine::Math::FixedVector2 landingSpot;
	// The player of the last one in and the tick it got in (OpenContain::m_playerEnteredMask, which its next update
	// clears: the next tick's scripts see it); none yet: 0xFFFFFFFF.
	std::uint32_t enteredBy{0xFFFFFFFFu};
	std::uint32_t reserved3{0};
	std::uint64_t enteredTick{0};
	// The tick the last one got out through its door (OpenContain::exitObjectViaDoor: DOOR_1_OPENING until DoorOpenTime
	// has passed, DOOR_1_CLOSING after); 0: nobody yet.
	std::uint64_t doorOpenedTick{0};
};

struct Passenger
{
	ecs::Entity transport;
	std::uint32_t slots{1};
	std::uint32_t reserved{0};
	std::uint64_t since{0}; // the tick it got in (Object::m_containedByFrame)
};

struct Boarding
{
	ecs::Entity transport;
	// Only to touch it (a crate collide's approach, AIEnterState for a hijacker): its arrival is the game's to act on,
	// never a boarding into cargo.
	std::uint32_t touchOnly{0};
	// A script ordered it (its last command source CMD_FROM_SCRIPT: AIEnterState's re-checks skip the fog).
	std::uint32_t fromScript{0};
};

// A rider that asked to get out of `transport` (AIExitState: aiExit): out when its exit is next free, the carrier
// landing first if it must; `instant`: taken out at once, doors or not (removeAllContained).
struct ExitIntent
{
	ecs::Entity transport;
	std::uint32_t instant{0};
	std::uint32_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Transport>
{
	static constexpr std::string_view StableName = "engine.gameplay.transport";
	static constexpr std::uint32_t Version = 6;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Transport &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.definition.slots);
		hasher.AppendU64(value.definition.exitDelay);
		hasher.AppendU64(value.occupied);
		hasher.AppendU64(static_cast<std::uint64_t>(value.state) | (value.landing ? 0x100u : 0u) | (value.definition.unloadInAir ? 0x200u : 0u) | (value.takingOff ? 0x400u : 0u) | (value.seekingSpot ? 0x800u : 0u) | (value.closed ? 0x1000u : 0u) | (value.landRequested ? 0x2000u : 0u));
		hasher.AppendU64(static_cast<std::uint64_t>(value.landingSpot.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.landingSpot.y.Raw()));
		hasher.AppendU64(value.nextExitTick);
		hasher.AppendU64(static_cast<std::uint64_t>(value.cruiseHeight.Raw()));
		hasher.AppendU64((std::uint64_t{value.enteredBy} << 32) ^ value.enteredTick);
		hasher.AppendU64(static_cast<std::uint64_t>(value.definition.riderHeight.Raw()) ^ (value.definition.garrisonsRiders ? 1ull << 63 : 0ull) ^
			(value.definition.goAggressiveOnExit ? 1ull << 62 : 0ull));
	}
};

template<>
struct ComponentTraits<engine::gameplay::Passenger>
{
	static constexpr std::string_view StableName = "engine.gameplay.passenger";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Passenger &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.transport.index);
		hasher.AppendU64(value.transport.generation);
		hasher.AppendU64(value.slots);
		hasher.AppendU64(value.since);
	}
};

template<>
struct ComponentTraits<engine::gameplay::ExitIntent>
{
	static constexpr std::string_view StableName = "engine.gameplay.exit_intent";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::ExitIntent &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.transport.index);
		hasher.AppendU64(value.transport.generation);
		hasher.AppendU64(value.instant);
	}
};

template<>
struct ComponentTraits<engine::gameplay::Boarding>
{
	static constexpr std::string_view StableName = "engine.gameplay.boarding";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Boarding &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.transport.index);
		hasher.AppendU64(value.transport.generation);
		hasher.AppendU64(value.touchOnly);
	}
};
}
