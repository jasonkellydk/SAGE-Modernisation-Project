export module engine.gameplay.rts.combat.systems.projectile_flight_system;
import engine.gameplay.common.health.components.health;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.sneaky_target;
export import engine.gameplay.rts.combat.components.projectile;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.attitude;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.rts.combat.algorithms.projectile_collision;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.rts.combat.systems.auto_fire_system;
export import engine.gameplay.common.spatial.systems.spatial_index_system;
export import engine.gameplay.rts.combat.resources.garrison_kills;
export import engine.gameplay.common.identity.components.definition_ref;

// Projectiles fly, chunk-parallel, as DumbProjectileBehavior::update: out of
// points, one detonates where it is (its shot lands there this tick) and is
// gone; else its arc's end follows its victim up to its follow distance (the
// path recalculated over the same number of points), and it moves to the
// path's next point, facing along the path from the previous point when it
// orients to it (its first point faces as the second will). One that clears garrisons (GarrisonHitKillCount) running
// into a structure does not detonate there yet: GarrisonClearSystem decides (a garrison hit).
export namespace engine::gameplay
{
struct ProjectileFlightSystem
{
	using Query = ecs::Query<ecs::Write<ProjectileFlight>, ecs::Write<Transform>, ecs::OptionalWrite<Attitude>, ecs::Optional<DefinitionRef>, ecs::Optional<Health>>;
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Read<GroundHeight>, ecs::Read<WeaponCatalog>, ecs::Read<Relationships>,
		ecs::Write<Detonations>, ecs::Write<GarrisonHits>>;
	using Lookup = ecs::Lookup<ecs::Read<SneakyTarget>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context)
	{
		context.Write<Detonations>().Reset(query.PreparedChunkCount());
		context.Write<GarrisonHits>().Reset(query.PreparedChunkCount());
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		using Engine::Math::FixedVector3;
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const Relationships &relationships = context.Read<Relationships>();
		auto &detonated = context.Write<Detonations>().Slot(context);
		auto &garrisonHits = context.Write<GarrisonHits>().Slot(context);
		const auto definitions = chunk.Get<DefinitionRef>();
		auto flights = chunk.Get<ProjectileFlight>();
		auto transforms = chunk.Get<Transform>();
		auto attitudes = chunk.Get<Attitude>();
		const auto bodies = chunk.Get<Health>();
		const auto entities = chunk.Entities();
		// Gone off: DetonateCallsKill with a body keeps it for the impact to kill (its die modules run); else it is taken away.
		const auto spent = [&](std::size_t row, const ProjectileFlight &flight) {
			if (flight.arc.callsKill != 0 && !bodies.empty())
				context.Commands().Remove<ProjectileFlight>(entities[row]);
			else
				context.Commands().Destroy(entities[row]);
		};
		for (std::size_t row = 0; row < flights.size(); ++row)
		{
			ProjectileFlight &flight = flights[row];
			// Its lifespan up (MaxLifespan), or out of points: detonate().
			if (context.Tick() >= flight.shot.fireTick + flight.arc.maxLifespan || flight.step >= flight.segments)
			{
				// detonate(): its detonation weapon goes off where it is.
				Shot landed = flight.shot;
				landed.aim = transforms[row].position;
				landed.carrier = entities[row];
				detonated.push_back(landed);
				spent(row, flight);
				continue;
			}
			// FlightPathAdjustDistPerSecond: the end moves toward where the victim is now, then the path is recalculated.
			if (flight.arc.followPerTick > Fixed{})
				if (const SpatialEntry *victim = spatial.Find(flight.shot.target))
				{
					const FixedVector3 delta = victim->position - flight.points[3];
					if (Engine::Math::LengthSquared(delta) > Fixed::FromRatio(1, 10))
					{
						const Fixed moved = Engine::Math::Length(delta);
						const Fixed step = moved > flight.arc.followPerTick ? flight.arc.followPerTick : moved;
						const FixedVector3 end = flight.points[3] + delta * (step / moved);
						flight.points = ArcControlPoints(flight.points[0], end, flight.arc,
							HighestTerrainAlong(ground, flight.points[0].XY(), end.XY()));
					}
				}
			const FixedVector3 at = ArcPoint(flight.points, flight.segments, flight.step);
			if (flight.arc.orientToPath)
			{
				// From_Unit_Forward_Direction: the step from the previous point (the first as the second).
				const FixedVector3 from = flight.step > 0 ? ArcPoint(flight.points, flight.segments, flight.step - 1)
					: flight.segments >= 2 ? at : flight.points[0];
				const FixedVector3 to = flight.step > 0 ? at : flight.segments >= 2 ? ArcPoint(flight.points, flight.segments, 1) : flight.points[3];
				const FixedVector3 step = to - from;
				if (step.x != Fixed{} || step.y != Fixed{})
					transforms[row].facing = Engine::Math::Heading(step.XY());
				if (!attitudes.empty())
					// Nose up is a negative pitch (the draw's Ry).
					attitudes[row].pitch = -Engine::Math::Heading({Engine::Math::Length(step.XY()), step.z});
			}
			else if (flight.arc.tumble)
			{
				// TumbleRandomly: its physics spins it by its rates every frame.
				transforms[row].facing += flight.tumble[0];
				if (!attitudes.empty())
				{
					attitudes[row].pitch += flight.tumble[1];
					attitudes[row].roll += flight.tumble[2];
				}
			}
			transforms[row].position = at;
			++flight.step;
			// Running into something on the way (projectileHandleCollision): it goes off there.
			const WeaponDefinition &weapon = weapons.At(flight.shot.weapon);
			const auto sneaky = [&](ecs::Entity thing) {
				const SneakyTarget *miss = context.Lookup<Lookup>().Get<SneakyTarget>(thing);
				return miss != nullptr && miss->Active(context.Tick());
			};
			if (const SpatialEntry *other = ProjectileCollision(weapon, flight.shot, entities[row], at, weapon.projectileRadius, spatial, relationships, sneaky))
			{
				Shot landed = flight.shot;
				landed.aim = at;
				landed.carrier = entities[row];
				if (flight.arc.garrisonHitKill > 0 && (other->classes & target_class::Structure) != 0)
					garrisonHits.push_back({landed, other->entity, flight.arc.garrisonHitKill, flight.arc.garrisonHitRequired, flight.arc.garrisonHitForbidden,
						definitions.empty() ? 0xFFFFFFFFu : definitions[row].index});
				else
					detonated.push_back(landed);
				if (flight.arc.garrisonHitKill > 0 && (other->classes & target_class::Structure) != 0)
					context.Commands().Destroy(entities[row]);
				else
					spent(row, flight);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::ProjectileFlightSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.projectile_flight";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// After the tick's shots and the index it follows victims in; its detonations land this tick.
	using Before = SystemTypeList<engine::gameplay::ImpactSystem, engine::gameplay::AutoFireSystem>;
	using After = SystemTypeList<engine::gameplay::WeaponSystem, engine::gameplay::SpatialIndexSystem>;
};
}
