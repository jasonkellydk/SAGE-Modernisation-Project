export module engine.gameplay.rts.stealth.systems.stealth_system;
import std;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.gameplay.rts.stealth.components.stealth;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.common.status.components.script_status;

// Stealth each tick, in parallel per chunk, before the spatial index: an
// entity that may stealth and has kept clear of its forbidden conditions
// (attacking, firing, moving faster than its threshold, being hit, using an
// ability, riding in a transport) for its delay is stealthed; the rest are not and wait
// their delay again. Hidden ones (stealthed, not detected) are marked in
// their target classes, so the tick's queries skip them as targets.
export namespace engine::gameplay
{
struct StealthSystem
{
	using Query = ecs::Query<ecs::Write<Stealth>, ecs::OptionalWrite<Targetable>, ecs::Optional<Health>, ecs::Optional<Armament>,
		ecs::Optional<AttackTarget>, ecs::Optional<Locomotion>, ecs::Optional<OffMap>, ecs::Optional<Disabled>, ecs::Optional<AiActivity>, ecs::Optional<ScriptStatus>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const std::uint64_t tick = context.Tick();
		auto stealths = chunk.Get<Stealth>();
		auto targetables = chunk.Get<Targetable>();
		const auto healths = chunk.Get<Health>();
		const auto armaments = chunk.Get<Armament>();
		const auto targets = chunk.Get<AttackTarget>();
		const auto motions = chunk.Get<Locomotion>();
		const bool riding = !chunk.Get<OffMap>().empty();
		const auto disabledRows = chunk.Get<Disabled>();
		const auto activityRows = chunk.Get<AiActivity>();
		const auto scriptRows = chunk.Get<ScriptStatus>();
		for (std::size_t row = 0; row < stealths.size(); ++row)
		{
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::Held))
				continue;
			Stealth &stealth = stealths[row];
			const bool dead = !healths.empty() && IsDead(healths[row]);
			bool allowed = stealth.Has(stealth_flag::CanStealth) && !dead && !riding;
			if (allowed && (stealth.forbidden & stealth_forbidden::Attacking) != 0 && !targets.empty() && targets[row].target.IsValid())
				allowed = false;
			if (allowed && (stealth.forbidden & stealth_forbidden::FiringAny) != 0 && !armaments.empty() && armaments[row].firedTick != 0 &&
				armaments[row].firedTick + 1 >= tick)
				allowed = false;
			if (allowed && (stealth.forbidden & stealth_forbidden::Moving) != 0 && !motions.empty() && motions[row].speed > stealth.moveThreshold)
				allowed = false;
			if (allowed && (stealth.forbidden & stealth_forbidden::TakingDamage) != 0 && !healths.empty() && healths[row].lastDamageTick != 0 &&
				healths[row].lastDamageTick + 1 >= tick)
				allowed = false;
			// STEALTH_NOT_WHILE_USING_ABILITY: OBJECT_STATUS_IS_USING_ABILITY (from its preparation on).
			const bool usingAbility = !activityRows.empty() && activityRows[row].usingAbility != 0;
			if (allowed && (stealth.forbidden & stealth_forbidden::UsingAbility) != 0 && usingAbility)
				allowed = false;
			// OBJECT_STATUS_SCRIPT_UNSTEALTHED: a script disabled its stealth.
			if (allowed && !scriptRows.empty() && scriptRows[row].Has(script_status::Unstealthed))
				allowed = false;
			if (allowed)
			{
				if (tick >= stealth.allowedAt)
					stealth.Set(stealth_flag::Stealthed, true);
			}
			else
			{
				stealth.allowedAt = tick + stealth.delay;
				stealth.Set(stealth_flag::Stealthed, false);
			}
			// hintDetectableWhileUnstealthed: kept out of stealth while firing or using an ability (its hint conditions), its
			// player sees it flash.
			const bool firing = !armaments.empty() && armaments[row].firedTick != 0 && armaments[row].firedTick + 1 >= tick;
			stealth.Set(stealth_flag::HintDetectable, !allowed && (((stealth.hint & stealth_hint::FiringWeapon) != 0 && firing) ||
				((stealth.hint & stealth_hint::UsingAbility) != 0 && usingAbility)));
			stealth.Set(stealth_flag::Detected, stealth.detectedUntil > tick);
			if (!targetables.empty())
			{
				auto &classes = targetables[row].classes;
				classes = stealth.Hidden() ? (classes | target_class::Hidden) : (classes & ~target_class::Hidden);
				classes = stealth.Has(stealth_flag::Stealthed) ? (classes | target_class::Stealthed) : (classes & ~target_class::Stealthed);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::StealthSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.stealth";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	// The composition orders it before its spatial index, so the tick's queries know who is hidden.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
