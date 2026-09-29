export module games.generalszh.gameplay.battleplans.systems.battle_plan_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import games.generalszh.gameplay.battleplans.components.battle_plan;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.rts.combat.components.turret;
import games.generalszh.content.objects.model_conditions;

// BattlePlanUpdate::update for every Strategy Center, chunk-parallel (not while disabled: an update module processes no
// disabled type). Once its ready tick comes: idle with a plan chosen, it unpacks that plan (its door opening for the
// plan's animation time); unpacked, the plan is active (its doors waiting to close; bombardment turns its turret on);
// active with another plan chosen, it packs (its doors closing for the animation time) - bombardment first waits on its
// turret at rest (idled, the turret off and sent back to rest); packed, it is idle again for TransitionIdleTime.
// setStatus's model conditions and turret are its own, written here; the plan given to (or taken from) its player's
// army, the paralysis and its AI idling go out as its chunk's BattlePlanEvents (ApplyBattlePlanEvents).
export namespace generalszh::gameplay
{
struct BattlePlanEvent
{
	enum class Kind : std::uint8_t
	{
		Unpack, // UNPACKING entered with `plan` (radar event, message, announcement, unpack sound)
		Active, // ACTIVE entered: setBattlePlan(`plan`)
		Pack,   // PACKING entered from `plan`: setBattlePlan(NONE) and the pack sound
		Idle,   // IDLE entered
		AiIdle, // bombardment waiting on its turret: aiIdle(CMD_FROM_AI)
	};
	ecs::Entity center;
	Kind kind{Kind::Unpack};
	PlanStatus plan{PlanStatus::None};
	std::uint8_t reserved[2]{};
};

struct BattlePlanEvents : ecs::ChunkOutputs<BattlePlanEvent>
{
};

namespace battle_plan_detail
{
// The door model conditions of each plan (DOOR_1 bombardment, DOOR_2 hold the line, DOOR_3 search and destroy).
inline std::uint32_t Door(PlanStatus plan, std::string_view stage)
{
	if (plan == PlanStatus::None)
		return 0;
	static const std::array<std::array<std::uint32_t, 3>, 3> bits = [] {
		std::array<std::array<std::uint32_t, 3>, 3> made{};
		for (std::size_t door = 0; door < 3; ++door)
		{
			const std::string prefix = "DOOR_" + std::to_string(door + 1);
			made[door] = {content::ModelConditionBit(prefix + "_OPENING"), content::ModelConditionBit(prefix + "_WAITING_TO_CLOSE"),
				content::ModelConditionBit(prefix + "_CLOSING")};
		}
		return made;
	}();
	const std::size_t door = static_cast<std::size_t>(plan) - 1;
	return stage == "OPENING" ? bits[door][0] : stage == "WAITING_TO_CLOSE" ? bits[door][1] : bits[door][2];
}
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::BattlePlanEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.battle_plan_events";
};
}

export namespace generalszh::gameplay
{
struct BattlePlanSystem
{
	using Query = ecs::Query<ecs::Write<BattlePlan>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::OptionalWrite<engine::gameplay::Appearance>,
		ecs::OptionalWrite<engine::gameplay::Turret>, ecs::Optional<engine::gameplay::Disabled>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Write<BattlePlanEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<BattlePlanEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using battle_plan_detail::Door;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		auto &events = context.Write<BattlePlanEvents>().Slot(context);
		auto plans = chunk.Get<BattlePlan>();
		const auto refs = chunk.Get<gp::DefinitionRef>();
		auto looks = chunk.Get<gp::Appearance>();
		auto turrets = chunk.Get<gp::Turret>();
		const auto disabled = chunk.Get<gp::Disabled>();
		const auto entities = chunk.Entities();
		const std::uint64_t now = context.Tick();
		for (std::size_t row = 0; row < plans.size(); ++row)
		{
			BattlePlan &plan = plans[row];
			if (!disabled.empty() && disabled[row].mask != 0)
				continue;
			const BattlePlanConfig *config = templates.BattlePlanOf(refs[row].index);
			if (config == nullptr || plan.nextReadyTick > now)
				continue;
			gp::Appearance *look = looks.empty() ? nullptr : &looks[row];
			gp::Turret *turret = turrets.empty() ? nullptr : &turrets[row];
			const auto set = [&](std::uint32_t bit, bool on) {
				if (look != nullptr && bit != 0)
					look->Set(bit, on);
			};
			// setStatus: the old status's condition off, the new one's on (and its timing).
			const auto enter = [&](PlanTransition status) {
				if (plan.status == status)
					return;
				switch (plan.status)
				{
				case PlanTransition::Unpacking: set(Door(plan.current, "OPENING"), false); break;
				case PlanTransition::Active: set(Door(plan.current, "WAITING_TO_CLOSE"), false); break;
				case PlanTransition::Packing: set(Door(plan.current, "CLOSING"), false); break;
				case PlanTransition::Idle: break;
				}
				switch (status)
				{
				case PlanTransition::Idle:
					events.push_back({entities[row], BattlePlanEvent::Kind::Idle, plan.current});
					plan.current = PlanStatus::None;
					plan.nextReadyTick = now + config->transitionIdleTicks;
					break;
				case PlanTransition::Unpacking:
					set(Door(plan.current, "OPENING"), true);
					if (plan.current != PlanStatus::None)
						plan.nextReadyTick = now + config->AnimationTicks(plan.current);
					events.push_back({entities[row], BattlePlanEvent::Kind::Unpack, plan.current});
					break;
				case PlanTransition::Active:
					events.push_back({entities[row], BattlePlanEvent::Kind::Active, plan.current});
					set(Door(plan.current, "WAITING_TO_CLOSE"), true);
					break;
				case PlanTransition::Packing:
					events.push_back({entities[row], BattlePlanEvent::Kind::Pack, plan.current});
					set(Door(plan.current, "CLOSING"), true);
					if (plan.current != PlanStatus::None)
						plan.nextReadyTick = now + config->AnimationTicks(plan.current);
					break;
				}
				plan.status = status;
			};
			switch (plan.status)
			{
			case PlanTransition::Idle:
				if (plan.desired != PlanStatus::None)
				{
					plan.current = plan.desired;
					enter(PlanTransition::Unpacking);
				}
				break;
			case PlanTransition::Unpacking:
				enter(PlanTransition::Active);
				if (plan.current == PlanStatus::Bombardment && turret != nullptr)
					turret->enabled = true;
				break;
			case PlanTransition::Active:
				if (plan.current == plan.desired)
					break;
				if (plan.current != PlanStatus::Bombardment)
				{
					enter(PlanTransition::Packing);
					break;
				}
				// Bombardment packs only with its turret at rest (isTurretInNaturalPosition; none: never).
				if (turret != nullptr && gp::TurretAtRest(*turret))
				{
					enter(PlanTransition::Packing);
					plan.centeringTurret = 0;
					turret->enabled = false;
				}
				else if (plan.centeringTurret == 0)
				{
					// aiIdle, the turret off (the fork's fix: it no longer re-aims while recentering) and recentered.
					events.push_back({entities[row], BattlePlanEvent::Kind::AiIdle, plan.current});
					if (turret != nullptr)
					{
						turret->enabled = false;
						turret->state = gp::TurretState::Recenter;
					}
					plan.centeringTurret = 1;
				}
				break;
			case PlanTransition::Packing:
				enter(PlanTransition::Idle);
				break;
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::BattlePlanSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.battle_plan";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
