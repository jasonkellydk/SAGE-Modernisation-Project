export module engine.gameplay.rts.stealth.systems.stealth_system;
import std;
export import engine.gameplay.rts.containment.components.garrison;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.common.weapons.components.weapon_slots;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.gameplay.rts.stealth.components.stealth;
export import engine.gameplay.rts.stealth.components.stealth_rider;
export import engine.gameplay.rts.stealth.resources.detections;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.combat.components.aggression;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.common.status.components.script_status;

// Stealth each tick, in parallel per chunk, before the spatial index (StealthUpdate::update): an entity whose update is
// on (not asleep awaiting a grant, not a disguiser without a disguise) steps its disguise transition (the look swaps at
// its halfway point; a finished reveal turns it off with its stealth); a disguiser within RevealDistanceFromTarget of its
// victim is marked detected (losing its disguise) and goes no further this tick; otherwise one that may stealth by its
// owner's rules (its own, or its rider's: UseRiderStealth) and has kept clear of their forbidden conditions (attacking,
// firing, moving faster than its threshold, being hit, using an ability, riding in a transport) for their delay is
// stealthed; the rest are not and wait the delay again. Hidden ones (stealthed, not detected) are marked in their target
// classes, so the tick's queries skip them as targets; a disguiser only while disguised, and only for those who do not
// count its disguise's player their enemy (target_class::Disguised).
export namespace engine::gameplay
{
namespace stealth_detail
{
// The stealth's marks in its target classes (a cache of its status for the tick's queries).
inline void MarkClasses(const Stealth &stealth, Targetable &targetable) noexcept
{
	const bool disguiser = stealth.Option(stealth_option::DisguisesAsTeam);
	const bool hidden = stealth.Hidden();
	auto &classes = targetable.classes;
	classes = hidden && !disguiser ? (classes | target_class::Hidden) : (classes & ~target_class::Hidden);
	classes = hidden && disguiser && stealth.IsDisguised() ? (classes | target_class::Disguised) : (classes & ~target_class::Disguised);
	classes = stealth.Has(stealth_flag::Stealthed) ? (classes | target_class::Stealthed) : (classes & ~target_class::Stealthed);
	targetable.disguisePlayer = stealth.disguisePlayer;
	targetable.disguiseTeam = stealth.disguiseTeam;
}

// calcStealthOwner's rules: its first rider's (a rider with a StealthUpdate), else its own.
inline StealthRules RulesOf(const Stealth &stealth, const StealthRider *rider) noexcept
{
	if (rider == nullptr || !rider->Has(stealth_rider_flag::Rider))
		return OwnRules(stealth);
	StealthRules rules = OwnRules(stealth);
	if (rider->Has(stealth_rider_flag::RiderStealth))
	{
		rules.delay = rider->delay;
		rules.forbidden = rider->forbidden;
		rules.orderIdleEnemies = rider->Has(stealth_rider_flag::OrderIdleEnemies);
	}
	rules.canStealth = rider->Has(stealth_rider_flag::CanStealth);
	return rules;
}

inline std::uint32_t DefaultTeamOf(const TeamRoster &roster, std::uint32_t player)
{
	return roster.DefaultTeam(player).value_or(Relationships::NoTeam);
}
}

// calcStealthOwner, each tick before the stealth itself: a UseRiderStealth container's first rider's rules.
struct StealthRiderSystem
{
	using Query = ecs::Query<ecs::Write<StealthRider>>;
	using Lookup = ecs::Lookup<ecs::Read<Stealth>>;
	using Resources = ecs::Resources<ecs::Read<CargoManifest>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const CargoManifest &manifest = context.Read<CargoManifest>();
		const auto lookup = context.Lookup<Lookup>();
		auto riders = chunk.Get<StealthRider>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < riders.size(); ++row)
		{
			StealthRider found{};
			const auto aboard = manifest.Aboard(entities[row]);
			if (!aboard.empty())
			{
				found.flags |= stealth_rider_flag::Rider;
				if (const Stealth *theirs = lookup.IsAlive(aboard.front()) ? lookup.Get<Stealth>(aboard.front()) : nullptr)
				{
					found.flags |= stealth_rider_flag::RiderStealth;
					found.delay = theirs->delay;
					found.forbidden = theirs->forbidden;
					if (theirs->Has(stealth_flag::CanStealth))
						found.flags |= stealth_rider_flag::CanStealth;
					if (theirs->Option(stealth_option::OrderIdleEnemies))
						found.flags |= stealth_rider_flag::OrderIdleEnemies;
				}
			}
			riders[row] = found;
		}
	}
};

struct StealthSystem
{
	using Query = ecs::Query<ecs::Write<Stealth>, ecs::OptionalWrite<Targetable>, ecs::Optional<Health>, ecs::Optional<Armament>,
		ecs::Optional<AttackTarget>, ecs::Optional<Locomotion>, ecs::Optional<OffMap>, ecs::Optional<Disabled>, ecs::Optional<AiActivity>,
		ecs::Optional<ScriptStatus>, ecs::Optional<Transform>, ecs::Optional<Owner>, ecs::Optional<StealthRider>, ecs::Optional<WeaponSlots>,
		ecs::Optional<Passenger>, ecs::Optional<Transport>>;
	using Lookup = ecs::Lookup<ecs::Read<Transform>, ecs::Read<Garrison>, ecs::Read<AttackTarget>>;
	using Resources = ecs::Resources<ecs::Write<RevealWakes>, ecs::Write<DisguiseEvents>, ecs::Read<TeamRoster>, ecs::Read<CargoManifest>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const
	{
		context.Write<RevealWakes>().Reset(query.PreparedChunkCount());
		context.Write<DisguiseEvents>().Reset(query.PreparedChunkCount());
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using namespace stealth_detail;
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
		const auto transforms = chunk.Get<Transform>();
		const auto owners = chunk.Get<Owner>();
		const auto riderRows = chunk.Get<StealthRider>();
		const auto slotRows = chunk.Get<WeaponSlots>();
		const auto passengers = chunk.Get<Passenger>();
		const auto transports = chunk.Get<Transport>();
		const CargoManifest &manifest = context.Read<CargoManifest>();
		const auto entities = chunk.Entities();
		const auto lookup = context.Lookup<Lookup>();
		auto &wakes = context.Write<RevealWakes>().Slot(context);
		auto &events = context.Write<DisguiseEvents>().Slot(context);
		const TeamRoster &roster = context.Read<TeamRoster>();
		for (std::size_t row = 0; row < stealths.size(); ++row)
		{
			// Still called while held (getDisabledTypesToProcess: DISABLED_HELD).
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::Held))
				continue;
			Stealth &stealth = stealths[row];
			// Asleep till granted (GrantedBySpecialPower), or off (a disguiser without a disguise): nothing.
			if (stealth.Has(stealth_flag::Asleep) || stealth.Has(stealth_flag::Off))
				continue;
			StealthRules rules = RulesOf(stealth, riderRows.empty() ? nullptr : &riderRows[row]);
			const auto position = transforms.empty() ? Engine::Math::FixedVector3{} : transforms[row].position;
			const ecs::Entity victim = !targets.empty() && targets[row].atPosition == 0 && lookup.IsAlive(targets[row].target) ? targets[row].target : ecs::Entity{};
			const auto finish = [&] {
				if (!targetables.empty())
					MarkClasses(stealth, targetables[row]);
			};
			// A disguise transition (gaining or losing the look): the look swaps at its halfway point; a reveal finished
			// turns it off with its stealth and detection.
			bool ended = false;
			if (const DisguiseChange change = StepDisguiseTransition(stealth, ended); change != DisguiseChange::None)
				events.push_back({entities[row], position, static_cast<std::uint8_t>(change == DisguiseChange::Disguised ? 1 : 0),
					static_cast<std::uint8_t>(change == DisguiseChange::Revealed && victim != ecs::Entity{} ? 1 : 0)});
			if (ended)
			{
				finish();
				continue;
			}
			// RevealDistanceFromTarget: within it of its victim (centres, 2D), it is revealed and goes no further this tick.
			if (stealth.revealDistance > Engine::Math::Fixed{} && victim != ecs::Entity{})
				if (const Transform *at = lookup.Get<Transform>(victim);
					at != nullptr && Engine::Math::DistanceSquared(at->position.XY(), position.XY()) <= stealth.revealDistance * stealth.revealDistance)
				{
					if (MarkAsDetected(stealth, rules, tick, 0))
					{
						const std::uint32_t player = owners.empty() ? 0u : owners[row].player;
						wakes.push_back({entities[row], position, player, DefaultTeamOf(roster, player)});
					}
					finish();
					continue;
				}
			// A temporary grant runs down; an order from its player ends it at once (no exploits), as does its end.
			if (stealth.framesGranted > 0)
			{
				--stealth.framesGranted;
				if ((!activityRows.empty() && activityRows[row].fromPlayer != 0) || stealth.framesGranted == 0)
				{
					RevokeGrant(stealth);
					rules = RulesOf(stealth, riderRows.empty() ? nullptr : &riderRows[row]);
				}
			}
			const bool dead = !healths.empty() && IsDead(healths[row]);
			// Inside something, only a garrison lets it stay stealthed (contain->isGarrisonable()).
			const bool garrisoned = riding && !passengers.empty() && lookup.Get<Garrison>(passengers[row].transport) != nullptr;
			bool allowed = rules.canStealth && !dead && (!riding || garrisoned);
			if (allowed && (rules.forbidden & stealth_forbidden::Attacking) != 0 && !targets.empty() && targets[row].target.IsValid())
				allowed = false;
			// STEALTH_NOT_WHILE_FIRING_*: all three, any shot last frame or this; else only a shot of a named slot (its weapon's
			// getLastShotFrame, the current slot's on its armament).
			if (allowed && (rules.forbidden & stealth_forbidden::FiringAny) != 0 && !armaments.empty())
			{
				const auto firedIn = [&](std::uint8_t slot) {
					const bool current = slotRows.empty() ? slot == 0 : slotRows[row].current == slot;
					const std::uint64_t fired = current ? armaments[row].firedTick : slotRows.empty() ? 0u : slotRows[row].slots[slot].firedTick;
					return fired != 0 && fired + 1 >= tick;
				};
				const bool firingAny = firedIn(0) || firedIn(1) || firedIn(2);
				if (firingAny && (rules.forbidden & stealth_forbidden::FiringAny) == stealth_forbidden::FiringAny)
					allowed = false;
				else if (firingAny)
					for (std::uint8_t slot = 0; slot < 3 && allowed; ++slot)
						if ((rules.forbidden & (stealth_forbidden::FiringPrimary << slot)) != 0 && firedIn(slot))
							allowed = false;
			}
			// STEALTH_NOT_WHILE_RIDERS_ATTACKING: letting its riders fire, any of them attacking (isAnyRiderAttacking).
			if (allowed && (rules.forbidden & stealth_forbidden::RidersAttacking) != 0 && !transports.empty() && transports[row].definition.passengersFire)
				for (const ecs::Entity rider : manifest.Aboard(entities[row]))
					if (const AttackTarget *attack = lookup.Get<AttackTarget>(rider); attack != nullptr && attack->target.IsValid())
						allowed = false;
			if (allowed && (rules.forbidden & stealth_forbidden::Moving) != 0 && !motions.empty() && motions[row].speed > stealth.moveThreshold)
				allowed = false;
			if (allowed && (rules.forbidden & stealth_forbidden::TakingDamage) != 0 && !healths.empty() && healths[row].lastDamageTick != 0 &&
				healths[row].lastDamageTick + 1 >= tick)
				allowed = false;
			// STEALTH_NOT_WHILE_USING_ABILITY: OBJECT_STATUS_IS_USING_ABILITY (from its preparation on).
			const bool usingAbility = !activityRows.empty() && activityRows[row].usingAbility != 0;
			if (allowed && (rules.forbidden & stealth_forbidden::UsingAbility) != 0 && usingAbility)
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
				stealth.allowedAt = tick + rules.delay;
				stealth.Set(stealth_flag::Stealthed, false);
			}
			// hintDetectableWhileUnstealthed: kept out of stealth while firing or using an ability (its hint conditions), its
			// player sees it flash.
			const bool firing = !armaments.empty() && armaments[row].firedTick != 0 && armaments[row].firedTick + 1 >= tick;
			stealth.Set(stealth_flag::HintDetectable, !allowed && (((stealth.hint & stealth_hint::FiringWeapon) != 0 && firing) ||
				((stealth.hint & stealth_hint::UsingAbility) != 0 && usingAbility)));
			stealth.Set(stealth_flag::Detected, stealth.detectedUntil > tick);
			finish();
		}
	}
};

// setWakeupIfInRange for this tick's reveals (StealthUpdate::markAsDetected with OrderIdleEnemiesToAttackMeUponReveal):
// every thing with an AI whose player counts the revealed one's player's default team its enemy, and that is within its
// vision range of it (in three dimensions), looks for a target at once if it is idle
// (AIUpdateInterface::wakeUpAndAttemptToTarget: its next mood check now). `Wakes` is the reveals' stream: RevealWakes
// (StealthSystem) or DetectionWakes (the detectors' reveal pass). A batch system: reveals are rare, so it does nothing
// on most ticks and walks its query only when there are some.
template<class Wakes>
struct WakeIdleEnemiesSystem
{
	using Query = ecs::Query<ecs::Write<Aggression>, ecs::Read<Transform>, ecs::Read<Owner>, ecs::Optional<MoveOrder>, ecs::Optional<AttackTarget>,
		ecs::Optional<AiActivity>>;
	using Resources = ecs::Resources<ecs::Read<Wakes>, ecs::Read<Relationships>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const Wakes &wakes = context.Read<Wakes>();
		if (wakes.Size() == 0)
			return;
		const Relationships &relationships = context.Read<Relationships>();
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto aggressions = chunk.template Get<Aggression>();
			const auto transforms = chunk.template Get<Transform>();
			const auto owners = chunk.template Get<Owner>();
			const auto orders = chunk.template Get<MoveOrder>();
			const auto targets = chunk.template Get<AttackTarget>();
			const auto activities = chunk.template Get<AiActivity>();
			for (std::size_t row = 0; row < aggressions.size(); ++row)
			{
				// isIdle: standing (no move), attacking nothing, not busy.
				const bool idle = (orders.empty() || orders[row].mode == MoveMode::Idle) && (targets.empty() || !targets[row].target.IsValid()) &&
					(activities.empty() || activities[row].busy == 0);
				if (!idle)
					continue;
				Aggression &aggression = aggressions[row];
				const Engine::Math::Fixed vision = aggression.vision;
				wakes.ForEach([&](const StealthReveal &reveal) {
					if (relationships.Between(Relationships::NoTeam, owners[row].player, reveal.team, reveal.player) != Relationship::Enemies)
						return;
					const Engine::Math::FixedVector3 apart = transforms[row].position - reveal.position;
					if (apart.x * apart.x + apart.y * apart.y + apart.z * apart.z > vision * vision)
						return;
					WakeToTarget(aggression, tick);
				});
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::StealthRiderSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.stealth_rider";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<engine::gameplay::StealthSystem>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<engine::gameplay::StealthSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.stealth";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	// The composition orders it before its spatial index, so the tick's queries know who is hidden.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<engine::gameplay::WakeIdleEnemiesSystem<engine::gameplay::RevealWakes>>
{
	static constexpr std::string_view StableName = "engine.gameplay.wake_idle_enemies_on_reveal";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::StealthSystem>;
};
}
