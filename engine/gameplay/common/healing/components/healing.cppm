export module engine.gameplay.common.healing.components.healing;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
import engine.ecs.core.component_registry;

// How entities heal (the original's AutoHealBehavior), as data:
// - SelfHealing: up to four programs (an object may have several self-heal
//   modules: its AutoHealBehavior(s), the veterancy AutoHeal every object
//   inherits, and a structure's BaseRegenerateUpdate), each `amount` every
//   `delay` ticks while hurt; after being hit it waits `startDelay` ticks
//   before healing again.
// - AreaHealing: up to four programs, each every `delay` ticks giving
//   `amount` to each hurt ally in `radius` (or every one of its player's,
//   when WholePlayer) whose target classes match `classes` and not
//   `forbiddenClasses`; SingleBurst heals once.
// - HealLock: whom an entity accepts area healing from until when (the
//   original's sole benefactor: heals from several healers do not stack).
export namespace engine::gameplay
{
// One self heal an entity gives (one module).
struct SelfHealProgram
{
	Engine::Math::Fixed amount;
	std::uint64_t delay{1};
	std::uint64_t startDelay{0};
	std::uint64_t nextTick{0};
	// The disabled types it still heals under (its update module's getDisabledTypesToProcess); 0: held only.
	std::uint32_t runsWhileDisabled{0};
	// Waits while its structure is not standing whole (BaseRegenerateUpdate::update: under construction or being sold).
	std::uint8_t onlyWhenStanding{0};
	// An upgrade-triggered module not yet upgraded (AutoHealBehavior's UpgradeMux: GLA Junk Repair, the inherited
	// veterancy heal): it does nothing.
	std::uint8_t dormant{0};
	std::uint8_t padding[2]{};
};

struct SelfHealing
{
	static constexpr std::uint32_t MaxPrograms = 4;
	// AutoHealBehavior self heals in module order, then BaseRegenerateUpdate's.
	std::array<SelfHealProgram, MaxPrograms> programs{};
	std::uint32_t count{0};
	// Set while its structure is not standing whole: programs that only run while standing wait.
	std::uint8_t waiting{0};
	std::uint8_t padding[3]{};

	bool Add(const SelfHealProgram &program) noexcept
	{
		if (count >= MaxPrograms)
			return false;
		programs[count++] = program;
		return true;
	}
	bool WaitsWhileNotStanding() const noexcept
	{
		for (std::uint32_t index = 0; index < count; ++index)
			if (programs[index].onlyWhenStanding != 0)
				return true;
		return false;
	}
};

namespace area_healing
{
inline constexpr std::uint32_t SkipSelf = 1u << 0;
inline constexpr std::uint32_t SingleBurst = 1u << 1;
inline constexpr std::uint32_t WholePlayer = 1u << 2;
inline constexpr std::uint32_t Spent = 1u << 3; // a single burst that went off
inline constexpr std::uint32_t Dormant = 1u << 4; // upgrade-triggered, not yet upgraded: it does nothing
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
	static constexpr std::uint32_t Version = 3;
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
