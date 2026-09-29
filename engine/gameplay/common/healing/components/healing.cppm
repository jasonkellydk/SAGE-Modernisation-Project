export module engine.gameplay.common.healing.components.healing;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
import engine.ecs.core.component_registry;

// How entities heal (the original's AutoHealBehavior), as data:
// - SelfHealing: `amount` every `delay` ticks while hurt; after being hit it
//   waits `startDelay` ticks before healing again.
// - AreaHealing: up to four programs, each every `delay` ticks giving
//   `amount` to each hurt ally in `radius` (or every one of its player's,
//   when WholePlayer) whose target classes match `classes` and not
//   `forbiddenClasses`; SingleBurst heals once.
// - HealLock: whom an entity accepts area healing from until when (the
//   original's sole benefactor: heals from several healers do not stack).
export namespace engine::gameplay
{
struct SelfHealing
{
	Engine::Math::Fixed amount;
	std::uint64_t delay{1};
	std::uint64_t startDelay{0};
	std::uint64_t nextTick{0};
	// The disabled types it still heals under (its update module's getDisabledTypesToProcess); 0: held only.
	std::uint32_t runsWhileDisabled{0};
	// Waits while its structure is not standing whole (BaseRegenerateUpdate::update: under construction or being sold);
	// `waiting` is set while that is so.
	std::uint8_t onlyWhenStanding{0};
	std::uint8_t waiting{0};
	std::uint8_t padding[2]{};
};

namespace area_healing
{
inline constexpr std::uint32_t SkipSelf = 1u << 0;
inline constexpr std::uint32_t SingleBurst = 1u << 1;
inline constexpr std::uint32_t WholePlayer = 1u << 2;
inline constexpr std::uint32_t Spent = 1u << 3; // a single burst that went off
}

// One area heal an entity gives (the original allows several modules).
struct AreaHealProgram
{
	Engine::Math::Fixed amount;
	Engine::Math::Fixed radius;
	std::uint64_t delay{1};
	std::uint64_t nextTick{0};
	std::uint32_t classes{0xFFFFFFFFu};
	std::uint32_t forbiddenClasses{0};
	std::uint32_t flags{0};
	std::uint32_t reserved{0};
};

struct AreaHealing
{
	static constexpr std::uint32_t MaxPrograms = 4;
	std::array<AreaHealProgram, MaxPrograms> programs{};
	std::uint32_t count{0};
	std::uint32_t reserved{0};

	bool Add(const AreaHealProgram &program) noexcept
	{
		if (count >= MaxPrograms)
			return false;
		programs[count++] = program;
		return true;
	}
};

struct HealLock
{
	ecs::Entity healer;
	std::uint64_t until{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::SelfHealing>
{
	static constexpr std::string_view StableName = "engine.gameplay.self_healing";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
template<>
struct ComponentTraits<engine::gameplay::AreaHealing>
{
	static constexpr std::string_view StableName = "engine.gameplay.area_healing";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
template<>
struct ComponentTraits<engine::gameplay::HealLock>
{
	static constexpr std::string_view StableName = "engine.gameplay.heal_lock";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
