export module games.generalszh.gameplay.ai.components.mob_member;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// A member of an angry mob (the original's MobMemberSlavedUpdate): its nexus (m_slaver, set as it is spawned: onEnslave),
// its count to its next look (every 16 ticks; a random 0..20 to start), the victim it remembers (the nexus's last), and
// how long it has been critically far from the nexus. Its rules per kind are MobMemberConfig (definition data).
export namespace generalszh::gameplay
{
struct MobMember
{
	ecs::Entity nexus;
	ecs::Entity primaryVictim;
	std::uint32_t framesToWait{0};
	std::uint32_t crisisTimer{0};
	std::uint8_t selfTasking{0};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};

// MustCatchUpRadius, NoNeedToCatchUpRadius, Squirrelliness (the share of the mob that may pick its own targets, at
// most 1), CatchUpCrisisBailTime (in its looks).
struct MobMemberConfig
{
	bool present{false};
	Engine::Math::Fixed mustCatchUpRadius;
	Engine::Math::Fixed noNeedToCatchUpRadius;
	Engine::Math::Fixed squirrelliness;
	std::uint32_t crisisBailTime{0};
};

// What the tick's looks decided for each member, carried out after the tick through its AI's orders (CMD_FROM_AI).
enum class MobOrder : std::uint8_t
{
	Move,     // aiMoveToPosition(at)
	Attack,   // aiAttackObject(target)
	Idle,     // aiIdle
	Kill,     // kill
	UseSet,   // chooseLocomotorSet(set)
};

struct MobEvent
{
	ecs::Entity member;
	MobOrder order{MobOrder::Move};
	std::uint8_t locomotorSet{0};
	ecs::Entity target;
	Engine::Math::FixedVector2 at;
};

struct MobEvents
{
	std::vector<MobEvent> list;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::MobMember>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.mob_member";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<generalszh::gameplay::MobEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.mob_events";
};
}
