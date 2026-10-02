export module games.generalszh.gameplay.battleplans.components.battle_plan;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
export import games.generalszh.content.objects.kind_of;
export import engine.gameplay.common.health.algorithms.max_health;

// A Strategy Center's battle plans (BattlePlanUpdate): where it stands in opening a plan's doors, holding the plan and
// closing them again (m_status), the plan it shows (m_currentPlan), the one its player last chose (m_desiredPlan), the
// one its player's army has from it (m_planAffectingArmy), the tick it may move on (m_nextReadyFrame) and whether it
// waits on its turret swinging back to rest before packing up (m_centeringTurret). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
// BattlePlanStatus.
enum class PlanStatus : std::uint8_t
{
	None,
	Bombardment,
	HoldTheLine,
	SearchAndDestroy,
};

// TransitionStatus.
enum class PlanTransition : std::uint8_t
{
	Idle,
	Unpacking,
	Active,
	Packing,
};

struct BattlePlan
{
	std::uint64_t nextReadyTick{0};
	PlanTransition status{PlanTransition::Idle};
	PlanStatus current{PlanStatus::None};
	PlanStatus desired{PlanStatus::None};
	PlanStatus affecting{PlanStatus::None};
	std::uint8_t centeringTurret{0};
	std::uint8_t reserved[3]{}; // no padding: checkpoints hold its bytes
};

// Each definition's BattlePlanUpdate module data (present or not): its special power (a SpecialPowers index), each
// plan's animation time and TransitionIdleTime (ticks), the troops a plan reaches (ValidMemberKindOf, less
// InvalidMemberKindOf), BattlePlanChangeParalyzeTime (ticks), the army's scalars (HoldTheLinePlanArmorDamageScalar,
// SearchAndDestroyPlanSightRangeScalar) and the center's own (StrategyCenterSearchAndDestroySightRangeScalar,
// StrategyCenterSearchAndDestroyDetectsStealth, StrategyCenterHoldTheLineMaxHealthScalar and ...ChangeType). The sounds,
// announcements and messages are the presentation's (read from the module by name).
struct BattlePlanConfig
{
	static constexpr std::uint32_t NoPower = 0xFFFFFFFFu;
	bool present{false};
	std::uint32_t power{NoPower};
	std::array<std::uint32_t, 3> animationTicks{}; // Bombardment, HoldTheLine, SearchAndDestroy
	std::uint32_t transitionIdleTicks{0};
	std::uint32_t paralyzeTicks{0};
	content::KindOfMask valid{};
	content::KindOfMask invalid{};
	Engine::Math::Fixed holdTheLineArmorScalar{Engine::Math::Fixed::One()};
	Engine::Math::Fixed searchAndDestroySightScalar{Engine::Math::Fixed::One()};
	Engine::Math::Fixed centerSightScalar{Engine::Math::Fixed::One()};
	Engine::Math::Fixed centerMaxHealthScalar{Engine::Math::Fixed::One()};
	engine::gameplay::MaxHealthChange centerMaxHealthChange{engine::gameplay::MaxHealthChange::PreserveRatio};
	bool centerDetectsStealth{true};

	std::uint32_t AnimationTicks(PlanStatus plan) const noexcept
	{
		return plan == PlanStatus::None ? 0u : animationTicks[static_cast<std::size_t>(plan) - 1];
	}
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::BattlePlan>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.battle_plan";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
