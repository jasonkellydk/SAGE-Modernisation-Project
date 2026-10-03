export module engine.gameplay.rts.combat.systems.weapon_system;
import std;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.sneaky_target;
export import engine.gameplay.rts.combat.algorithms.attack_goal;
export import engine.gameplay.rts.combat.algorithms.target_pitch;
export import engine.gameplay.rts.combat.algorithms.weapon_fitness;
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
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.rts.veterancy.components.experience;
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
// What chooseBestWeaponForTarget weighs about the attack: who ordered it, the firer's weapon bonus conditions, and for an
// object victim its fitness (none: a spot on the ground) and, its set pitch limited, both bodies.
struct SlotChoice
{
	CommandSource source{CommandSource::Ai};
	std::uint32_t conditions{0};
	const VictimFitness *victim{nullptr};
	const PitchBody *from{nullptr};
	const PitchBody *body{nullptr};
};

// WeaponSet::chooseBestWeaponForTarget (PREFER_MOST_DAMAGE): a locked weapon stays in hand; at a spot on the ground, the
// PRIMARY. Else of the slots its command source may pick (AutoChooseSources: retail tests the mask against
// CMD_DEFAULT_SWITCH_WEAPON's value, 4, which is FROM_AI's bit, so a slot FROM_AI may pick any source may), not out of
// ammo for good, whose weapon may target the victim, pitch to it (isWithinTargetPitch) and is reckoned to hurt it
// (estimateWeaponDamage with the firer's bonus; an unresistable one may do none): the most damaging ready one; none
// ready, the most damaging one that is not (reloading, or its turret still turning onto the victim); ties to the
// earlier slot. A slot preferred against the victim (PreferredAgainst: it is every kind named) is the most damaging
// there is, and ready unless out of ammo. None fits: the PRIMARY.
inline std::uint8_t ChooseSlot(const WeaponSlots &set, const WeaponCatalog &weapons, const ArmorCatalog &armors, const SpatialEntry &victim,
	std::uint64_t tick, const Turret *turret, const AltTurret *alt, const SlotChoice &choice) noexcept
{
	if (set.locked < WeaponSlotCount)
		return set.locked;
	if (choice.victim == nullptr)
		return 0;
	constexpr std::uint8_t FromAiBit = 1u << static_cast<std::uint8_t>(CommandSource::Ai);
	const Engine::Math::Fixed huge = Engine::Math::Fixed::FromRaw(std::numeric_limits<std::int64_t>::max());
	bool found = false, foundBackup = false;
	Engine::Math::Fixed best, bestBackup;
	std::uint8_t decision = 0, backup = 0;
	for (int index = static_cast<int>(WeaponSlotCount) - 1; index >= 0; --index)
	{
		const WeaponSlot &slot = set.slots[static_cast<std::size_t>(index)];
		if (slot.weapon == WeaponCatalog::None)
			continue;
		if ((slot.sources >> static_cast<std::uint8_t>(choice.source) & 1u) == 0 && (slot.sources & FromAiBit) == 0)
			continue;
		if (slot.readyTick == OutOfAmmo)
			continue;
		const WeaponDefinition &weapon = weapons.At(slot.weapon);
		if (!CanTarget(weapon, victim.classes))
			continue;
		if (choice.from != nullptr && choice.body != nullptr && !WithinTargetPitch(weapon, *choice.from, *choice.body))
			continue;
		Engine::Math::Fixed damage = EstimateWeaponDamage(weapon, weapon.primaryDamage * weapons.Bonus(weapon, choice.conditions).Get(WeaponBonusField::Damage),
			*choice.victim, weapons, armors);
		bool ready = tick >= slot.readyTick;
		// isWeaponSlotOnTurretAndAimingAtTarget: its turret is still turning onto the victim.
		const Turret *aimer = slot.aim == SlotAim::Turret ? turret : slot.aim == SlotAim::AltTurret && alt != nullptr ? &alt->turret : nullptr;
		if (aimer != nullptr && aimer->state == TurretState::Aim && !aimer->aligned)
			ready = false;
		if (damage <= Engine::Math::Fixed{} && weapon.damageType != weapons.unresistable)
			continue;
		if (slot.preferred != 0 && (victim.classes & slot.preferred) == slot.preferred)
		{
			damage = huge;
			ready = true;
		}
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
	return found ? decision : foundBackup ? backup : std::uint8_t{0};
}
}

struct WeaponSystem
{
	// AIAttackAimAtTargetState::update's REL_THRESH: 0.035f radians (the binary32 0x3D0F5C29), the least aim delta.
	static constexpr Engine::Math::TurnAngle RelThresh = Engine::Math::TurnFromRadiansBinary32Bits(0x3D0F5C29u);

	using Query = ecs::Query<ecs::Write<Armament>, ecs::Write<Transform>, ecs::Write<AttackTarget>, ecs::Read<Owner>, ecs::Optional<DefinitionRef>,
		ecs::Optional<Turret>, ecs::Optional<AltTurret>, ecs::OptionalWrite<WeaponSlots>, ecs::Optional<OffMap>, ecs::Optional<Disabled>,
		ecs::Optional<Targetable>, ecs::Optional<WeaponBonusConditions>, ecs::OptionalWrite<FiringTracker>, ecs::Optional<BodyExtent>, ecs::Optional<Experience>, ecs::Exclude<UnderConstruction>,
		ecs::Exclude<Sale>>; // isAbleToAttack: not unbuilt or sold
	// What it shoots at is marked (FAERIE_FIRE) or not.
	// (And how tall its victim stands, for a weapon's pitch limits.)
	using Lookup = ecs::Lookup<ecs::Read<StatusFlags>, ecs::Read<BodyExtent>, ecs::Read<Health>, ecs::Read<Subdual>, ecs::Read<UnderConstruction>, ecs::Read<Experience>,
		ecs::Read<SneakyTarget>, ecs::Read<Locomotion>>;
	using Resources = ecs::Resources<ecs::Read<RandomSeed>, ecs::Read<SpatialIndex>, ecs::Read<WeaponCatalog>, ecs::Read<LaunchLayouts>,
		ecs::Write<FiredShots>, ecs::Write<TemporaryWeaponFires>, ecs::Write<ScriptShots>, ecs::Read<GroundHeight>, ecs::Read<ArmorCatalog>, ecs::Write<Disarms>,
		ecs::Read<DirectShots>>;

	// The tick's shots start empty, with one slot after the chunks' for the shots fired straight from their weapons by
	// behaviours earlier in the tick (DirectShots) and the weapons fired on their own
	// (createAndFireTempWeapon -> Weapon::fireWeapon from the source at the spot: carried by its projectile when it
	// has one, else landing after its travel).
	void BeforeChunks(Query &query, ecs::SystemContext &context)
	{
		FiredShots &fired = context.Write<FiredShots>();
		const std::size_t chunks = query.PreparedChunkCount();
		fired.Reset(chunks + 1);
		context.Write<Disarms>().Reset(chunks);
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const std::uint64_t tick = context.Tick();
		const auto lookup = context.Lookup<Lookup>();
		// The shots behaviours fired from their objects' own weapons earlier this tick (DirectShots), as they were.
		context.Read<DirectShots>().ForEach([&](const Shot &shot) { fired.SlotAt(chunks).push_back(shot); });
		// The shots scripts fired before the systems (their projectiles already out).
		for (const Shot &shot : context.Write<ScriptShots>().Take())
			fired.SlotAt(chunks).push_back(shot);
		for (const TemporaryWeaponFire &fire : context.Write<TemporaryWeaponFires>().Take())
		{
			if (fire.weapon == WeaponCatalog::None)
				continue;
			const WeaponDefinition &weapon = weapons.At(fire.weapon);
			const std::uint64_t travel = weapon.laser && !weapon.projectile ? 0 : HitDelayTicks(fire.origin, fire.aim, weapon.speed);
			const Engine::Math::FixedVector2 toward = fire.aim.XY() - fire.origin.XY();
			Shot shot{fire.source, {}, fire.weapon, fire.sourcePlayer, fire.origin, fire.aim, tick,
				weapon.lobbed || weapon.guided || weapon.objectFlown ? LandsWithProjectile : tick + travel};
			shot.launchYaw = Engine::Math::Atan2(toward.y, toward.x);
			// Dealt as it is fired in the original (its source there still): its affects flags hold though it lands a tick later.
			shot.sourceHeld = travel == 0 ? 1 : 0;
			// (createAndFireTempWeapon: fired by its source, at its veterancy.)
			if (const Experience *experience = lookup.IsAlive(fire.source) ? lookup.Get<Experience>(fire.source) : nullptr)
				shot.veterancy = experience->level;
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
		auto &disarms = context.Write<Disarms>().Slot(context);
		const auto disabledRows = chunk.Get<Disabled>();
		const auto targetables = chunk.Get<Targetable>();
		const auto bonusRows = chunk.Get<WeaponBonusConditions>();
		auto trackers = chunk.Get<FiringTracker>();
		const auto extents = chunk.Get<BodyExtent>();
		const auto experiences = chunk.Get<Experience>();
		const auto lookup = context.Lookup<Lookup>();
		for (std::size_t row = 0; row < armaments.size(); ++row)
		{
			// The lock an attack asked for as it was given (AttackTarget::lockSlot: setWeaponLock LOCKED_TEMPORARILY).
			if (targets[row].lockSlot != 0)
			{
				if (!slotSets.empty())
					LockSlotTemporarily(slotSets[row], armaments[row], static_cast<std::uint8_t>(targets[row].lockSlot - 1));
				targets[row].lockSlot = 0;
			}
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::Held))
				continue;
			Armament &armament = armaments[row];
			if (armament.weapon == WeaponCatalog::None || !Attacking(targets[row]))
				continue;
			// Carried, it fires only where its carrier lets it (Object::isAbleToAttack).
			if (!offMap.empty() && !offMap[row].armed)
				continue;
			const ecs::Entity shelter = offMap.empty() ? ecs::Entity{} : offMap[row].holder;
			const Turret *mainTurret = turrets.empty() ? nullptr : &turrets[row];
			// Its first approach's temporary target, while the turret of its current weapon is on it (TurretAI's FIRE state,
			// AIAttackFireWeaponState on the turret's own machine: it shoots at the turret's goal, not counted against the
			// attack's shots): this tick's victim.
			const bool temporaryAim = mainTurret != nullptr && mainTurret->onTemporary && targets[row].temporary != ecs::Entity{};
			AttackTarget temporaryAttack;
			temporaryAttack.target = targets[row].temporary;
			const AttackTarget &attack = temporaryAim ? temporaryAttack : targets[row];
			SpatialEntry point;
			const SpatialEntry *target = AttackGoal(spatial, attack, point);
			if (target == nullptr)
				continue;
			const AltTurret *altTurret = altTurrets.empty() ? nullptr : &altTurrets[row];
			// A weapon set: the slot to fire at this victim this tick (the AI recomputes it every frame).
			const Turret *aimer = mainTurret;
			std::uint8_t slotIndex = 0;
			bool altAimed = false;
			if (!slotSets.empty())
			{
				WeaponSlots &set = slotSets[row];
				StoreSlot(set.slots[set.current], armament);
				weapon_detail::SlotChoice choice{targets[row].source, bonusRows.empty() ? 0u : bonusRows[row].Effective()};
				std::optional<VictimFitness> fitness;
				std::optional<PitchBody> from, body;
				if (attack.atPosition == 0)
				{
					fitness = FitnessOf(*target, lookup, true);
					choice.victim = &*fitness;
					if (lookup.IsAlive(target->entity) && AnyPitchLimited(weapons, armament, &set))
					{
						from = PitchBodyOf(transforms[row].position, extents.empty() ? nullptr : &extents[row]);
						body = PitchBodyOf(target->position, lookup.Get<BodyExtent>(target->entity));
						choice.from = &*from;
						choice.body = &*body;
					}
				}
				set.current = weapon_detail::ChooseSlot(set, weapons, context.Read<ArmorCatalog>(), *target, tick, mainTurret, altTurret, choice);
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
			const Engine::Math::Fixed radius = targetables.empty() ? Engine::Math::Fixed{} : targetables[row].radius;
			// A leech range weapon that has fired or wound up in this attack reaches any distance (hasLeechRange).
			const std::uint8_t slotBit = static_cast<std::uint8_t>(1u << slotIndex);
			const bool leeching = (attack.leech & slotBit) != 0;
			if (!leeching && (!WithinAttackRange(BonusAttackRange(weapon.attackRange, bonus), transform.position.XY(), radius, *target) ||
				TooCloseToAttack(weapon.minimumRange, transform.position.XY(), radius, *target)))
				continue;
			if (!armament.turret)
			{
				// AIAttackAimAtTargetState::update: the body turns while it is more than AcceptableAimDelta off, a delta
				// floored at REL_THRESH (0.035f rad, about 2 degrees).
				const std::int64_t tolerance = std::max<std::int64_t>(weapon.aimDelta.units, RelThresh.units);
				const std::int32_t off = Engine::Math::DeltaTo(transform.facing, Engine::Math::Heading(toTarget));
				const std::int64_t magnitude = std::llabs(static_cast<std::int64_t>(off));
				if (magnitude > tolerance)
				{
					// m_canTurnInPlace (its locomotor's MinSpeed 0): else it does not turn here but flies at its victim
					// (setLocomotorGoalPositionExplicit: the targeting gives that goal) until it faces it.
					if (const Locomotion *own = lookup.Get<Locomotion>(entities[row]); own != nullptr && own->locomotor.minSpeed > Engine::Math::Fixed{})
						continue;
					const auto limit = static_cast<std::int64_t>(armament.turnRate.units);
					const std::int64_t turn = limit == 0 ? off : std::clamp<std::int64_t>(off, -limit, limit);
					transform.facing += Engine::Math::TurnAngle{static_cast<std::uint32_t>(turn)};
					if (magnitude - std::llabs(turn) > tolerance)
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
			const ecs::Entity victim = attack.target;
			const bool windingUp = armament.preAttackUntil != 0 && armament.preAttackSeen + 1 == tick && armament.preAttackVictim == victim;
			if (!windingUp)
			{
				armament.preAttackUntil = 0;
				const bool fullClip = weapon.clipSize == 0 || armament.clip == 0 || armament.clip >= weapon.clipSize;
				const bool skip = (weapon.preAttackType == WeaponDefinition::PreAttack::PerClip && !fullClip) ||
					(weapon.preAttackType == WeaponDefinition::PreAttack::PerAttack && armament.lastVictim == victim);
				const auto windUp = skip ? 0 : static_cast<std::uint64_t>(
					(Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(weapon.preAttackDelay)) * bonus.Get(WeaponBonusField::PreAttack)).Floor());
				// Weapon::preFireWeapon: a wind-up at all sets a leech range weapon's unlimited reach.
				if (windUp > 0 && weapon.leechRange)
					targets[row].leech |= slotBit;
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

			auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation});
			if (!temporaryAim)
				targets[row].fired = 1;
			// Weapon::privateFireWeapon's DAMAGE_DISARM: nothing is fired; the victim is disarmed (the game carries it out),
			// the shot counted and a round spent (an emptied clip reloading as it would), but no wait between shots is set.
			if (weapon.damageType == weapons.disarm)
			{
				if (attack.atPosition == 0)
					disarms.push_back({entities[row], victim, armament.weapon, owners[row].player, target->position,
						experiences.empty() ? std::uint8_t{0} : experiences[row].level});
				if (!temporaryAim)
					CountShot(targets[row]);
				if (weapon.clipSize > 0)
				{
					if (armament.clip == 0 || armament.clip > weapon.clipSize)
						armament.clip = weapon.clipSize;
					if (--armament.clip == 0)
					{
						armament.clip = weapon.clipSize;
						armament.readyTick = tick + std::max<std::uint64_t>(BonusDelay(weapon.clipReload, bonus), 1);
						armament.reloading = true;
						if (!slotSets.empty())
							ReleaseTemporaryLock(slotSets[row]);
					}
				}
			}
			else
			{
				// As Weapon::privateFireWeapon: wrap at the barrel count; this barrel fires.
				// (Wrapped round, the first barrel starts its ShotsPerBarrel afresh.)
				if (armament.barrel >= armament.barrels)
					armament.barrelShots = 0;
				const std::uint8_t barrel = armament.barrel >= armament.barrels ? std::uint8_t{0} : armament.barrel;
				Engine::Math::FixedVector3 origin = transform.position;
				if (weapon.projectile)
				{
					const bool turreted = armament.turret && aimer != nullptr;
					origin = LaunchPosition(definitions.empty() ? nullptr : layouts.Of(definitions[row].index), barrel, turreted, turreted ? aimer->angle : Engine::Math::TurnAngle{},
						turreted ? aimer->pitch : Engine::Math::TurnAngle{}, transform, slotIndex, altAimed);
				}
				// WeaponTemplate::fireWeaponTemplate's scatter: ScatterRadius, plus ScatterRadiusVsInfantry at an infantry
				// victim, randomised (a distance up to it, any way round); the aim moves that far, on to the ground there. A
				// projectile with any scatter flies at that spot and not after its victim (so it can miss); a shot without a
				// projectile still hits its victim (the damage goes to the victim's position).
				Engine::Math::FixedVector3 aim = target->position;
				ecs::Entity shotVictim = attack.target;
				// Weapon::privateFireWeapon's getSneakyTargetingOffset: a victim its attackers miss for now is shot at a spot off
				// it (along its facing), a position and not it.
				if (const SneakyTarget *sneaky = shotVictim != ecs::Entity{} ? lookup.Get<SneakyTarget>(shotVictim) : nullptr; sneaky != nullptr && sneaky->Active(tick))
				{
					aim.x += sneaky->offset.x;
					aim.y += sneaky->offset.y;
					shotVictim = {};
				}
				// Weapon::privateFireWeapon's ScatterTarget pattern: while this clip has entries it has not aimed at, it aims at
				// a random one of them (scaled by ScatterTargetScalar) off the victim's position, on the ground there, and at no
				// victim; that entry is used up until the clip reloads. The pattern used up, it fires as it would without one.
				const std::uint32_t patternSize = std::min(weapon.scatterCount, ScatterTargetMax);
				const std::uint64_t unused = patternSize == 0 ? 0 : ~armament.scatterUsed & (patternSize == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << patternSize) - 1);
				if (unused != 0)
				{
					auto patternRandom = Engine::Math::Stream(seed ^ 0x5CA77A6Eu, {tick, entities[row].index, entities[row].generation});
					auto pick = Engine::Math::UniformInt(patternRandom, 0, static_cast<std::int64_t>(std::popcount(unused)) - 1);
					std::uint64_t left = unused;
					for (; pick > 0; --pick)
						left &= left - 1;
					const auto entry = static_cast<std::uint32_t>(std::countr_zero(left));
					armament.scatterUsed |= std::uint64_t{1} << entry;
					const Engine::Math::FixedVector2 offset = weapons.scatterTargets[weapon.scatterFirst + entry];
					aim.x += offset.x * weapon.scatterTargetScalar;
					aim.y += offset.y * weapon.scatterTargetScalar;
					aim.z = context.Read<GroundHeight>().At(aim.XY());
					shotVictim = {};
				}
				// fireWeaponTemplate's victimPos (after the sneaky offset and the ScatterTarget pattern, before the scatter): what
				// its delay is measured to.
				const Engine::Math::FixedVector3 victimAt = aim;
				const bool infantryVictim = shotVictim != ecs::Entity{} && (target->classes & target_class::Infantry) != 0;
				if (weapon.scatterRadius > Engine::Math::Fixed{} || (weapon.infantryScatter > Engine::Math::Fixed{} && infantryVictim))
				{
					Engine::Math::Fixed scatter = weapon.scatterRadius;
					if (weapon.infantryScatter > Engine::Math::Fixed{} && infantryVictim)
						scatter += weapon.infantryScatter;
					auto scatterRandom = Engine::Math::Stream(seed ^ 0x5CA77E4u, {tick, entities[row].index, entities[row].generation});
					scatter = Engine::Math::UniformFixed(scatterRandom, Engine::Math::Fixed{}, scatter);
					const Engine::Math::TurnAngle way{static_cast<std::uint32_t>(Engine::Math::UniformInt(scatterRandom, 0, 0xFFFFFFFFll))};
					// A laser (no projectile) hits its victim while the scatter is within either damage radius (scaled by its
					// RADIUS bonus); beyond both it misses for the ground at the scattered spot, with no victim.
					const Engine::Math::Fixed radiusScale = bonus.Get(WeaponBonusField::Radius);
					const bool laserMiss = weapon.laser && !weapon.projectile && scatter > weapon.primaryRadius * radiusScale &&
						scatter > weapon.secondaryRadius * radiusScale;
					if ((weapon.projectile && scatter > Engine::Math::Fixed{}) || laserMiss)
					{
						aim.x += scatter * Engine::Math::Cos(way);
						aim.y += scatter * Engine::Math::Sin(way);
						aim.z = context.Read<GroundHeight>().At(aim.XY());
						shotVictim = {};
					}
				}
				// A lobbed shot's projectile carries it (it lands where that detonates); others land after their travel. A
				// laser's damage is dealt as it fires, whatever its speed (fireWeaponTemplate returns before the delay).
				const std::uint64_t travel = weapon.laser && !weapon.projectile ? 0 : HitDelayTicks(transform.position, victimAt, weapon.speed);
				const bool turned = armament.turret && aimer != nullptr;
				out.push_back({entities[row], shotVictim, armament.weapon, owners[row].player, origin, aim, tick,
					weapon.lobbed || weapon.guided || weapon.objectFlown ? LandsWithProjectile : tick + travel, transform.facing + (turned ? aimer->angle : Engine::Math::TurnAngle{}),
					turned ? aimer->pitch : Engine::Math::TurnAngle{}, ecs::Entity{}, slotIndex, 0, {}, shelter, experiences.empty() ? std::uint8_t{0} : experiences[row].level, 0, {}, bonus.Get(WeaponBonusField::Damage),
					bonus.Get(WeaponBonusField::Radius)});

				const std::uint64_t spread = weapon.delayMax > weapon.delayMin ? weapon.delayMax - weapon.delayMin : 0;
				const std::uint64_t delay = weapon.delayMin +
					(spread == 0 ? 0 : static_cast<std::uint64_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(spread))));
				// getDelayBetweenShots: divided by the rate-of-fire bonus.
				armament.readyTick = tick + std::max<std::uint64_t>(BonusDelay(delay, bonus), 1);
				armament.firedTick = tick;
				// Weapon::privateFireWeapon: a leech range weapon, once fired, reaches any distance for the rest of the attack.
				if (weapon.leechRange)
					targets[row].leech |= slotBit;
				// A limited attack counts the shot; its last ends it.
				if (!temporaryAim)
					CountShot(targets[row]);
				// Then move to the next.
				armament.firedBarrel = barrel;
				// Its ShotsPerBarrel fired, the next barrel's turn (m_numShotsForCurBarrel).
				if (++armament.barrelShots >= weapon.shotsPerBarrel)
				{
					armament.barrel = static_cast<std::uint8_t>(barrel + 1);
					armament.barrelShots = 0;
				}
				else
					armament.barrel = barrel;
				armament.reloading = false;
				if (weapon.clipSize > 0)
				{
					if (armament.clip == 0 || armament.clip > weapon.clipSize)
						armament.clip = weapon.clipSize;
					if (--armament.clip == 0)
					{
						if (weapon.reloadsAtBase || weapon.noReload)
						{
							// Out of ammo (Weapon::privateFireWeapon: m_status OUT_OF_AMMO, never fireable again) until it
							// reloads at base (its airfield refills it); a weapon that never reloads stays so.
							armament.readyTick = OutOfAmmo;
							armament.reloading = true;
						}
						else
						{
							armament.clip = weapon.clipSize;
							armament.scatterUsed = 0;
							armament.readyTick = tick + std::max<std::uint64_t>(BonusDelay(weapon.clipReload, bonus), 1);
							armament.reloading = true;
						}
						// Object::fireCurrentWeapon: reloaded, it lets go of a temporary lock.
						if (!slotSets.empty())
							ReleaseTemporaryLock(slotSets[row]);
					}
				}
			}
			// Object::isReloadTimeShared: the set's other weapons wait as long (not a clip emptied for good).
			if (!slotSets.empty() && armament.readyTick != OutOfAmmo)
			{
				WeaponSlots &set = slotSets[row];
				ShareReloadTime(set, armament.readyTick, armament.reloading);
				set.slots[set.current].readyTick = armament.readyTick;
				set.slots[set.current].reloading = armament.reloading;
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
				{
					const std::uint64_t before = armament.readyTick;
					Retime(armament, weapon, weapons.Bonus(weapon, (bonusRows.empty() ? 0u : bonusRows[row].Effective()) |
						ContinuousFireConditions(tracker, weapons)), tick, random);
					// (Shared: every weapon waits as long, reloading.)
					if (!slotSets.empty() && armament.readyTick != before)
						ShareReloadTime(slotSets[row], armament.readyTick, true);
				}
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
	// Its rows are independent: large chunks are shared out in pieces of 32 rows.
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::MovementSystem, engine::gameplay::TargetingSystem>;
};
}
