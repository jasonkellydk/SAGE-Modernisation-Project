export module engine.gameplay.common.fire.systems.fire_spread_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.fire.components.flammable;
export import engine.gameplay.common.fire.components.fire_spread;
export import engine.gameplay.common.fire.resources.ignitions;
export import engine.gameplay.common.fire.systems.flammability_system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.random.resources.random_seed;
import Engine.Core.Math.FixedRandom;

// Fire spreading, in parallel per chunk, before the flammability pass: a
// burning thing's first try comes a random delay after it caught fire; on
// each try it throws embers and offers to set alight the closest thing in
// its range (found in this tick's spatial index) that would catch fire now
// (not burning, never burned: read from last tick's state). The offers are
// gathered by target, and the flammability pass sets them alight.
export namespace engine::gameplay
{
struct FireSpreadSystem
{
	using Query = ecs::Query<ecs::Read<Flammable>, ecs::Write<FireSpread>, ecs::Read<Transform>>;
	using Lookup = ecs::Lookup<ecs::Read<Flammable>>;
	using Resources = ecs::Resources<ecs::Read<RandomSeed>, ecs::Read<SpatialIndex>, ecs::Write<IgnitionOffers>, ecs::Write<Ignitions>, ecs::Write<SpreadTries>>;

	static bool WouldIgnite(const Flammable &fire) noexcept { return fire.state == FlameState::Normal && fire.burned == 0; }

	void BeforeChunks(Query &query, ecs::SystemContext &context) const
	{
		context.Write<IgnitionOffers>().Reset(query.PreparedChunkCount());
		context.Write<SpreadTries>().Reset(query.PreparedChunkCount());
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const auto lookup = context.Lookup<Lookup>();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0xF1E5u;
		auto &offers = context.Write<IgnitionOffers>().Slot(context);
		auto &tries = context.Write<SpreadTries>().Slot(context);
		const std::uint64_t tick = context.Tick();
		const auto flammables = chunk.Get<Flammable>();
		auto spreads = chunk.Get<FireSpread>();
		const auto transforms = chunk.Get<Transform>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < spreads.size(); ++row)
		{
			FireSpread &spread = spreads[row];
			if (flammables[row].state != FlameState::Aflame)
			{
				spread.nextTry = 0;
				continue;
			}
			const auto delay = [&] {
				auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation});
				const auto ticks = Engine::Math::UniformInt(random, static_cast<std::int64_t>(spread.minDelay),
					static_cast<std::int64_t>(std::max(spread.minDelay, spread.maxDelay)));
				return tick + static_cast<std::uint64_t>(std::max<std::int64_t>(ticks, 1));
			};
			if (spread.nextTry == 0)
			{
				spread.nextTry = delay(); // just caught fire
				continue;
			}
			if (tick < spread.nextTry)
				continue;
			spread.nextTry = delay();
			const auto &at = transforms[row].position;
			tries.push_back({entities[row], at});
			if (spread.range <= Engine::Math::Fixed{})
				continue;
			// The closest (in 3D, ties to the first found) that would catch fire.
			ecs::Entity closest;
			Engine::Math::Fixed best;
			const Engine::Math::Fixed reach = spread.range * spread.range;
			spatial.ForEachWithin(at.XY(), spread.range, [&](const SpatialEntry &entry) {
				if (entry.entity == entities[row])
					return;
				const Flammable *other = lookup.Get<Flammable>(entry.entity);
				if (other == nullptr || !WouldIgnite(*other))
					return;
				const Engine::Math::Fixed distance = Engine::Math::DistanceSquared(entry.position, at);
				if (distance > reach || (closest.IsValid() && distance >= best))
					return;
				closest = entry.entity;
				best = distance;
			});
			if (closest.IsValid())
				offers.push_back({closest, entities[row]});
		}
	}

	void AfterChunks(Query &, ecs::SystemContext &context) const { context.Write<Ignitions>().Gather(context.Write<IgnitionOffers>()); }
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::FireSpreadSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.fire_spread";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// Before the flammability pass sets its offers alight.
	using Before = SystemTypeList<engine::gameplay::FlammabilitySystem>;
	using After = SystemTypeList<>;
};
}
