export module engine.gameplay.rts.combat.components.firing_tracker;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

// An armed entity's FiringTracker (the original's object helper): whom it keeps firing at and how many shots in a row
// (a victim switch within the coast still counts on), its continuous fire (0 none, 1 CONTINUOUS_FIRE_MEAN, 2
// CONTINUOUS_FIRE_FAST; `slow`: spinning down, CONTINUOUS_FIRE_SLOW), whether its last shot was at a thing marked with
// FAERIE_FIRE (TARGET_FAERIE_FIRE), the tick past which it cools down (0: never), and
// its looping fire sound: the weapon whose sound loops and the tick it stops (0: none), and the tick its weapons load
// their clips if it has not fired again (AutoReloadWhenIdle; 0: none). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct FiringTracker
{
	std::uint64_t cooldownTick{0};
	std::uint64_t loopUntil{0};
	std::uint64_t reloadTick{0};
	ecs::Entity victim;
	std::uint32_t shots{0};
	std::uint32_t loopWeapon{0xFFFFFFFFu};
	std::uint8_t level{0};
	std::uint8_t slow{0};
	std::uint8_t faerie{0};
	std::uint8_t reserved[5]{}; // no padding: checkpoints hold its bytes
};

// FiringTracker::speedUp: none to MEAN, MEAN to FAST (the one that says VoiceRapidFire); FAST stays.
inline void SpeedUp(FiringTracker &tracker) noexcept
{
	if (tracker.level < 2)
		++tracker.level;
	tracker.slow = 0;
}

// FiringTracker::coolDown: from MEAN or FAST straight to none, spinning down (SLOW); from none, SLOW off and no more
// cooling down. Either way the count starts over.
inline void CoolDown(FiringTracker &tracker) noexcept
{
	if (tracker.level != 0)
	{
		tracker.level = 0;
		tracker.slow = 1;
	}
	else
	{
		tracker.slow = 0;
		tracker.cooldownTick = 0;
	}
	tracker.shots = 0;
	tracker.victim = {};
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::FiringTracker>
{
	static constexpr std::string_view StableName = "engine.gameplay.firing_tracker";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
