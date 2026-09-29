export module engine.gameplay.rts.collision.systems.body_collision_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.collision.components.body_collision;
export import engine.gameplay.rts.collision.components.collider;
export import engine.gameplay.rts.collision.resources.contacts;
export import engine.gameplay.rts.collision.resources.collision_settings;
export import engine.gameplay.common.physics.components.physics_body;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.bounding_volume;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.components.carried;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.lifetime.components.lifetime;
export import engine.gameplay.rts.combat.resources.shots;
export import engine.gameplay.common.weapons.resources.weapon_catalog;

// Bodies with physics pushed apart by what they run into, after the tick's movement (PartitionManager's collisions at
// the end of the frame, PhysicsBehavior::onCollide; the push is a force its physics feels next tick). In entity order,
// each body checks what its footprint and height overlap (the partition), where it or the other moved:
//   what it may not collide with: nothing without physics that does not stand firm (IMMOBILE), what it crushes or is
//   crushed by as it moves (checkForOverlapCollision: the crush handles those), and, for a body with an AI, anything
//   unless it is dead and the other stands firm (processCollision declines);
//   then they must meet sphere to sphere from their centres (3D while it is above the ground, else circle to circle).
// Against what stands firm it bounces as off the ground (its speed, at least 1/150 a frame, times its mass times the
// StructureStiffness, straight back from the other's centre) and its velocity is thrown away; falling on to it from
// above DefaultStructureRubbleHeight it is lost: into a structure it is deleted (a vehicle's
// VehicleCrashesIntoBuildingWeapon goes off first), into anything else a vehicle's VehicleCrashesIntoNonBuildingWeapon
// goes off. Against another body it is pushed away by how far they overlap, at most 5.
export namespace engine::gameplay
{
struct BodyCollisionSystem
{
	using Query = ecs::Query<ecs::Write<PhysicsBody>, ecs::Read<BodyCollision>, ecs::Read<Transform>, ecs::Read<BoundingVolume>, ecs::Read<Collider>,
		ecs::Optional<Health>, ecs::Optional<Owner>, ecs::Exclude<OffMap>, ecs::Exclude<Carried>>;
	using Lookup = ecs::Lookup<ecs::Read<Transform>, ecs::Read<BoundingVolume>, ecs::Read<PhysicsBody>, ecs::Read<Collider>, ecs::Read<BodyCollision>>;
	using Resources = ecs::Resources<ecs::Read<ColliderIndex>, ecs::Read<GroundHeight>, ecs::Read<CollisionSettings>, ecs::Read<WeaponCatalog>,
		ecs::Write<ShotQueue>>;

	static bool Moving(const PhysicsBody &body) noexcept
	{
		const Engine::Math::Fixed tiny = Engine::Math::Fixed::FromRatio(1, 1000);
		return Engine::Math::Abs(body.velocity.x) > tiny || Engine::Math::Abs(body.velocity.y) > tiny || Engine::Math::Abs(body.velocity.z) > tiny;
	}

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		using Engine::Math::FixedVector3;
		const ColliderIndex &index = context.Read<ColliderIndex>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const CollisionSettings &settings = context.Read<CollisionSettings>();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		ShotQueue &shots = context.Write<ShotQueue>();
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		const std::uint64_t tick = context.Tick();
		const Fixed stiffness = Engine::Math::Clamp(settings.structureStiffness, Fixed::FromRatio(1, 100), Fixed::FromRatio(99, 100));
		query.ForEachChunk([&](auto chunk) {
			auto bodies = chunk.template Get<PhysicsBody>();
			const auto responses = chunk.template Get<BodyCollision>();
			const auto transforms = chunk.template Get<Transform>();
			const auto volumes = chunk.template Get<BoundingVolume>();
			const auto colliders = chunk.template Get<Collider>();
			const auto healths = chunk.template Get<Health>();
			const auto owners = chunk.template Get<Owner>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < bodies.size(); ++row)
			{
				PhysicsBody &body = bodies[row];
				const BodyCollision &response = responses[row];
				const bool dead = !healths.empty() && IsDead(healths[row]);
				// An AI's body is pushed only dead, and then only by what stands firm.
				if (response.Has(body_collision_flag::HasAI) && !dead)
					continue;
				const auto &at = transforms[row].position;
				const BoundingVolume &self = volumes[row];
				const bool moving = Moving(body);
				const bool above = at.z - ground.At(at.XY()) > Fixed{};
				const FixedVector3 center{at.x, at.y, at.z + self.centerLift};
				std::vector<std::pair<ecs::Entity, FixedVector3>> met;
				index.ForEachWithin(at.XY(), self.sphereRadius + Fixed::FromInt(64), [&](const SpatialEntry &entry) {
					if (entry.entity == entities[row] || entry.entity == response.ignored)
						return;
					// Ignored by it (isIgnoringCollisionsWith, the other way).
					if (const BodyCollision *theirs = lookup.template Get<BodyCollision>(entry.entity); theirs != nullptr && theirs->ignored == entities[row])
						return;
					const BoundingVolume *other = lookup.template Get<BoundingVolume>(entry.entity);
					const PhysicsBody *otherBody = lookup.template Get<PhysicsBody>(entry.entity);
					if (other == nullptr || (otherBody == nullptr && other->immobile == 0))
						return;
					if (response.Has(body_collision_flag::HasAI) && other->immobile == 0)
						return;
					// Only where one of them moved (the partition's dirty data).
					if (!moving && (otherBody == nullptr || !Moving(*otherBody)))
						return;
					// The crush's own business while it moves.
					if (const Collider *otherCollider = lookup.template Get<Collider>(entry.entity); moving && otherCollider != nullptr &&
						(otherCollider->crusherLevel > colliders[row].crushableLevel || colliders[row].crusherLevel > otherCollider->crushableLevel))
						return;
					// The partition's overlap: footprints and heights.
					const Fixed reach = self.circleRadius + other->circleRadius;
					if (Engine::Math::DistanceSquared(at.XY(), entry.position.XY()) > reach * reach)
						return;
					if (at.z - self.below > entry.position.z + other->above || entry.position.z - other->below > at.z + self.above)
						return;
					const FixedVector3 otherCenter{entry.position.x, entry.position.y, entry.position.z + other->centerLift};
					met.emplace_back(entry.entity, otherCenter);
				});
				std::sort(met.begin(), met.end(), [](const auto &a, const auto &b) { return a.first.index < b.first.index; });
				bool lost = false;
				for (const auto &[otherEntity, otherCenter] : met)
				{
					if (lost)
						break;
					const BoundingVolume &other = *lookup.template Get<BoundingVolume>(otherEntity);
					// onCollide's test: spheres in 3D above the ground, circles on it.
					FixedVector3 delta = otherCenter - center;
					Fixed usRadius = self.sphereRadius, themRadius = other.sphereRadius;
					if (!above)
					{
						delta.z = {};
						usRadius = self.circleRadius;
						themRadius = other.circleRadius;
					}
					const Fixed distanceSquared = Engine::Math::Dot(delta, delta);
					const Fixed together = usRadius + themRadius;
					if (distanceSquared > together * together || response.Has(body_collision_flag::NoForce))
						continue;
					Fixed distance = Engine::Math::Sqrt(distanceSquared);
					const Fixed overlap = together - distance;
					if (distance < Fixed::One())
						distance = Fixed::One();
					Fixed factor;
					if (other.immobile != 0)
					{
						const Fixed speed = Engine::Math::Max(Engine::Math::Length(body.velocity), Fixed::FromRatio(1, 150));
						factor = Fixed{} - speed * body.mass * stiffness;
						// Falling on to it from high up: lost in it.
						if (delta.z < Fixed{} && at.z >= settings.rubbleHeight)
						{
							const bool vehicle = response.Has(body_collision_flag::Vehicle);
							const std::uint32_t weapon = other.structure != 0 ? response.buildingCrashWeapon : response.otherCrashWeapon;
							if (vehicle && weapon != WeaponCatalog::None)
							{
								Shot shot;
								shot.source = entities[row];
								shot.weapon = weapon;
								shot.sourcePlayer = owners.empty() ? 0u : owners[row].player;
								shot.origin = at;
								shot.aim = at;
								shot.fireTick = tick;
								shot.impactTick = tick;
								shots.Add(shot);
							}
							if (other.structure != 0)
							{
								commands.template Add<Lifetime>(entities[row], Lifetime{tick, 1, 0});
								lost = true;
								break;
							}
						}
						body.velocity = {};
					}
					else
						factor = Fixed{} - Engine::Math::Min(overlap, Fixed::FromInt(5));
					// PhysicsBehavior::applyForce: a = F / m.
					const Fixed scale = factor / distance / body.mass;
					body.acceleration.x += delta.x * scale;
					body.acceleration.y += delta.y * scale;
					body.acceleration.z += delta.z * scale;
				}
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::BodyCollisionSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.body_collision";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after movement and the collider index, before the tick's impacts.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
