export module games.generalszh.gameplay.combat.algorithms.object_flown_launches;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.effects.algorithms.radius_decals;
import games.generalszh.content.combat.combat_catalog;
import engine.gameplay.rts.combat.resources.shots;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.rts.combat.components.neutron_flight;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.attitude;
import engine.gameplay.common.physics.components.physics_body;

// Projectiles that fly themselves as full objects, made after the tick from its shots.
export namespace generalszh::gameplay
{
// Weapon::fireWeaponTemplate for a projectile flying itself (a superweapon's NeutronMissileUpdate): its ProjectileObject
// made on the firer's player's default team where it is fired from (setProducer: the firer), facing the firer's way,
// its flight set for the spot (TargetFromDirectlyAbove over it) with the firer's velocity (none: a building), to launch
// on its first tick.
inline void ApplyObjectFlownLaunches(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *fired = world.FindResource<gp::FiredShots>();
	const auto *weapons = world.FindResource<gp::WeaponCatalog>();
	if (fired == nullptr || weapons == nullptr)
		return;
	std::vector<gp::Shot> shots;
	fired->ForEach([&](const gp::Shot &shot) {
		if (shot.weapon != gp::WeaponCatalog::None && weapons->At(shot.weapon).objectFlown)
			shots.push_back(shot);
	});
	for (const gp::Shot &shot : shots)
	{
		const gp::WeaponDefinition &weapon = weapons->At(shot.weapon);
		const std::uint32_t team = game.roster.DefaultTeam(shot.sourcePlayer).value_or(0u);
		const ecs::Entity missile = SpawnObject(game, game.templates.DefinitionAt(weapon.projectileDefinition).name, shot.origin.XY(), shot.launchYaw, team, {});
		if (!world.IsAlive(missile))
			continue;
		world.Get<gp::Transform>(missile)->position = shot.origin;
		SetProducer(game, missile, shot.source);
		gp::NeutronFlight flight;
		flight.target = shot.aim;
		flight.intermediate = shot.aim + Engine::Math::FixedVector3{Engine::Math::Fixed{}, Engine::Math::Fixed{}, weapon.neutron.targetFromAbove};
		flight.forward = {Engine::Math::Cos(shot.launchYaw), Engine::Math::Sin(shot.launchYaw), Engine::Math::Fixed{}};
		flight.noTurnLeft = weapon.neutron.initialDistance;
		flight.launcher = shot.source;
		flight.weapon = shot.weapon;
		if (const auto *body = world.IsAlive(shot.source) ? world.Get<gp::PhysicsBody>(shot.source) : nullptr)
			flight.velocity = body->velocity;
		if (!world.Has<gp::Attitude>(missile))
			world.Add<gp::Attitude>(missile);
		world.Add<gp::NeutronFlight>(missile);
		*world.Get<gp::NeutronFlight>(missile) = flight;
		// NeutronMissileUpdate::projectileFireAtObjectOrPosition: its DeliveryDecal on its target, until it dies.
		if (const auto neutron = content::ReadNeutronMissile(game.templates.DefinitionAt(weapon.projectileDefinition), game.step))
			LayRadiusDecal(game, missile, neutron->deliveryDecal, neutron->deliveryDecalRadius, shot.aim, RadiusDecalUntil::Dies);
	}
}
}
