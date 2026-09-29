export module engine.gameplay.rts.combat.systems.weapon_system;
import std;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.algorithms.attack_goal;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.construction.components.sale;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.combat.resources.shots;
export import engine.gameplay.rts.combat.systems.targeting_system;
export import engine.gameplay.rts.combat.components.turret;
export import engine.gameplay.rts.combat.resources.launch_layouts;
export import engine.gameplay.rts.combat.algorithms.projectile_arc;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.rts.combat.components.firing_tracker;
export import engine.gameplay.common.status.components.status_flags;
import Engine.Core.Math.FixedRandom;

// Fires armed entities at their targets, chunk-parallel: bodies without a
// turret turn to face the target first; a turret must be aligned with it
// (the turret system aims it) unless it fires while turning; a weapon fires once the target is
// within range and the weapon is ready, then waits its shot delay (random
// between the weapon's bounds) or its clip reload. Shots go out per chunk
// with the tick they land on (travel time at the weapon's speed). A
// projectile leaves from its barrel's launch point (turned with the turret);
// a lobbed one's shot lands with its projectile. Each shot goes to its FiringTracker (Object::... shotFired): its
// continuous fire speeds up or cools down by the shots in a row, the weapon's wait re-timed at once when that changes
// its rate of fire (Weapon::onWeaponBonusChange), and its looping fire sound kept going.
export namespace engine::gameplay
{
namespace weapon_detail
{
// WeaponSet::chooseBestWeaponForTarget (PREFER_MOST_DAMAGE): of the slots whose weapon may target the victim
// and would hurt it (an unresistable one may do none), the most damaging one that is ready; none ready, the most
// damaging one that is not (reloading, or its turret still turning onto the victim); ties go to the earlier slot.
// None fits: the slot in use stays.
inline std::uint8_t ChooseSlot(const WeaponSlots &set, const WeaponCatalog &weapons, const SpatialEntry &victim, std::uint64_t tick,
	const Turret *turret, const AltTurret *alt, std::uint32_t unresistable) noexcept
{
	// A locked weapon stays in hand.
	if (set.locked < WeaponSlotCount)
		return set.locked;
	bool found = false, foundBackup = false;
	Engine::Math::Fixed best, bestBackup;
	std::uint8_t decision = 0, backup = 0;
	for (int index = static_cast<int>(WeaponSlotCount) - 1; index >= 0; --index)
	{
		const WeaponSlot &slot = set.slots[static_cast<std::size_t>(index)];
		if (slot.weapon == WeaponCatalog::None || slot.readyTick == OutOfAmmo)
			continue;
		const WeaponDefinition &weapon = weapons.At(slot.weapon);
		if (!CanTarget(weapon, victim.classes))
			continue;
		const Engine::Math::Fixed damage = weapon.primaryDamage;
		if (damage <= Engine::Math::Fixed{} && weapon.damageType != unresistable)
			continue;
		bool ready = tick >= slot.readyTick;
		// isWeaponSlotOnTurretAndAimingAtTarget: its turret is still turning onto the victim.
		const Turret *aimer = slot.aim == SlotAim::Turret ? turret : slot.aim == SlotAim::AltTurret && alt != nullptr ? &alt->turret : nullptr;
		if (aimer != nullptr && aimer->state == TurretState::Aim && !aimer->aligned)
			ready = false;
		if (ready)
		{
			if (!found || damage >= best)
			{
				best = damage;
				decision = static_cast<std::uint8_t>(index);
				found = true;
			}
		}
		else if (!foundBackup || damage >= bestBackup)
		{
			bestBackup = damage;
			backup = static_cast<std::uint8_t>(index);
			foundBackup = true;
		}
	}
	return found ? decision : foundBackup ? backup : set.current;
}
}

struct WeaponSystem
{
	using Query = ecs::Query<ecs::Write<Armament>, ecs::Write<Transform>, ecs::Write<AttackTarget>, ecs::Read<Owner>, ecs::Optional<DefinitionRef>,
		ecs::Optional<Turret>, ecs::Optional<AltTurret>, ecs::OptionalWrite<WeaponSlots>, ecs::Optional<OffMap>, ecs::Optional<Disabled>,
		ecs::Optional<Targetable>, ecs::Optional<WeaponBonusConditions>, ecs::OptionalWrite<FiringTracker>, ecs::Exclude<UnderConstruction>,
		ecs::Exclude<Sale>>; // isAbleToAttack: not unbuilt or sold
	// What it shoots at is marked (FAERIE_FIRE) or not.
	using Lookup = ecs::Lookup<ecs::Read<StatusFlags>>;
	using Resources = ecs::Resources<ecs::Read<RandomSeed>, ecs::Read<SpatialIndex>, ecs::Read<WeaponCatalog>, ecs::Read<LaunchLayouts>,
		ecs::Write<FiredShots>, ecs::Write<TemporaryWeaponFires>>;

	// The tick's shots start empty, with one slot after the chunks' for the weapons fired on their own
	// (createAndFireTempWeapon -> Weapon::fireWeapon from the source at the spot: carried by its projectile when it
	// has one, else landing after its travel).
	void BeforeChunks(Query &query, ecs::SystemContext &context)
	{
		FiredShots &fired = context.Write<FiredShots>();
		const std::size_t chunks = query.PreparedChunkCount();
		fired.Reset(chunks + 1);
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const std::uint64_t tick = context.Tick();
		for (const TemporaryWeaponFire &fire : context.Write<TemporaryWeaponFires>().Take())
		{
			if (fire.weapon == WeaponCatalog::None)
				continue;
			const WeaponDefinition &weapon = weapons.At(fire.weapon);
			const Engine::Math::Fixed distance = Engine::Math::Length(fire.aim - fire.origin);
			const std::uint64_t travel = weapon.speed > Engine::Math::Fixed{} ? static_cast<std::uint64_t>((distance / weapon.speed).Ceil()) : 0;
			const Engine::Math::FixedVector2 toward = fire.aim.XY() - fire.origin.XY();
			Shot shot{fire.source, {}, fire.weapon, fire.sourcePlayer, fire.origin, fire.aim, tick,
				weapon.lobbed || weapon.guided || weapon.objectFlown ? LandsWithProjectile : tick + travel};
			shot.launchYaw = Engine::Math::Atan2(toward.y, toward.x);
			fired.SlotAt(chunks).push_back(shot);
		}
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const LaunchLayouts &layouts = context.Read<LaunchLayouts>();
		FiredShots &fired = context.Write<FiredShots>();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0xF12Eu;
		auto armaments = chunk.Get<Armament>();
		auto transforms = chunk.Get<Transform>();
		auto targets = chunk.Get<AttackTarget>();
		const auto owners = chunk.Get<Owner>();
		const auto turrets = chunk.Get<Turret>();
		const auto altTurrets = chunk.Get<AltTurret>();
		auto slotSets = chunk.Get<WeaponSlots>();
		const auto definitions = chunk.Get<DefinitionRef>();
		const auto offMap = chunk.Get<OffMap>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		auto &out = fired.Slot(context);
		const auto disabledRows = chunk.Get<Disabled>();
		const auto targetables = chunk.Get<Targetable>();
		const auto bonusRows = chunk.Get<WeaponBonusConditions>();
		auto trackers = chunk.Get<FiringTracker>();
		const auto lookup = context.Lookup<Lookup>();
		for (std::size_t row = 0; row < armaments.size(); ++row)
		{
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::Held))
				continue;
			Armament &armament = armaments[row];
			if (armament.weapon == WeaponCatalog::None || !Attacking(targets[row]))
				continue;
			// Carried, it fires only where its carrier lets it (Object::isAbleToAttack).
			if (!offMap.empty() && !offMap[row].armed)
				continue;
			const ecs::Entity shelter = offMap.empty() ? ecs::Entity{} : offMap[row].holder;
			SpatialEntry point;
			const SpatialEntry *target = AttackGoal(spatial, targets[row], point);
			if (target == nullptr)
				continue;
			const Turret *mainTurret = turrets.empty() ? nullptr : &turrets[row];
			const AltTurret *altTurret = altTurrets.empty() ? nullptr : &altTurrets[row];
			// A weapon set: the slot to fire at this victim this tick (the AI recomputes it every frame).
			const Turret *aimer = mainTurret;
			std::uint8_t slotIndex = 0;
			bool altAimed = false;
			if (!slotSets.empty())
			{
				WeaponSlots &set = slotSets[row];
				StoreSlot(set.slots[set.current], armament);
				set.current = weapon_detail::ChooseSlot(set, weapons, *target, tick, mainTurret, altTurret, weapons.unresistable);
				const WeaponSlot &chosen = set.slots[set.current];
				slotIndex = set.current;
				altAimed = chosen.aim == SlotAim::AltTurret && altTurret != nullptr;
				aimer = chosen.aim == SlotAim::Turret ? mainTurret : chosen.aim == SlotAim::AltTurret && altTurret != nullptr ? &altTurret->turret : nullptr;
				LoadSlot(armament, chosen, aimer != nullptr || (chosen.aim == SlotAim::Body && armament.turret && mainTurret == nullptr));
				if (armament.weapon == WeaponCatalog::None)
					continue;
			}
			const WeaponDefinition &weapon = weapons.At(armament.weapon);
			// Weapon::computeBonus: its conditions pick the global and the weapon's own bonus rows.
			const WeaponBonus bonus = weapons.Bonus(weapon, (bonusRows.empty() ? 0u : bonusRows[row].Effective()) |
				(trackers.empty() ? 0u : ContinuousFireConditions(trackers[row], weapons)));
			Transform &transform = transforms[row];
			const Engine::Math::FixedVector2 toTarget = target->position.XY() - transform.position.XY();
			const Engine::Math::Fixed distance = Engine::Math::Length(toTarget);
			const Engine::Math::Fixed radius = targetables.empty() ? Engine::Math::Fixed{} : targetables[row].radius;
			if (!WithinAttackRange(BonusAttackRange(weapon.attackRange, bonus), transform.position.XY(), radius, *target) ||
				TooCloseToAttack(weapon.minimumRange, transform.position.XY(), radius, *target))
				continue;
			if (!armament.turret)
			{
				const std::int32_t off = Engine::Math::DeltaTo(transform.facing, Engine::Math::Heading(toTarget));
				const std::int64_t magnitude = std::llabs(static_cast<std::int64_t>(off));
				if (magnitude > static_cast<std::int64_t>(weapon.aimDelta.units))
				{
					const auto limit = static_cast<std::int64_t>(armament.turnRate.units);
					const std::int64_t turn = limit == 0 ? off : std::clamp<std::int64_t>(off, -limit, limit);
					transform.facing += Engine::Math::TurnAngle{static_cast<std::uint32_t>(turn)};
					if (magnitude - std::llabs(turn) > static_cast<std::int64_t>(weapon.aimDelta.units))
						continue;
				}
			}
			if (armament.turret && aimer != nullptr && !aimer->fireReady && !aimer->definition.firesWhileTurning)
				continue;
			if (tick < armament.readyTick)
				continue;
			// Weapon::preFireWeapon, as the fire state starts (AIAttackFireWeaponState::onEnter, the frame before its
			// first update, which is when a shot without a wind-up goes): it fires PreAttackDelay (scaled by its
			// PRE_ATTACK bonus) frames after that, if it keeps at it (getStatus: PRE_ATTACK until then). A wind-up comes
			// before every shot, or only a full clip's first, or only the first at this victim (getPreAttackDelay).
			const ecs::Entity victim = targets[row].target;
			const bool windingUp = armament.preAttackUntil != 0 && armament.preAttackSeen + 1 == tick && armament.preAttackVictim == victim;
			if (!windingUp)
			{
				armament.preAttackUntil = 0;
				const bool fullClip = weapon.clipSize == 0 || armament.clip == 0 || armament.clip >= weapon.clipSize;
				const bool skip = (weapon.preAttackType == WeaponDefinition::PreAttack::PerClip && !fullClip) ||
					(weapon.preAttackType == WeaponDefinition::PreAttack::PerAttack && armament.lastVictim == victim);
				const auto windUp = skip ? 0 : static_cast<std::uint64_t>(
					(Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(weapon.preAttackDelay)) * bonus.Get(WeaponBonusField::PreAttack)).Floor());
				if (windUp > 1)
				{
					armament.preAttackUntil = tick - 1 + windUp;
					armament.preAttackVictim = victim;
				}
			}
			if (armament.preAttackUntil != 0 && tick < armament.preAttackUntil)
			{
				armament.preAttackSeen = tick;
				continue;
			}
			armament.preAttackUntil = 0;
			armament.lastVictim = victim;

			// As Weapon::privateFireWeapon: wrap at the barrel count; this barrel fires.
			const std::uint8_t barrel = armament.barrel >= armament.barrels ? std::uint8_t{0} : armament.barrel;
			Engine::Math::FixedVector3 origin = transform.position;
			if (weapon.projectile)
			{
				const bool turreted = armament.turret && aimer != nullptr;
				origin = LaunchPosition(definitions.empty() ? nullptr : layouts.Of(definitions[row].index), barrel, turreted, turreted ? aimer->angle : Engine::Math::TurnAngle{},
					turreted ? aimer->pitch : Engine::Math::TurnAngle{}, transform, slotIndex, altAimed);
			}
			// A lobbed shot's projectile carries it (it lands where that detonates); others land after their travel.
			const std::uint64_t travel = weapon.speed > Engine::Math::Fixed{} ? static_cast<std::uint64_t>((distance / weapon.speed).Ceil()) : 0;
			const bool turned = armament.turret && aimer != nullptr;
			out.push_back({entities[row], targets[row].target, armament.weapon, owners[row].player, origin, target->position, tick,
				weapon.lobbed || weapon.guided || weapon.objectFlown ? LandsWithProjectile : tick + travel, transform.facing + (turned ? aimer->angle : Engine::Math::TurnAngle{}),
				turned ? aimer->pitch : Engine::Math::TurnAngle{}, ecs::Entity{}, slotIndex, {}, shelter, 0, bonus.Get(WeaponBonusField::Damage),
				bonus.Get(WeaponBonusField::Radius)});

			auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation});
			const std::uint64_t spread = weapon.delayMax > weapon.delayMin ? weapon.delayMax - weapon.delayMin : 0;
			const std::uint64_t delay = weapon.delayMin +
				(spread == 0 ? 0 : static_cast<std::uint64_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(spread))));
			// getDelayBetweenShots: divided by the rate-of-fire bonus.
			armament.readyTick = tick + std::max<std::uint64_t>(BonusDelay(delay, bonus), 1);
			armament.firedTick = tick;
			// A limited attack counts the shot; its last ends it.
			CountShot(targets[row]);
			// Then move to the next.
			armament.firedBarrel = barrel;
			armament.barrel = static_cast<std::uint8_t>(barrel + 1);
			armament.reloading = false;
			if (weapon.clipSize > 0)
			{
				if (armament.clip == 0 || armament.clip > weapon.clipSize)
					armament.clip = weapon.clipSize;
				if (--armament.clip == 0)
				{
					if (weapon.reloadsAtBase)
					{
						// Out of ammo until it reloads at base (its airfield refills it).
						armament.readyTick = OutOfAmmo;
						armament.reloading = true;
					}
					else
					{
						armament.clip = weapon.clipSize;
						armament.readyTick = tick + std::max<std::uint64_t>(BonusDelay(weapon.clipReload, bonus), 1);
						armament.reloading = true;
					}
					// Object::fireCurrentWeapon: reloaded, it lets go of a temporary lock.
					if (!slotSets.empty())
						ReleaseTemporaryLock(slotSets[row]);
				}
			}
			if (!trackers.empty())
			{
				FiringTracker &tracker = trackers[row];
				const std::uint8_t level = tracker.level, faerie = tracker.faerie;
				// A victim marked with FAERIE_FIRE: TARGET_FAERIE_FIRE (a ground shot, or an unmarked victim, clears it).
				const StatusFlags *marks = weapons.faerieFireStatus < 64 && lookup.IsAlive(victim) ? lookup.Get<StatusFlags>(victim) : nullptr;
				tracker.faerie = marks != nullptr && (marks->bits & (std::uint64_t{1} << weapons.faerieFireStatus)) != 0 ? 1 : 0;
				ShotFired(tracker, weapon, armament.weapon, victim, armament.readyTick, tick);
				// Weapon::onWeaponBonusChange: a changed rate of fire re-times the wait from now (a new pick between shots).
				if (tracker.level != level || tracker.faerie != faerie)
					Retime(armament, weapon, weapons.Bonus(weapon, (bonusRows.empty() ? 0u : bonusRows[row].Effective()) |
						ContinuousFireConditions(tracker, weapons)), tick, random);
			}
		}
	}

	// The weapon bonus conditions its continuous fire gives.
	static std::uint32_t ContinuousFireConditions(const FiringTracker &tracker, const WeaponCatalog &weapons) noexcept
	{
		return (tracker.level == 1 ? weapons.continuousFireMean : tracker.level == 2 ? weapons.continuousFireFast : 0u) |
			(tracker.faerie != 0 ? weapons.targetFaerieFire : 0u);
	}

	// Weapon::onWeaponBonusChange: reloading its clip or between shots, its wait starts over from now at the new rate.
	static void Retime(Armament &armament, const WeaponDefinition &weapon, const WeaponBonus &bonus, std::uint64_t tick, Engine::Math::RandomStream &random)
	{
		if (armament.readyTick == OutOfAmmo || armament.readyTick <= tick)
			return;
		if (armament.reloading)
			armament.readyTick = tick + BonusDelay(weapon.clipReload, bonus);
		else
		{
			const std::uint64_t spread = weapon.delayMax > weapon.delayMin ? weapon.delayMax - weapon.delayMin : 0;
			const std::uint64_t delay = weapon.delayMin +
				(spread == 0 ? 0 : static_cast<std::uint64_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(spread))));
			armament.readyTick = tick + BonusDelay(delay, bonus);
		}
	}

	// FiringTracker::shotFired: the same victim (or another within the coast) counts on, else the count starts over;
	// the coast runs from the weapon's next possible shot; MEAN cools down under ContinuousFireOne shots and speeds up
	// past ContinuousFireTwo, FAST cools down under ContinuousFireTwo, none speeds up past ContinuousFireOne; a weapon
	// with a looping fire sound keeps it going FireSoundLoopTime past this shot (another weapon ends it).
	static void ShotFired(FiringTracker &tracker, const WeaponDefinition &weapon, std::uint32_t weaponIndex, ecs::Entity victim, std::uint64_t nextShot,
		std::uint64_t tick) noexcept
	{
		// Each shot pushes back the idle reload.
		if (weapon.autoReloadIdleTicks > 0)
			tracker.reloadTick = tick + weapon.autoReloadIdleTicks;
		if (victim == tracker.victim)
			++tracker.shots;
		else if (tick < tracker.cooldownTick)
		{
			++tracker.shots;
			tracker.victim = victim;
		}
		else
		{
			tracker.shots = 1;
			tracker.victim = victim;
		}
		tracker.cooldownTick = weapon.continuousFireCoast == 0 ? 0
			: nextShot == OutOfAmmo ? OutOfAmmo : nextShot + weapon.continuousFireCoast;
		const std::uint32_t one = weapon.continuousFireOne, two = weapon.continuousFireTwo;
		if (tracker.level == 1)
		{
			if (tracker.shots < one)
				CoolDown(tracker);
			else if (tracker.shots > two)
				SpeedUp(tracker);
		}
		else if (tracker.level == 2)
		{
			if (tracker.shots < two)
				CoolDown(tracker);
		}
		else if (tracker.shots > one)
			SpeedUp(tracker);
		if (weapon.fireSoundLoopTicks != 0)
		{
			tracker.loopWeapon = weaponIndex;
			tracker.loopUntil = tick + weapon.fireSoundLoopTicks;
		}
		else
			tracker.loopUntil = 0;
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::WeaponSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.weapons";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::MovementSystem, engine::gameplay::TargetingSystem>;
};
}
