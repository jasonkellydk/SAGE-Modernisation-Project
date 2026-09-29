export module engine.gameplay.rts.collision.systems.collision_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.collision.components.collider;
export import engine.gameplay.rts.collision.resources.contacts;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.movement.systems.movement_system;
export import engine.gameplay.common.identity.components.owner;

// Collisions, in two passes:
// - ColliderIndexSystem (before the step): every body where it is, gathered
//   per chunk into the collider index (sorted: order-independent).
// - ContactSystem (after movement): each moving crusher finds the bodies it
//   touches in the index, in parallel per chunk; the contacts are ordered by
//   the body run into once the chunks are done.
export namespace engine::gameplay
{
struct ColliderIndexSystem
{
	using Query = ecs::Query<ecs::Read<Transform>, ecs::Read<Collider>, ecs::Exclude<OffMap>>;
	using Resources = ecs::Resources<ecs::Write<ColliderIndex>, ecs::Write<ColliderGather>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<ColliderGather>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		auto &out = context.Write<ColliderGather>().Slot(context);
		const auto transforms = chunk.Get<Transform>();
		const auto colliders = chunk.Get<Collider>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < colliders.size(); ++row)
			out.push_back({entities[row], transforms[row].position, colliders[row].radius, 0, colliders[row].crushableLevel});
	}

	void AfterChunks(Query &, ecs::SystemContext &context) const
	{
		std::vector<SpatialEntry> entries;
		ColliderGather &gather = context.Write<ColliderGather>();
		entries.reserve(gather.Size());
		gather.AppendTo(entries);
		context.Write<ColliderIndex>().Rebuild(std::move(entries));
	}
};

struct ContactSystem
{
	using Query = ecs::Query<ecs::Read<Transform>, ecs::Read<Collider>, ecs::Read<Locomotion>, ecs::Optional<Owner>, ecs::Exclude<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<ColliderIndex>, ecs::Write<ContactOffers>, ecs::Write<Contacts>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<ContactOffers>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const ColliderIndex &index = context.Read<ColliderIndex>();
		auto &out = context.Write<ContactOffers>().Slot(context);
		const auto transforms = chunk.Get<Transform>();
		const auto colliders = chunk.Get<Collider>();
		const auto motions = chunk.Get<Locomotion>();
		const auto owners = chunk.Get<Owner>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < colliders.size(); ++row)
		{
			const Collider &collider = colliders[row];
			if (collider.crusherLevel == 0 || motions[row].speed <= Engine::Math::Fixed{})
				continue;
			const auto &at = transforms[row].position;
			index.ForEachWithin(at.XY(), collider.radius, [&](const SpatialEntry &entry) {
				if (entry.entity == entities[row])
					return;
				const Engine::Math::Fixed reach = collider.radius + entry.radius;
				if (Engine::Math::DistanceSquared(at.XY(), entry.position.XY()) <= reach * reach)
					out.push_back({entry.entity, entities[row], at, motions[row].speed, collider.radius, transforms[row].facing, collider.crusherLevel,
						owners.empty() ? 0u : owners[row].player});
			});
		}
	}

	void AfterChunks(Query &, ecs::SystemContext &context) const { context.Write<Contacts>().Gather(context.Write<ContactOffers>()); }
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::ColliderIndexSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.collider_index";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<engine::gameplay::ContactSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.contacts";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// Where the movers got to this tick.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::MovementSystem>;
};
}
