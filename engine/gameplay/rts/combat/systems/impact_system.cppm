export module engine.gameplay.rts.combat.systems.impact_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.systems.weapon_system;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.physics.resources.shock_waves;
export import engine.gameplay.rts.combat.resources.garrison_kills;

// Queues the tick's shots and lands the due ones: a direct hit damages its
// target; radius damage hits everything within the primary radius for the
// primary damage and within the secondary radius for the secondary damage,
// filtered by whom the weapon affects. Homing: a shot lands on its target's
// position when the target is still there. The result is the tick's
// incoming damage, sorted by target, and the impacts for the client.
// Shots carried by projectiles land where those detonated this tick
// (Weapon::fireProjectileDetonationWeapon: at the projectile, no victim).
export namespace engine::gameplay
{
struct ImpactSystem
{
	using Query = ecs::Query<ecs::Read<Health>>;
	using Resources = ecs::Resources<ecs::Read<FiredShots>, ecs::Read<GarrisonClears>, ecs::Write<ShockWaves>, ecs::Read<Detonations>, ecs::Read<MissileDetonations>, ecs::Write<ShotQueue>, ecs::Write<IncomingDamage>, ecs::Read<SpatialIndex>,
		ecs::Read<Relationships>, ecs::Read<WeaponCatalog>>;

	// `team`: the firer's team (none when it is gone or aboard something: its player's view alone).
	static bool Affected(const Relationships &relationships, const WeaponDefinition &weapon, const Shot &shot, const SpatialEntry &entry,
		std::uint32_t team = Relationships::NoTeam) noexcept
	{
		if ((entry.classes & (target_class::AirborneVehicle | target_class::AirborneInfantry)) != 0 &&
			(weapon.affects & weapon_affects::NotAirborne) != 0)
			return false;
		if (entry.entity == shot.carrier)
			return false;
		if (entry.entity == shot.source)
			return (weapon.affects & weapon_affects::Self) != 0;
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
		context.Read<MissileDetonations>().AppendTo(landing);
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
			const SpatialEntry *target = detonated ? nullptr : spatial.Find(shot.target);
			Engine::Math::FixedVector3 at = detonated ? shot.aim : weapon.damageAtSelf ? shot.origin : target != nullptr ? target->position : shot.aim;
			queue.Impacts().push_back({shot.source, shot.weapon, at});
			// The firer's bonus scales damage and radii (getPrimaryDamage / getPrimaryDamageRadius ...).
			const Engine::Math::Fixed primaryRadius = weapon.primaryRadius * shot.radiusScale + shot.radiusBonus;
			const Engine::Math::Fixed secondaryRadius = weapon.secondaryRadius * shot.radiusScale + shot.radiusBonus;
			const Engine::Math::Fixed primaryDamage = weapon.primaryDamage * shot.damageScale;
			const Engine::Math::Fixed secondaryDamage = weapon.secondaryDamage * shot.damageScale;
			const Engine::Math::Fixed radius = std::max(primaryRadius, secondaryRadius);
			if (radius <= Engine::Math::Fixed{})
			{
				if (target != nullptr)
					incoming.Add({shot.target, shot.source, primaryDamage, weapon.damageType, weapon.deathType, DamageRecord::NoFxType, shot.sourcePlayer,
						weapon.damageStatusType});
				continue;
			}
			const SpatialEntry *firer = spatial.Find(shot.source);
			const std::uint32_t firerTeam = firer != nullptr ? firer->team : Relationships::NoTeam;
			// The shock wave's source: a detonating projectile where it went off, else the firer where it stands.
			const Engine::Math::FixedVector3 from = detonated ? at : firer != nullptr ? firer->position : shot.origin;
			spatial.ForEachWithin(at.XY(), radius, [&](const SpatialEntry &entry) {
				if (!Affected(relationships, weapon, shot, entry, firerTeam))
					return;
				// Weapon::dealDamageInternal: a shock wave rides the damage (straight up when on top of its source).
				if (weapon.shockWaveAmount > Engine::Math::Fixed{})
				{
					Engine::Math::FixedVector3 vector = entry.position - from;
					const Engine::Math::Fixed tiny = Engine::Math::Fixed::FromRaw(1);
					if (Engine::Math::Abs(vector.x) < tiny && Engine::Math::Abs(vector.y) < tiny && Engine::Math::Abs(vector.z) < tiny)
						vector.z = Engine::Math::Fixed::One();
					shocks.push_back({entry.entity, vector, weapon.shockWaveAmount, weapon.shockWaveRadius, weapon.shockWaveTaperOff});
				}
				const Engine::Math::Fixed reach = primaryRadius + entry.radius;
				const bool primary = Engine::Math::DistanceSquared(entry.position.XY(), at.XY()) <= reach * reach;
				const Engine::Math::Fixed amount = primary ? primaryDamage : secondaryDamage;
				if (amount > Engine::Math::Fixed{})
					incoming.Add({entry.entity, shot.source, amount, weapon.damageType, weapon.deathType, DamageRecord::NoFxType, shot.sourcePlayer,
						weapon.damageStatusType});
			});
		}
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
