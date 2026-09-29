export module engine.gameplay.rts.collision.systems.collide_weapon_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.collision.components.collide_weapon;
export import engine.gameplay.rts.collision.components.collider;
export import engine.gameplay.rts.collision.resources.contacts;
export import engine.gameplay.rts.combat.resources.shots;
export import engine.gameplay.common.fire.components.flammable;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.common.weapons.resources.weapon_catalog;

// FireWeaponCollide each tick, after movement and before the tick's impacts: the original collides a body that moved
// (its partition data dirty) with those it overlaps, both ways (PartitionContactList::processContactList); a body with
// a collide weapon that may fire (burning, when it must be: shouldFireWeapon; not fired yet when it fires once) fires
// it at each mover overlapping it (footprints, 2D), one shot each (loadAmmoNow, fireWeapon), in entity order.
export namespace engine::gameplay
{
struct CollideWeaponSystem
{
	using Query = ecs::Query<ecs::Write<CollideWeapon>, ecs::Read<Transform>, ecs::Read<Collider>, ecs::Optional<Flammable>, ecs::Optional<Owner>>;
	using Lookup = ecs::Lookup<ecs::Read<Locomotion>, ecs::Read<Transform>, ecs::Read<Health>>;
	using Resources = ecs::Resources<ecs::Read<ColliderIndex>, ecs::Read<WeaponCatalog>, ecs::Write<ShotQueue>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const ColliderIndex &index = context.Read<ColliderIndex>();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		ShotQueue &shots = context.Write<ShotQueue>();
		const auto lookup = context.Lookup<Lookup>();
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto collides = chunk.template Get<CollideWeapon>();
			const auto transforms = chunk.template Get<Transform>();
			const auto colliders = chunk.template Get<Collider>();
			const auto flames = chunk.template Get<Flammable>();
			const auto owners = chunk.template Get<Owner>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < collides.size(); ++row)
			{
				CollideWeapon &collide = collides[row];
				if (collide.weapon == WeaponCatalog::None || (collide.fireOnce != 0 && collide.fired != 0))
					continue;
				if (collide.requiresAflame != 0 && (flames.empty() || flames[row].state != FlameState::Aflame))
					continue;
				const auto &at = transforms[row].position;
				std::vector<ecs::Entity> movers;
				index.ForEachWithin(at.XY(), colliders[row].radius, [&](const SpatialEntry &entry) {
					if (entry.entity == entities[row])
						return;
					const Locomotion *motion = lookup.template Get<Locomotion>(entry.entity);
					if (motion == nullptr || motion->speed <= Engine::Math::Fixed{})
						return;
					if (const Health *health = lookup.template Get<Health>(entry.entity); health != nullptr && IsDead(*health))
						return;
					movers.push_back(entry.entity);
				});
				std::sort(movers.begin(), movers.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
				const WeaponDefinition &weapon = weapons.At(collide.weapon);
				for (const ecs::Entity mover : movers)
				{
					const Transform *victim = lookup.template Get<Transform>(mover);
					const Engine::Math::Fixed distance = Engine::Math::Distance(at.XY(), victim->position.XY());
					const std::uint64_t travel = weapon.speed > Engine::Math::Fixed{} ? static_cast<std::uint64_t>((distance / weapon.speed).Ceil()) : 0;
					Shot shot;
					shot.source = entities[row];
					shot.target = mover;
					shot.weapon = collide.weapon;
					shot.sourcePlayer = owners.empty() ? 0u : owners[row].player;
					shot.origin = at;
					shot.aim = victim->position;
					shot.fireTick = tick;
					shot.impactTick = tick + travel;
					shots.Add(shot);
					collide.fired = 1;
					if (collide.fireOnce != 0)
						break;
				}
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::CollideWeaponSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.collide_weapon";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after movement and before the tick's impacts.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
