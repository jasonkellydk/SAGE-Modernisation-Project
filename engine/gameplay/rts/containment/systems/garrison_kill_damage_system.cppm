export module engine.gameplay.rts.containment.systems.garrison_kill_damage_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.health.resources.incoming_damage;
export import engine.gameplay.rts.containment.components.garrison;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.resources.weapon_catalog;

// ActiveBody::attemptDamage's DAMAGE_KILL_GARRISONED (a Microwave Tank's building clearer), after the tick's impacts and
// before its damage: each such damage on a garrisonable structure (GarrisonContain) that is not ImmuneToClearBuildingAttacks
// kills up to its amount (before armor, rounded down) of those inside, in the order they are held, still alive (each
// killed by the damage's source: scoreTheKill, then kill()); the structure itself loses no health (a handled type).
export namespace engine::gameplay
{
struct GarrisonKillDamageSystem
{
	using Query = ecs::Query<ecs::Read<Garrison>>;
	using Lookup = ecs::Lookup<ecs::Read<Garrison>, ecs::Read<Health>>;
	using Resources = ecs::Resources<ecs::Write<IncomingDamage>, ecs::Read<CargoManifest>, ecs::Read<WeaponCatalog>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		if (weapons.killGarrisoned == WeaponCatalog::None)
			return;
		IncomingDamage &incoming = context.Write<IncomingDamage>();
		const CargoManifest &manifest = context.Read<CargoManifest>();
		const auto lookup = context.Lookup<Lookup>();
		std::vector<DamageRecord> kills;
		std::vector<ecs::Entity> killed;
		for (const DamageRecord &record : incoming.All())
		{
			if (record.damageType != weapons.killGarrisoned)
				continue;
			const Garrison *garrison = lookup.IsAlive(record.target) ? lookup.Get<Garrison>(record.target) : nullptr;
			if (garrison == nullptr || garrison->immuneToClear != 0)
				continue;
			const std::int64_t toKill = record.amount.Floor();
			std::int64_t made = 0;
			for (const ecs::Entity inside : manifest.Aboard(record.target))
			{
				if (made >= toKill)
					break;
				const Health *health = lookup.Get<Health>(inside);
				if (health == nullptr || IsDead(*health) || std::find(killed.begin(), killed.end(), inside) != killed.end())
					continue;
				kills.push_back({inside, record.source, health->maximum, weapons.unresistable, weapons.normalDeath, DamageRecord::NoFxType, record.sourcePlayer});
				killed.push_back(inside);
				++made;
			}
		}
		if (kills.empty())
			return;
		for (const DamageRecord &kill : kills)
			incoming.Add(kill);
		incoming.Seal();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::GarrisonKillDamageSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.garrison_kill_damage";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the impacts and before the damage.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
