export module engine.gameplay.rts.combat.systems.impact_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.systems.weapon_system;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.physics.resources.shock_waves;
export import engine.gameplay.rts.combat.resources.garrison_kills;
export import engine.gameplay.rts.combat.resources.historic_damage;
export import engine.gameplay.common.identity.components.producer;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.resources.template_equivalence;
export import engine.gameplay.common.health.components.pending_damage;

// Queues the tick's shots and lands the due ones: a direct hit damages its
// target; radius damage hits everything within the primary radius for the
// primary damage and within the secondary radius for the secondary damage,
// filtered by whom the weapon affects. Homing: a shot lands on its target's
// position when the target is still there. The result is the tick's
// incoming damage, sorted by target, and the impacts for the client.
// Shots carried by projectiles land where those detonated this tick
// (Weapon::fireProjectileDetonationWeapon: at the projectile, no victim).
// Each hit first counts towards its weapon's historic bonus (dealDamageInternal -> processHistoricDamage): enough hits
// together fire the bonus weapon there, from the firer (createAndFireTempWeapon; it goes off on the next tick's firing).
export namespace engine::gameplay
{
struct ImpactSystem
{
	using Query = ecs::Query<ecs::Read<Health>>;
	// (A missile's body, killed after its blast.)
	using Lookup = ecs::Lookup<ecs::Read<Health>, ecs::Read<Transform>, ecs::Read<Producer>, ecs::Read<DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<FiredShots>, ecs::Read<GarrisonClears>, ecs::Write<ShockWaves>, ecs::Read<Detonations>, ecs::Read<MissileDetonations>, ecs::Write<ShotQueue>, ecs::Write<IncomingDamage>, ecs::Read<SpatialIndex>,
		ecs::Read<Relationships>, ecs::Read<WeaponCatalog>, ecs::Write<HistoricDamage>, ecs::Write<TemporaryWeaponFires>, ecs::Read<TemplateEquivalence>>;

	// `team`: the firer's team (none when it is gone or aboard something: its player's view alone). `producer`: what made
	// the firer (Weapon::dealDamageInternal spares it, as the firer itself, unless the weapon affects SELF). `similar`: the
	// entry is of a kind equivalent to the firer's (ThingTemplate::isEquivalentTo), which a NOT_SIMILAR weapon spares
	// when the firer regards it as an ally (so a stick of carpet bombs or a crowd of terrorists does not go off in a chain).
	static bool Affected(const Relationships &relationships, const WeaponDefinition &weapon, const Shot &shot, const SpatialEntry &entry,
		std::uint32_t team = Relationships::NoTeam, ecs::Entity producer = {}, bool similar = false) noexcept
	{
		if ((entry.classes & (target_class::AirborneVehicle | target_class::AirborneInfantry)) != 0 &&
			(weapon.affects & weapon_affects::NotAirborne) != 0)
			return false;
		if (entry.entity == shot.carrier)
			return false;
		if (entry.entity == shot.source)
			return (weapon.affects & weapon_affects::Self) != 0;
		if (producer != ecs::Entity{} && entry.entity == producer && (weapon.affects & weapon_affects::Self) == 0)
			return false;
		if (similar && (weapon.affects & weapon_affects::NotSimilar) != 0 &&
			relationships.Between(team, shot.sourcePlayer, entry.team, entry.player) == Relationship::Allies)
			return false;
		// Weapon::dealDamageInternal asks the victim (Object::getRelationship): an undetected defector sees everyone as neutral.
		if ((entry.classes & target_class::Undetected) != 0)
			return (weapon.affects & weapon_affects::Neutrals) != 0;
		switch (relationships.Between(team, shot.sourcePlayer, entry.team, entry.player))
		{
		case Relationship::Allies: return (weapon.affects & weapon_affects::Allies) != 0;
		case Relationship::Enemies: return (weapon.affects & weapon_affects::Enemies) != 0;
		default: return (weapon.affects & weapon_affects::Neutrals) != 0;
		}
	}

	void Execute(ecs::SystemContext &context)
	{
		const FiredShots &fired = context.Read<FiredShots>();
		ShotQueue &queue = context.Write<ShotQueue>();
		IncomingDamage &incoming = context.Write<IncomingDamage>();
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const Relationships &relationships = context.Read<Relationships>();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const auto lookup = context.Lookup<Lookup>();
		std::vector<DamageRecord> killed;
		fired.ForEach([&](const Shot &shot) {
			if (shot.impactTick != LandsWithProjectile)
				queue.Add(shot);
		});
		incoming.Clear();
		queue.Impacts().clear();
		auto &shocks = context.Write<ShockWaves>().list;
		shocks.clear();
		std::vector<Shot> landing = queue.TakeDue(context.Tick());
		const std::size_t queued = landing.size();
		context.Read<Detonations>().AppendTo(landing);
		const std::size_t lobbed = landing.size();
		context.Read<MissileDetonations>().AppendTo(landing);
		const std::size_t missiles = landing.size();
		// Garrison hits that cleared nobody go off as usual; those that did kill (Object::kill, the launcher credited).
		const GarrisonClears &clears = context.Read<GarrisonClears>();
		landing.insert(landing.end(), clears.detonations.begin(), clears.detonations.end());
		for (const GarrisonKill &kill : clears.kills)
			incoming.Add({kill.victim, kill.source, kill.amount, weapons.unresistable, weapons.normalDeath, DamageRecord::NoFxType, kill.sourcePlayer});
		for (std::size_t index = 0; index < landing.size(); ++index)
		{
			const Shot &shot = landing[index];
			const bool detonated = index >= queued;
			const WeaponDefinition &weapon = weapons.At(shot.weapon);
			// MissileAIUpdate::detonate with MissileCallsOnDie: after its blast, the missile is killed (unresistable, its most
			// health, DEATH_DETONATED, by no one).
			if (index >= lobbed && index < missiles && weapon.missileCallsOnDie && lookup.IsAlive(shot.carrier))
				if (const Health *body = lookup.Get<Health>(shot.carrier))
					killed.push_back({shot.carrier, ecs::Entity{}, body->maximum, weapons.unresistable, weapons.detonatedDeath, DamageRecord::NoFxType,
						DamageRecord::NoPlayer});
			// DumbProjectileBehavior::detonate with DetonateCallsKill: after its blast the shell is killed the same way.
			if (index >= queued && index < lobbed && weapon.arc.callsKill != 0 && lookup.IsAlive(shot.carrier))
				if (const Health *body = lookup.Get<Health>(shot.carrier))
					killed.push_back({shot.carrier, ecs::Entity{}, body->maximum, weapons.unresistable, weapons.detonatedDeath, DamageRecord::NoFxType,
						DamageRecord::NoPlayer});
			// WeaponTemplate::fireWeaponTemplate: DamageDealtAtSelfPosition deals it with no victim (damageID INVALID_ID) at
			// the firer's position as it fired (a laser: where its beam ends).
			const SpatialEntry *target = detonated || weapon.damageAtSelf ? nullptr : spatial.Find(shot.target);
			Engine::Math::FixedVector3 at = detonated ? shot.aim : weapon.damageAtSelf && !weapon.laser ? shot.origin : target != nullptr ? target->position : shot.aim;
			// Weapon::dealDamageInternal's source: a detonating projectile itself (its launcher is its producer), else the firer.
			const ecs::Entity self = detonated && shot.carrier != ecs::Entity{} ? shot.carrier : shot.source;
			const bool suicide = (weapon.affects & weapon_affects::Suicide) != 0;
			queue.Impacts().push_back({shot.source, shot.weapon, at, shot.veterancy});
			if (weapon.historicBonusCount > 0 && ProcessHistoricDamage(context.Write<HistoricDamage>(), shot.weapon, weapon, at.XY(), context.Tick()) &&
				weapon.historicBonusWeapon != WeaponCatalog::None)
			{
				const SpatialEntry *source = spatial.Find(shot.source);
				context.Write<TemporaryWeaponFires>().Add({shot.source, weapon.historicBonusWeapon, shot.sourcePlayer, source != nullptr ? source->position : shot.origin, at});
			}
			// The firer's bonus scales damage and radii (getPrimaryDamage / getPrimaryDamageRadius ...).
			const Engine::Math::Fixed primaryRadius = weapon.primaryRadius * shot.radiusScale + shot.radiusBonus;
			const Engine::Math::Fixed secondaryRadius = weapon.secondaryRadius * shot.radiusScale + shot.radiusBonus;
			const Engine::Math::Fixed primaryDamage = weapon.primaryDamage * shot.damageScale;
			const Engine::Math::Fixed secondaryDamage = weapon.secondaryDamage * shot.damageScale;
			const Engine::Math::Fixed radius = std::max(primaryRadius, secondaryRadius);
			if (radius <= Engine::Math::Fixed{})
			{
				// SUICIDE (WEAPON_KILLS_SELF) without a radius: only its source is hurt, by HUGE_DAMAGE_AMOUNT of the
				// weapon's damage and death types (no player's: m_sourcePlayerMask 0); its victim is spared. (A source
				// already gone: the original dereferences it; nothing here.)
				if (suicide)
				{
					if (lookup.IsAlive(self))
						incoming.Add({self, self, HugeDamage(), weapon.damageType, weapon.deathType, DamageRecord::NoFxType, DamageRecord::NoPlayer,
							weapon.damageStatusType});
					continue;
				}
				// The direct victim, whatever the weapon's affects flags.
				if (target != nullptr)
					incoming.Add({shot.target, shot.source, primaryDamage, weapon.damageType, weapon.deathType, DamageRecord::NoFxType, shot.sourcePlayer,
						weapon.damageStatusType});
				continue;
			}
			// Weapon::dealDamageInternal with its source gone (findObjectByID null: a delayed hit whose firer was deleted before
			// it landed): no affects flag is tested, nobody is the source to kill, and every shock wave goes straight up. A
			// detonating projectile is there as it goes off; a shot marked sourceHeld was dealt while its source still was.
			const bool sourceGone = !detonated && shot.sourceHeld == 0 && !lookup.IsAlive(shot.source);
			const SpatialEntry *firer = spatial.Find(shot.source);
			const std::uint32_t firerTeam = firer != nullptr ? firer->team : Relationships::NoTeam;
			// The shock wave's source: a detonating projectile where it went off, else the firer where it stands.
			const Engine::Math::FixedVector3 from = detonated ? at : firer != nullptr ? firer->position : shot.origin;
			// RadiusDamageAngle: only those within the cone about its firer's facing (a firer gone: no one).
			const Transform *firerBody = weapon.coned && lookup.IsAlive(shot.source) ? lookup.Get<Transform>(shot.source) : nullptr;
			if (weapon.coned && firerBody == nullptr)
				continue;
			// The firer's producer and kind: carried by the shot when the firer is gone as it lands, else its own.
			ecs::Entity producer = shot.producer;
			if (producer == ecs::Entity{} && lookup.IsAlive(shot.source))
				if (const Producer *made = lookup.Get<Producer>(shot.source))
					producer = made->entity;
			std::uint32_t kind = shot.kind;
			if (kind == Shot::NoKind && (weapon.affects & weapon_affects::NotSimilar) != 0 && lookup.IsAlive(shot.source))
				if (const DefinitionRef *ref = lookup.Get<DefinitionRef>(shot.source))
					kind = ref->index;
			const TemplateEquivalence *kinds =
				kind != Shot::NoKind && (weapon.affects & weapon_affects::NotSimilar) != 0 ? &context.Read<TemplateEquivalence>() : nullptr;
			// Weapon::dealDamageInternal: the primary victim (the shot's direct target) is hurt whatever the affects flags;
			// with SUICIDE the source in the blast takes HUGE_DAMAGE_AMOUNT instead of its share, whatever SELF says.
			const ecs::Entity primaryVictim = target != nullptr ? shot.target : ecs::Entity{};
			// iterateObjectsInRange(pos, radius, DAMAGE_RANGE_CALC_TYPE = FROM_BOUNDINGSPHERE_3D): to each bounding sphere.
			spatial.ForEachSphereWithin(at, radius, [&](const SpatialEntry &entry, Engine::Math::Fixed distanceSquared) {
				const bool primaryHit = primaryVictim != ecs::Entity{} && entry.entity == primaryVictim;
				const bool killSelf = !sourceGone && !primaryHit && suicide && entry.entity == self;
				if (!sourceGone && !primaryHit && !killSelf)
				{
					bool similar = false;
					if (kinds != nullptr)
						if (const DefinitionRef *ref = lookup.Get<DefinitionRef>(entry.entity))
							similar = kinds->Equivalent(kind, ref->index);
					if (!Affected(relationships, weapon, shot, entry, firerTeam, producer, similar))
						return;
				}
				if (firerBody != nullptr)
				{
					const Engine::Math::FixedVector3 toward = entry.position - firerBody->position;
					const Engine::Math::Fixed length = Engine::Math::Length(toward);
					const Engine::Math::Fixed along = length > Engine::Math::Fixed{}
						? (Engine::Math::Cos(firerBody->facing) * toward.x + Engine::Math::Sin(firerBody->facing) * toward.y) / length : Engine::Math::Fixed{};
					if (along < weapon.coneCosine)
						return;
				}
				// Weapon::dealDamageInternal: a shock wave rides the damage (straight up when on top of its source).
				if (weapon.shockWaveAmount > Engine::Math::Fixed{})
				{
					Engine::Math::FixedVector3 vector = sourceGone ? Engine::Math::FixedVector3{} : entry.position - from;
					const Engine::Math::Fixed tiny = Engine::Math::Fixed::FromRaw(1);
					if (Engine::Math::Abs(vector.x) < tiny && Engine::Math::Abs(vector.y) < tiny && Engine::Math::Abs(vector.z) < tiny)
						vector.z = Engine::Math::Fixed::One();
					shocks.push_back({entry.entity, vector, weapon.shockWaveAmount, weapon.shockWaveRadius, weapon.shockWaveTaperOff});
				}
				const bool primary = distanceSquared <= primaryRadius * primaryRadius;
				const Engine::Math::Fixed amount = killSelf ? HugeDamage() : primary ? primaryDamage : secondaryDamage;
				if (amount > Engine::Math::Fixed{})
					incoming.Add({entry.entity, shot.source, amount, weapon.damageType, weapon.deathType, DamageRecord::NoFxType, shot.sourcePlayer,
						weapon.damageStatusType});
			});
		}
		for (const DamageRecord &record : killed)
			incoming.Add(record);
		incoming.Seal();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::ImpactSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.impacts";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<engine::gameplay::HealthSystem>;
	using After = SystemTypeList<engine::gameplay::WeaponSystem>;
};
}
