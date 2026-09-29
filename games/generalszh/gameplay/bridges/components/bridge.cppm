export module games.generalszh.gameplay.bridges.components.bridge;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// A bridge, on the object that stands for it (the original's TerrainLogic Bridge and its BridgeInfo, with the
// BridgeBehavior of that object): a map-drawn bridge's GenericBridge, or a landmark bridge (IsBridge) itself. Where it
// runs (from and to, its four corners at deck height), its Roads.ini template (by its index in the bridge catalog), its
// towers, and its damage: the state TerrainLogic last recorded (curDamageState), whether that last record was a change
// into or out of rubble (damageStateChanged) and the tick it was made (updateBridgeDamageStates: the scripts of the
// next tick see it), the body state its BridgeBehavior last heard (onBodyDamageStateChange), and the tick it died
// (m_deathFrame: 0, not dead), and its deck's layer. Simulation state: checkpointed.
export namespace generalszh::gameplay
{
// BodyDamageType.
namespace body_state
{
inline constexpr std::uint8_t Pristine = 0;
inline constexpr std::uint8_t Damaged = 1;
inline constexpr std::uint8_t ReallyDamaged = 2;
inline constexpr std::uint8_t Rubble = 3;
}

struct Bridge
{
	static constexpr std::uint64_t Never = ~std::uint64_t{0};
	Engine::Math::FixedVector3 from;
	Engine::Math::FixedVector3 to;
	Engine::Math::FixedVector3 fromLeft;
	Engine::Math::FixedVector3 fromRight;
	Engine::Math::FixedVector3 toLeft;
	Engine::Math::FixedVector3 toRight;
	Engine::Math::Fixed width;
	std::array<ecs::Entity, 4> towers{}; // BridgeTowerType: FROM_LEFT, FROM_RIGHT, TO_LEFT, TO_RIGHT
	std::uint64_t statesUpdatedTick{Never};
	std::uint64_t deathTick{0};
	std::uint32_t bridgeTemplate{0xFFFFFFFFu}; // its Roads.ini Bridge (catalog order); none: not found
	std::uint8_t curDamageState{body_state::Pristine};
	std::uint8_t bodyState{body_state::Pristine};
	std::uint8_t changed{0};
	std::uint8_t landmark{0};
	std::uint8_t layer{0}; // its deck's pathfinding layer (Bridge::m_layer; 0: none: the layers were all used)
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};

// A bridge's tower (BridgeTowerBehavior): its bridge and which corner it stands at.
struct BridgeTower
{
	ecs::Entity bridge;
	std::uint8_t type{0};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::Bridge>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.bridge";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ComponentTraits<generalszh::gameplay::BridgeTower>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.bridge_tower";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
