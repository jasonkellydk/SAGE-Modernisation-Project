export module engine.gameplay.rts.combat.systems.missile_jam_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.missile;
export import engine.gameplay.common.health.components.subdual;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.random.resources.random_seed;
import Engine.Core.Math.FixedRandom;

// MissileAIUpdate::projectileNowJammed, after the tick's subdual damage (ActiveBody::onSubdualChange for a projectile):
// a missile subdued this tick, not jammed yet, is jammed for good: its goal becomes where it was going (its victim's
// position while it tracks one, else its goal spot) scattered by up to DistanceScatterWhenJammed each way, on the
// ground there; it stops tracking and has no victim any more (aiMoveToPosition). It flies on for the new spot from its
// next update (the original turns it within the damage). A batch over the missiles, only on ticks something subdued.
export namespace engine::gameplay
{
struct MissileJamSystem
{
	using Query = ecs::Query<ecs::Write<MissileFlight>>;
	using Lookup = ecs::Lookup<ecs::Read<Transform>>;
	using Resources = ecs::Resources<ecs::Read<SubdualChanges>, ecs::Read<WeaponCatalog>, ecs::Read<GroundHeight>, ecs::Read<RandomSeed>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const auto &changes = context.Read<SubdualChanges>().list;
		if (std::none_of(changes.begin(), changes.end(), [](const SubdualChange &change) { return change.subdued && change.projectile; }))
			return;
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0x7A33u;
		const std::uint64_t tick = context.Tick();
		const auto lookup = context.Lookup<Lookup>();
		query.ForEachChunk([&](auto chunk) {
			auto missiles = chunk.template Get<MissileFlight>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < missiles.size(); ++row)
			{
				const bool struck = std::any_of(changes.begin(), changes.end(),
					[&](const SubdualChange &change) { return change.subdued && change.projectile && change.entity == entities[row]; });
				MissileFlight &m = missiles[row];
				if (!struck || m.jammed != 0)
					continue;
				m.jammed = 1;
				Engine::Math::FixedVector3 target = m.goal;
				if (m.tracking && lookup.IsAlive(m.shot.target))
					if (const Transform *victim = lookup.Get<Transform>(m.shot.target))
						target = victim->position;
				const Engine::Math::Fixed scatter = weapons.At(m.shot.weapon).missile.jamScatter;
				auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation});
				target.x += Engine::Math::UniformFixed(random, -scatter, scatter);
				target.y += Engine::Math::UniformFixed(random, -scatter, scatter);
				target.z = ground.At(target.XY());
				m.goal = target;
				m.originalGoal = target;
				m.tracking = false;
				m.shot.target = {};
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::MissileJamSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.missile_jam";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the tick's subdual damage is weighed.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
