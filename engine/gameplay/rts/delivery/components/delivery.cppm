export module engine.gameplay.rts.delivery.components.delivery;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
import engine.ecs.system.system;

// A payload run (the original's DeliverPayloadAIUpdate and its DeliverPayloadStateMachine): fly to `moveTo`; once
// within the delivery distance of `target` (further inbound by the pre-open distance) open the doors (DOOR_1_OPENING)
// and after the door delay drop a rider every drop delay (at the drop's offset and variance; FireWeapon: fire its
// current weapon at the target instead and destroy the rider) and its visible payload items (VisibleNumBones, so many
// a drop) while still close enough; out of reach with any left, carry on past (twice the turn radius and a bit) and
// come round again, at most `maxAttempts` times, brought back to the map's edge if it strays off; done, fly straight on
// off the map (or pop away: `selfDestruct`) and go. Whatever its state, within DiveStartDistance (2D) of its target it
// dives (PRECISE_Z_POS) until within DiveEndDistance (3D), firing its StrafingWeaponSlot at a strafe point every tick of
// the dive. Counts are in ticks, distances in world units.
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

// DeliverPayloadAIUpdate's m_diveState (DIVESTATE_PREDIVE, DIVESTATE_DIVING, DIVESTATE_POSTDIVE).
enum class DiveState : std::uint8_t
{
	PreDive,
	Diving,
	PostDive,
};

struct Delivery
{
	static constexpr std::uint32_t NoBit = 0xFFFFFFFFu;
	static constexpr std::uint32_t NoRun = 0xFFFFFFFFu;
	static constexpr std::uint32_t NoEffect = 0xFFFFFFFFu;
	static constexpr std::uint8_t NoSlot = 0xFF;

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
	// m_targetPos.z (the target's height), and the height of the spot its current move heads for (its goal's z: what
	// PRECISE_Z_POS holds it at).
	Engine::Math::Fixed targetHeight;
	Engine::Math::Fixed goalHeight;
	// Where it was as the last tick's run began (its velocity: how far it went since).
	Engine::Math::FixedVector3 lastPosition;
	// DiveStartDistance and DiveEndDistance.
	Engine::Math::Fixed diveStart;
	Engine::Math::Fixed diveEnd;
	std::uint32_t strafeEffect{NoEffect}; // StrafeWeaponFX (the game's effect id)
	std::int32_t visibleBones{0};         // VisibleNumBones: the visible payload items it carries
	std::int32_t visibleDelivered{0};     // m_visibleItemsDelivered
	std::int32_t visiblePerDrop{0};       // VisibleItemsDroppedPerInterval
	std::uint32_t visibleRun{NoRun};      // the game's visible payload (what each item is and how it goes; NoRun: none made)
	DiveState dive{DiveState::PostDive};
	std::uint8_t strafeSlot{NoSlot};      // StrafingWeaponSlot (NoSlot: none)
	std::uint8_t fireWeapon{0};           // FireWeapon
	std::uint8_t inheritVelocity{0};      // InheritTransportVelocity (a rider dropped takes its velocity as a force)
};

// A visible payload item let go this tick (DeliveringState::update's VisibleItemsDroppedPerInterval path), for the game
// to make: its run's `index`th item (from 1: its bone), from the carrier as it stood when it let go (its position,
// facing and velocity), for the carrier's target (and its move's goal).
struct VisibleDrop
{
	ecs::Entity carrier;
	Engine::Math::FixedVector3 at;
	Engine::Math::FixedVector3 velocity;
	Engine::Math::FixedVector3 target;
	Engine::Math::FixedVector2 moveTo;
	Engine::Math::TurnAngle facing;
	std::uint32_t run{Delivery::NoRun};
	std::int32_t index{0};
	std::uint32_t reserved{0};
};

using VisibleDrops = ecs::ChunkOutputs<VisibleDrop>;

// What a run shows and plays this tick, for presentation: its StartDive sound as the dive starts (on the carrier, of
// its definition), its StrafeWeaponFX at each strafe point (doFXPos).
struct DeliveryCue
{
	enum class Kind : std::uint8_t
	{
		StartDive,
		Strafe,
	};
	ecs::Entity carrier;
	Engine::Math::FixedVector3 at;
	std::uint32_t definition{0};
	std::uint32_t effect{Delivery::NoEffect};
	Kind kind{Kind::StartDive};
};

struct DeliveryCues : ecs::ChunkOutputs<DeliveryCue>
{
};

// A delivery done (CleanUpState, or popped away), or a rider fired off as a weapon (FireWeapon: destroyObject): the
// session removes it.
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
	static constexpr std::uint32_t Version = 3;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<engine::gameplay::DeliveriesDone>
{
	static constexpr std::string_view StableName = "engine.gameplay.deliveries_done";
};

template<>
struct ResourceTraits<engine::gameplay::VisibleDrops>
{
	static constexpr std::string_view StableName = "engine.gameplay.visible_drops";
};

template<>
struct ResourceTraits<engine::gameplay::DeliveryCues>
{
	static constexpr std::string_view StableName = "engine.gameplay.delivery_cues";
};
}
