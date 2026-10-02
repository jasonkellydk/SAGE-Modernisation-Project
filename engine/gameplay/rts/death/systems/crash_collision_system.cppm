export module engine.gameplay.rts.death.systems.crash_collision_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.death.components.crash;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.death.definitions.death_definition;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.bounding_volume;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.rts.collision.resources.contacts;

// What a crashing wreck (a spiralling helicopter, a falling jet) runs into, after it moves (the partition's collisions at the end of the frame,
// PhysicsBehavior::onCollide): of the bodies whose footprint and height it overlaps, those that stand firm (IMMOBILE:
// a dead body's AI does not weigh in against those) and that it also meets sphere to sphere (in 3D while above the
// ground, else circle to circle), the last one (in entity order) is its last collidee (m_lastCollidee: kept until
// another). Its crash reads it on its next tick (Helicopter/JetSlowDeathBehavior::update: a tree there brings it down
// where it is). A body flung by its slow death, not down yet, keeps its last collidee the same way (SlowDeathBehavior::
// update: a tree there snags it).
export namespace engine::gameplay
{
struct CrashCollisionSystem
{
	using Query = ecs::Query<ecs::OptionalWrite<Crash>, ecs::Write<Dying>, ecs::Read<Transform>, ecs::Read<BoundingVolume>>;
	using Lookup = ecs::Lookup<ecs::Read<BoundingVolume>>;
	using Resources = ecs::Resources<ecs::Read<ColliderIndex>, ecs::Read<GroundHeight>, ecs::Read<DeathCatalog>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		const ColliderIndex &index = context.Read<ColliderIndex>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const DeathCatalog &catalog = context.Read<DeathCatalog>();
		const auto lookup = context.Lookup<Lookup>();
		auto crashes = chunk.Get<Crash>();
		auto dyings = chunk.Get<Dying>();
		const auto transforms = chunk.Get<Transform>();
		const auto volumes = chunk.Get<BoundingVolume>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < dyings.size(); ++row)
		{
			Crash *crash = crashes.empty() ? nullptr : &crashes[row];
			Dying &dying = dyings[row];
			const DeathDefinition &definition = catalog.At(dying.death);
			if (crash != nullptr)
			{
				if (crash->groundTick != 0 || dying.slow >= definition.slow.size() || definition.slow[dying.slow].crash.kind == CrashKind::None)
					continue;
			}
			else if (dying.flung == 0 || dying.landed != 0 || dying.snagged != 0)
				continue;
			const auto &at = transforms[row].position;
			const BoundingVolume &self = volumes[row];
			const bool above = at.z - ground.At(at.XY()) > Fixed{};
			const Engine::Math::FixedVector3 center{at.x, at.y, at.z + self.centerLift};
			ecs::Entity last{};
			index.ForEachWithin(at.XY(), self.sphereRadius + Fixed::FromInt(64), [&](const SpatialEntry &entry) {
				if (entry.entity == entities[row])
					return;
				const BoundingVolume *other = lookup.template Get<BoundingVolume>(entry.entity);
				if (other == nullptr || other->immobile == 0)
					return;
				// The partition's overlap: footprints (bounding circles) and heights.
				const Fixed reach = self.sphereRadius + entry.radius;
				if (Engine::Math::DistanceSquared(at.XY(), entry.position.XY()) > reach * reach)
					return;
				if (at.z - self.below > entry.position.z + other->above || entry.position.z - other->below > at.z + self.above)
					return;
				// onCollide's own test: bounding spheres from their centres (3D above the ground).
				const Fixed spheres = self.sphereRadius + other->sphereRadius;
				const Engine::Math::FixedVector3 otherCenter{entry.position.x, entry.position.y, entry.position.z + other->centerLift};
				const Fixed apart = above ? Engine::Math::DistanceSquared(center, otherCenter) : Engine::Math::DistanceSquared(center.XY(), otherCenter.XY());
				if (apart > spheres * spheres)
					return;
				if (last == ecs::Entity{} || entry.entity.index > last.index)
					last = entry.entity;
			});
			if (last != ecs::Entity{})
				(crash != nullptr ? crash->lastCollidee : dying.lastCollidee) = last;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::CrashCollisionSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.crash_collision";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	// The composition orders it after the slow deaths move the wrecks and the collider index is built.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
