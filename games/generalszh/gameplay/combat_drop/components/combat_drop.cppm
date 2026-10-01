export module games.generalszh.gameplay.combat_drop.components.combat_drop;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
import engine.ecs.system.system;

// A Chinook's combat drop (ChinookAIUpdate: MOVE_TO_COMBAT_DROP then DO_COMBAT_DROP), as data.
//   CombatDrop, on the transport: its goal (an object whose position it follows, or a spot), the stage, the hover it
//     wants over the goal (ChinookMoveToBldgState: its locomotor's preferred height raised to clear a building by
//     MinDropHeight) and the height it had before, and its ropes while it drops (ChinookCombatDropState's RopeInfo):
//     where each hangs from and where its rappellers start down it (its model's RopeStart / RopeEnd bones, placed once
//     as the drop starts), how far out it is and how fast it unrolls, and the tick the next rappeller may go down it.
//   OnRope, on a rappeller let down a rope: whose rope (the transport, the rope). The transport counts a rope in use while
//     someone on it is alive and above the ground (removeDoneRappellers).
//   Rappel, on a rappeller (AIRappelState): what it drops onto (a building to go into, or the ground), the height it
//     stops at, and how fast it may come down (m_rappelRate).
//   FallingRope: a rope let go as its drop ended (ChinookCombatDropState::onExit: setRopeSpeed and an expiration date),
//     for the presentation, until its entity goes.
// RopesInUse (the ropes someone is still on, this tick) and RappelLandings (rappellers down this tick, their landings
// applied after the step) pass between the systems.
// Simulation state: checkpointed.
export namespace generalszh::gameplay
{
enum class CombatDropStage : std::uint8_t
{
	TakingOff, // landed when told: up first (ChinookAIUpdate::aiDoCommand: TAKING_OFF, the drop pending)
	Moving,    // MOVE_TO_COMBAT_DROP
	Dropping,  // DO_COMBAT_DROP
};

struct DropRope
{
	Engine::Math::FixedVector3 top;    // the rope's drawable (RopeStart), in the world
	Engine::Math::FixedVector3 dropAt; // dropStartTransform (RopeEnd), in the world
	Engine::Math::TurnAngle dropFacing;
	std::uint32_t reserved{0};
	Engine::Math::Fixed length;    // ropeLen
	Engine::Math::Fixed speed;     // ropeSpeed
	Engine::Math::Fixed lengthMax; // ropeLenMax
	std::uint64_t nextDrop{0};     // nextDropTime
};

struct CombatDrop
{
	static constexpr std::size_t MaxRopes = 32; // the bone arrays of ChinookCombatDropState::onEnter
	ecs::Entity target;                // none: a spot
	Engine::Math::FixedVector2 goal;
	Engine::Math::Fixed destZ;         // m_destZ: the ground under the goal plus the hover wanted
	Engine::Math::Fixed oldHeight;     // m_oldPreferredHeight
	Engine::Math::Fixed newHeight;     // m_newPreferredHeight
	CombatDropStage stage{CombatDropStage::TakingOff};
	std::uint8_t ropeCount{0};
	std::uint8_t reserved[6]{};
	std::array<DropRope, MaxRopes> ropes{};
};

struct OnRope
{
	ecs::Entity transport;
	std::uint32_t rope{0};
	std::uint32_t reserved{0};
};

struct Rappel
{
	ecs::Entity building;          // a building it drops into (none: the ground)
	Engine::Math::Fixed destZ;     // m_destZ
	Engine::Math::Fixed rate;      // m_rappelRate (down: negative)
};

struct FallingRope
{
	Engine::Math::FixedVector3 top;
	Engine::Math::Fixed length;
	Engine::Math::Fixed lengthMax; // its segments' count (initRopeParms)
	std::uint64_t fallFrom{0};     // the tick it let go
	std::uint32_t definition{0};   // the transport's (its rope's look)
	std::uint32_t reserved{0};
};

struct RopeInUse
{
	ecs::Entity transport;
	std::uint32_t rope{0};
};

struct RopesInUse : ecs::ChunkOutputs<RopeInUse>
{
};

struct RappelLanding
{
	ecs::Entity rappeller;
	ecs::Entity building; // none: it came down on the ground
};

struct RappelLandings : ecs::ChunkOutputs<RappelLanding>
{
};
}

export namespace ecs
{
namespace combat_drop_detail
{
inline void Hash(StateHasher &hasher, const Engine::Math::FixedVector3 &at) noexcept
{
	hasher.AppendU64(static_cast<std::uint64_t>(at.x.Raw()));
	hasher.AppendU64(static_cast<std::uint64_t>(at.y.Raw()));
	hasher.AppendU64(static_cast<std::uint64_t>(at.z.Raw()));
}
inline void Hash(StateHasher &hasher, ecs::Entity entity) noexcept { hasher.AppendU64((std::uint64_t{entity.index} << 32) | entity.generation); }
}

template<>
struct ComponentTraits<generalszh::gameplay::CombatDrop>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.combat_drop";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::CombatDrop &value, StateHasher &hasher) noexcept
	{
		combat_drop_detail::Hash(hasher, value.target);
		hasher.AppendU64(static_cast<std::uint64_t>(value.goal.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.goal.y.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.destZ.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.oldHeight.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.newHeight.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.stage) | (std::uint64_t{value.ropeCount} << 8));
		for (std::size_t index = 0; index < value.ropeCount && index < value.ropes.size(); ++index)
		{
			const auto &rope = value.ropes[index];
			combat_drop_detail::Hash(hasher, rope.top);
			combat_drop_detail::Hash(hasher, rope.dropAt);
			hasher.AppendU64(rope.dropFacing.units);
			hasher.AppendU64(static_cast<std::uint64_t>(rope.length.Raw()));
			hasher.AppendU64(static_cast<std::uint64_t>(rope.speed.Raw()));
			hasher.AppendU64(static_cast<std::uint64_t>(rope.lengthMax.Raw()));
			hasher.AppendU64(rope.nextDrop);
		}
	}
};
template<>
struct ComponentTraits<generalszh::gameplay::OnRope>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.on_rope";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::OnRope &value, StateHasher &hasher) noexcept
	{
		combat_drop_detail::Hash(hasher, value.transport);
		hasher.AppendU64(value.rope);
	}
};
template<>
struct ComponentTraits<generalszh::gameplay::Rappel>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.rappel";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::Rappel &value, StateHasher &hasher) noexcept
	{
		combat_drop_detail::Hash(hasher, value.building);
		hasher.AppendU64(static_cast<std::uint64_t>(value.destZ.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.rate.Raw()));
	}
};
template<>
struct ComponentTraits<generalszh::gameplay::FallingRope>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.falling_rope";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::FallingRope &value, StateHasher &hasher) noexcept
	{
		combat_drop_detail::Hash(hasher, value.top);
		hasher.AppendU64(static_cast<std::uint64_t>(value.length.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.lengthMax.Raw()));
		hasher.AppendU64(value.fallFrom);
		hasher.AppendU64(value.definition);
	}
};
template<>
struct ResourceTraits<generalszh::gameplay::RopesInUse>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.ropes_in_use";
};
template<>
struct ResourceTraits<generalszh::gameplay::RappelLandings>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.rappel_landings";
};
}
