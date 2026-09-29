export module games.generalszh.gameplay.battleplans.resources.battle_plan_cues;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;
export import games.generalszh.gameplay.battleplans.components.battle_plan;
import engine.ecs.system.system;

// What the tick's Strategy Centers gave the presentation (BattlePlanUpdate::setStatus): a plan unpacking (the player's
// battle plan radar event, the plan's message, announcement and unpack sound), active (search and destroy's idle loop),
// packing (the pack sound, the unpack sound and idle loop stopped) and packed (the pack sound stopped). The tick's only:
// cleared as each tick starts.
export namespace generalszh::gameplay
{
struct BattlePlanCue
{
	enum class Kind : std::uint8_t
	{
		Unpack,
		Active,
		Pack,
		Idle,
	};
	Kind kind{Kind::Unpack};
	PlanStatus plan{PlanStatus::None};
	std::uint8_t reserved[2]{};
	std::uint32_t definition{0};
	std::uint32_t player{0};
	ecs::Entity center;
	Engine::Math::FixedVector3 at;
};

struct BattlePlanCues
{
	std::vector<BattlePlanCue> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::BattlePlanCues>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.battle_plan_cues";
};
}
