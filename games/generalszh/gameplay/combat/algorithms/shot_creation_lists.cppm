export module games.generalszh.gameplay.combat.algorithms.shot_creation_lists;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.death.resources.death_events;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.rts.combat.resources.shots;
import games.generalszh.gameplay.creation.algorithms.creation_list_runner;

// Weapons' creation lists (Weapon::fireWeaponTemplate's FireOCL, the detonation's ProjectileDetonationOCL): run where
// this tick's shots left and landed, on their firer's team, after the tick's systems.
export namespace generalszh::gameplay
{
namespace gp = engine::gameplay;

// What this tick's shots create: a weapon's fire creation list where it
// fired, its detonation creation list where it landed (napalm fire fields).
inline void RunShotCreationLists(GameWorld &game)
{
	const auto teamOf = [&](ecs::Entity entity) {
		const auto *member = game.world.IsAlive(entity) ? game.world.Get<gp::TeamMember>(entity) : nullptr;
		return member != nullptr ? member->team : gp::NoTeam;
	};
	game.world.Resource<gp::FiredShots>().ForEach([&](const gp::Shot &shot) {
		if (const auto *weapon = game.templates.WeaponContentAt(shot.weapon); weapon != nullptr && !weapon->FireOCL(shot.veterancy).empty())
			RunCreationList(game, weapon->FireOCL(shot.veterancy), {shot.origin, Engine::Math::Heading((shot.aim - shot.origin).XY()), teamOf(shot.source), shot.source});
	});
	for (const gp::Impact &impact : game.world.Resource<gp::ShotQueue>().Impacts())
		if (const auto *weapon = game.templates.WeaponContentAt(impact.weapon); weapon != nullptr && !weapon->DetonationOCL(impact.veterancy).empty())
			RunCreationList(game, weapon->DetonationOCL(impact.veterancy), {impact.position, {}, teamOf(impact.source), impact.source});
}
}
