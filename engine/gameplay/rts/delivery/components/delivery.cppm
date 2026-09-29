export module engine.gameplay.rts.delivery.components.delivery;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// A payload run (the original's DeliverPayloadAIUpdate and its DeliverPayloadStateMachine): fly to `moveTo`; once
// within the delivery distance of `target` (further inbound by the pre-open distance) open the doors (DOOR_1_OPENING)
// and after the door delay drop a rider every drop delay (at the drop's offset and variance) while still close enough;
// out of reach with riders left, carry on past (twice the turn radius and a bit) and come round again, at most
// `maxAttempts` times, brought back to the map's edge if it strays off; done, fly straight on off the map (or pop
// away: `selfDestruct`) and go. Counts are in ticks, distances in world units.
export namespace engine::gameplay
{
enum class DeliveryPhase : std::uint8_t
{
	Approach,
	Delivering,
	ConsiderNewApproach,
	RecoverFromOffMap,
	HeadOffMap,
	CleanUp,
};

struct Delivery
{
	static constexpr std::uint32_t NoBit = 0xFFFFFFFFu;

	Engine::Math::FixedVector2 target;
	Engine::Math::FixedVector2 moveTo;
	Engine::Math::FixedVector3 dropOffset;
	Engine::Math::FixedVector3 dropVariance;
	Engine::Math::FixedVector2 exit;           // where it heads off the map
	Engine::Math::FixedVector2 deliveredFacing; // its facing as it headed off (unit vector)
	Engine::Math::Fixed distance;              // DeliveryDistance
	Engine::Math::Fixed preOpen;               // PreOpenDistance
	Engine::Math::Fixed previousDistance;      // squared, at the last closeness test (m_previousDistanceSqr)
	std::uint64_t reEntryTick{0};
	std::uint32_t doorDelay{0};
	std::uint32_t dropDelay{0};
	std::uint32_t dropDelayLeft{0};
	std::int32_t maxAttempts{1};
	std::int32_t attempts{0}; // entries into ConsiderNewApproach
	std::uint32_t doorOpening{NoBit}; // its DOOR_1_OPENING / DOOR_1_CLOSING model conditions
	std::uint32_t doorClosing{NoBit};
	DeliveryPhase phase{DeliveryPhase::Approach};
	std::uint8_t entered{0};           // the phase's onEnter has run
	std::uint8_t parachuteDirectly{0};
	std::uint8_t selfDestruct{0};
	std::uint8_t reserved[8]{};
};

// A delivery done (CleanUpState, or popped away): the session removes the carrier.
struct DeliveryDone
{
	ecs::Entity carrier;
};

using DeliveriesDone = ecs::ChunkOutputs<DeliveryDone>;
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Delivery>
{
	static constexpr std::string_view StableName = "engine.gameplay.delivery";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<engine::gameplay::DeliveriesDone>
{
	static constexpr std::string_view StableName = "engine.gameplay.deliveries_done";
};
}
