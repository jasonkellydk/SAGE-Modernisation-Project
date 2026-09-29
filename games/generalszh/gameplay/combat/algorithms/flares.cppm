export module games.generalszh.gameplay.combat.algorithms.flares;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import engine.gameplay.rts.combat.components.countermeasures;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.physics.components.physics_body;
import engine.gameplay.common.physics.algorithms.forces;
import engine.gameplay.rts.movement.components.locomotion;

// CountermeasuresBehavior::launchVolley's flares, after the step (CountermeasuresSystem asked for them): each made on its
// launcher's player's default team where the launcher is, facing as it does, moving as it does (transferVelocityTo: its
// speed along its heading) and pushed off along its way out (applyMotiveForce); it joins the launcher's flares (the
// oldest dropped when the list is full).
export namespace generalszh::gameplay
{
inline void ApplyFlareLaunches(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	auto *resource = world.FindResource<gp::FlareLaunches>();
	if (resource == nullptr)
		return;
	std::vector<gp::FlareLaunch> launches;
	resource->AppendTo(launches);
	resource->Reset(0);
	for (const gp::FlareLaunch &launch : launches)
	{
		const auto *at = world.IsAlive(launch.owner) ? world.Get<gp::Transform>(launch.owner) : nullptr;
		if (at == nullptr || launch.definition == 0xFFFFFFFFu)
			continue;
		const gp::Transform from = *at;
		const std::uint32_t player = world.Get<gp::Owner>(launch.owner)->player;
		const std::uint32_t team = game.roster.DefaultTeam(player).value_or(world.Get<gp::TeamMember>(launch.owner)->team);
		const ecs::Entity flare = SpawnObject(game, game.templates.DefinitionAt(launch.definition).name, from.position.XY(), from.facing, team, {});
		if (!world.IsAlive(flare))
			continue;
		world.Get<gp::Transform>(flare)->position = from.position;
		if (auto *body = world.Get<gp::PhysicsBody>(flare))
		{
			if (const auto *motion = world.Get<gp::Locomotion>(launch.owner))
			{
				const auto heading = Engine::Math::Direction(from.facing);
				body->velocity = {heading.x * motion->speed, heading.y * motion->speed, Engine::Math::Fixed{}};
			}
			gp::ApplyMotiveForce(*body, launch.velocity, game.tick);
		}
		if (auto *decoys = world.Get<gp::Countermeasures>(launch.owner))
		{
			if (decoys->flareCount == gp::Countermeasures::MaxFlares)
			{
				std::rotate(decoys->flares.begin(), decoys->flares.begin() + 1, decoys->flares.end());
				--decoys->flareCount;
			}
			decoys->flares[decoys->flareCount++] = flare;
		}
	}
}
}
