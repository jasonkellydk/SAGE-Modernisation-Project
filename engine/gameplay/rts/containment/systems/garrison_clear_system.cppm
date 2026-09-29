export module engine.gameplay.rts.containment.systems.garrison_clear_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.resources.garrison_kills;
export import engine.gameplay.rts.containment.components.garrison;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.resources.weapon_catalog;

// DumbProjectileBehavior::projectileHandleCollision's garrison clearing, for the tick's garrison hits in order: a
// structure that is garrisonable (GarrisonContain), not ImmuneToClearBuildingAttacks and has someone inside loses up to
// GarrisonHitKillCount of those inside, in the order they are held, that are alive (health left) and of its kinds (each killed by the
// projectile's launcher: scoreTheKill, then kill()); killing any, the projectile is gone without detonating (its
// GarrisonHitKillFX plays on the building); killing none, it detonates there as usual.
export namespace engine::gameplay
{
struct GarrisonClearSystem
{
	using Query = ecs::Query<ecs::Read<Garrison>>;
	using Lookup = ecs::Lookup<ecs::Read<Garrison>, ecs::Read<Targetable>, ecs::Read<Health>>;
	using Resources = ecs::Resources<ecs::Read<GarrisonHits>, ecs::Write<GarrisonClears>, ecs::Read<CargoManifest>, ecs::Read<WeaponCatalog>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		GarrisonClears &clears = context.Write<GarrisonClears>();
		clears.kills.clear();
		clears.detonations.clear();
		const CargoManifest &manifest = context.Read<CargoManifest>();
		const auto lookup = context.Lookup<Lookup>();
		context.Read<GarrisonHits>().ForEach([&](const GarrisonHit &hit) {
			std::uint32_t killed = 0;
			const Garrison *garrison = lookup.IsAlive(hit.building) ? lookup.Get<Garrison>(hit.building) : nullptr;
			if (garrison != nullptr && garrison->immuneToClear == 0)
				for (const ecs::Entity inside : manifest.Aboard(hit.building))
				{
					if (killed >= hit.count)
						break;
					const Health *health = lookup.Get<Health>(inside);
					const Targetable *kind = lookup.Get<Targetable>(inside);
					if (health == nullptr || IsDead(*health) || kind == nullptr)
						continue;
					if ((kind->classes & hit.requiredClasses) != hit.requiredClasses || (kind->classes & hit.forbiddenClasses) != 0)
						continue;
					clears.kills.push_back({hit.building, inside, hit.shot.source, hit.shot.sourcePlayer, killed == 0 ? hit.projectileDefinition : 0xFFFFFFFFu,
						health->maximum});
					++killed;
				}
			if (killed == 0)
				clears.detonations.push_back(hit.shot);
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::GarrisonClearSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.garrison_clear";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the projectiles fly and before the impacts.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
