export module games.generalszh.gameplay.powers.algorithms.visible_payloads;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import engine.gameplay.rts.delivery.components.delivery;
import engine.gameplay.rts.combat.systems.missile_flight_system;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.attitude;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.physics.components.physics_body;
import engine.gameplay.common.physics.algorithms.forces;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.rts.movement.components.move_order;
import engine.config.binding.values;

// DeliverPayloadAIUpdate's visible payload (GeneralsMD DeliverPayloadAIUpdate.cpp, DeliveringState::update), made after
// the tick from its VisibleDrops, in the order let go: each item its run's VisiblePayloadTemplateName, of its carrier's
// player (produced by it), where its carrier's VisibleDropBoneBaseNameNN bone was as it let go (Drawable::
// getPristineBonePositions -> transformBoneToWorld; no bone: the carrier's position), facing its carrier's way. With
// InheritTransportVelocity it takes the carrier's velocity (applyMotiveForce of velocity x mass) and starts one
// velocity back. A projectile (MissileAIUpdate) is fired at the carrier's target for VisiblePayloadWeaponTemplate
// (projectileFireAtObjectOrPosition, the carrier's veterancy): it flies as the game's guided missiles do, from the next
// tick, landing that weapon where it detonates. Anything else is a bomb: it pitches at CenterOfMassOffset x
// ExitPitchRate and, with an AI of its own, heads for the run's goal (aiMoveToPosition).
export namespace generalszh::gameplay
{
inline void ApplyVisibleDrops(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	using Engine::Math::FixedVector3;
	auto &world = game.world;
	const auto *drops = world.FindResource<gp::VisibleDrops>();
	if (drops == nullptr || drops->Size() == 0)
		return;
	std::vector<gp::VisibleDrop> made;
	drops->AppendTo(made);
	const content::GameContent &content = game.templates.Content();
	for (const gp::VisibleDrop &drop : made)
	{
		if (drop.run >= content.powers.visibleRuns.size() || !world.IsAlive(drop.carrier))
			continue;
		const content::VisibleRun &run = content.powers.visibleRuns[drop.run];
		const content::ObjectDefinition *kind = content.objects.Find(run.payload);
		if (kind == nullptr)
			continue;
		const std::uint32_t player = world.Has<gp::Owner>(drop.carrier) ? world.Get<gp::Owner>(drop.carrier)->player : 0u;
		// Where its bone was (the carrier's frame turned by its facing, onto its position).
		FixedVector3 at = drop.at;
		const auto bone = static_cast<std::size_t>(drop.index - 1);
		if (drop.index >= 1 && bone < run.bones.size() && run.bones[bone])
		{
			const FixedVector3 local = *run.bones[bone];
			const Fixed c = Engine::Math::Cos(drop.facing), s = Engine::Math::Sin(drop.facing);
			at = drop.at + FixedVector3{local.x * c - local.y * s, local.x * s + local.y * c, local.z};
		}
		if (run.inheritVelocity)
			at = at - drop.velocity;
		if (content.missiles.contains(run.payload))
		{
			// A projectile: fired at the target for its weapon (none given: the original stops there, it never flies).
			if (run.weapon.empty())
				continue;
			const std::uint32_t weapon = game.templates.Weapon(run.weapon + "|" + run.payload);
			if (weapon == gp::WeaponCatalog::None)
				continue;
			const gp::WeaponDefinition &definition = game.templates.weapons.At(weapon);
			gp::Shot shot;
			shot.source = drop.carrier;
			shot.weapon = weapon;
			shot.sourcePlayer = player;
			shot.origin = at;
			shot.aim = drop.target;
			shot.fireTick = game.tick;
			shot.impactTick = gp::LandsWithProjectile;
			shot.launchYaw = drop.facing;
			if (const auto *experience = world.Get<gp::Experience>(drop.carrier))
				shot.veterancy = experience->level;
			gp::MissileFlight flight = gp::LaunchMissile(shot, definition);
			if (run.inheritVelocity)
				flight.velocity = flight.velocity + drop.velocity;
			const ecs::Entity missile = world.Create();
			world.Add<gp::Transform>(missile);
			world.Add<gp::DefinitionRef>(missile);
			world.Add<gp::Owner>(missile);
			world.Add<gp::Attitude>(missile);
			world.Add<gp::MissileFlight>(missile);
			*world.Get<gp::Transform>(missile) = gp::Transform{at, drop.facing};
			*world.Get<gp::DefinitionRef>(missile) = gp::DefinitionRef{definition.projectileDefinition};
			*world.Get<gp::Owner>(missile) = gp::Owner{player};
			const FixedVector3 direction = flight.forward;
			*world.Get<gp::Attitude>(missile) = gp::Attitude{-Engine::Math::Heading({Engine::Math::Length(direction.XY()), direction.z}), {}};
			*world.Get<gp::MissileFlight>(missile) = flight;
			continue;
		}
		// A bomb.
		const std::uint32_t team = game.roster.DefaultTeam(player).value_or(0u);
		const ecs::Entity bomb = SpawnObject(game, run.payload, at.XY(), drop.facing, team, {});
		if (!world.IsAlive(bomb))
			continue;
		world.Get<gp::Transform>(bomb)->position = at;
		SetProducer(game, bomb, drop.carrier);
		if (auto *body = world.Get<gp::PhysicsBody>(bomb))
		{
			if (run.inheritVelocity)
				gp::ApplyMotiveForce(*body, drop.velocity * body->mass, game.tick);
			// PhysicsBehavior's CenterOfMassOffset (0 unless given) times the pitch rate.
			if (run.exitPitchRate != Fixed{})
				body->pitchRate = static_cast<std::int32_t>(Engine::Math::TurnFromRadians(body->centerOfMassOffset * run.exitPitchRate).units);
		}
		if (auto *order = world.Get<gp::MoveOrder>(bomb))
			*order = gp::MoveToPoint(drop.moveTo);
	}
}
}
