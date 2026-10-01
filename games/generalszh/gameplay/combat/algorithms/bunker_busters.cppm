export module games.generalszh.gameplay.combat.algorithms.bunker_busters;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.effects.resources.effect_cues;
import games.generalszh.gameplay.containment.algorithms.garrisons;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.common.health.components.pending_damage;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.rts.combat.resources.shots;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.lifecycle.resources.kill_requests;
import engine.gameplay.rts.upgrades.resources.player_upgrades;

// BunkerBusterBehavior::onDie -> bustTheBunker for guided missiles that went off this tick (the fork's fix: only a missile
// killing itself, i.e. detonating, busts), after the step: when its player has UpgradeRequired (or it needs none), a
// bustable container it was aimed at (a garrison, tunnel or cave: for a tunnel, its network's occupants) puts everyone
// out at once and hurts each 100 of OccupantDamageWeaponTemplate's damage and death type from the shooter
// (harmAndForceExitAllContained; with no such weapon they are all killed, killAllContained); DetonationFX plays on the
// building (on the missile where it went off when it hit none) and ShockwaveWeaponTemplate goes off there from it
// (createAndFireTempWeapon, with the next tick's firing). Not ported: CrashThroughBunkerFX on the way in (the missile
// has no MISSILE_KILLING_SELF dive), the seismic ground heave (not compiled in the original).
export namespace generalszh::gameplay
{
namespace bunker_buster_detail
{
namespace gp = engine::gameplay;

inline bool Bustable(const content::ObjectDefinition &kind)
{
	return std::ranges::any_of(kind.modules, [](const content::ModuleEntry &module) {
		return module.type == "GarrisonContain" || module.type == "TunnelContain" || module.type == "CaveContain";
	});
}

}

inline void ApplyBunkerBusters(GameWorld &game, const engine::gameplay::MissileDetonations &detonations)
{
	namespace gp = engine::gameplay;
	using namespace bunker_buster_detail;
	auto &world = game.world;
	const auto *weapons = world.FindResource<gp::WeaponCatalog>();
	if (weapons == nullptr)
		return;
	std::vector<gp::Shot> shots;
	detonations.ForEach([&](const gp::Shot &shot) { shots.push_back(shot); });
	for (const gp::Shot &shot : shots)
	{
		if (shot.weapon >= weapons->Size() || !weapons->At(shot.weapon).guided)
			continue;
		const BunkerBusterConfig *buster = game.templates.BunkerBusterOf(weapons->At(shot.weapon).projectileDefinition);
		if (buster == nullptr)
			continue;
		if (!buster->upgrade.empty())
		{
			const auto upgrade = game.templates.Content().upgrades.Find(buster->upgrade);
			if (!upgrade || !world.Resource<gp::PlayerUpgrades>().Completed(shot.sourcePlayer).Has(*upgrade))
				continue;
		}
		const ecs::Entity target = world.IsAlive(shot.target) ? shot.target : ecs::Entity{};
		Engine::Math::FixedVector3 where = shot.aim;
		if (target != ecs::Entity{})
		{
			where = world.Get<gp::Transform>(target)->position;
			const auto *ref = world.Get<gp::DefinitionRef>(target);
			if (ref != nullptr && Bustable(game.templates.DefinitionAt(ref->index)))
			{
				const auto aboard = game.manifest.Aboard(target);
				const std::vector<ecs::Entity> occupants(aboard.begin(), aboard.end());
				const std::uint32_t occupantWeapon = buster->occupantWeapon.empty() ? gp::WeaponCatalog::None : game.templates.Weapon(buster->occupantWeapon);
				for (const ecs::Entity occupant : occupants)
				{
					if (!world.IsAlive(occupant))
						continue;
					if (occupantWeapon == gp::WeaponCatalog::None)
					{
						KillNow(game, occupant);
						continue;
					}
					TakeOutNow(game, target, occupant);
					const gp::WeaponDefinition &weapon = weapons->At(occupantWeapon);
					DamageFrom(game, occupant, shot.source, Engine::Math::Fixed::FromInt(100), weapon.damageType, weapon.deathType);
				}
			}
		}
		if (!buster->detonationFX.empty())
			if (auto *cues = world.FindResource<EffectCues>())
				cues->list.push_back({buster->detonationFX, where, target});
		if (!buster->shockwaveWeapon.empty())
			if (const std::uint32_t shockwave = game.templates.Weapon(buster->shockwaveWeapon); shockwave != gp::WeaponCatalog::None)
				if (auto *fires = world.FindResource<gp::TemporaryWeaponFires>())
					fires->Add({target != ecs::Entity{} ? target : shot.source, shockwave, shot.sourcePlayer, where, where});
	}
}
}
