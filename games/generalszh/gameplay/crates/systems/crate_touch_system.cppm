export module games.generalszh.gameplay.crates.systems.crate_touch_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.crates.resources.crates;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.rts.collision.components.collider;
export import engine.gameplay.rts.collision.resources.contacts;

// Who runs into a crate (the original's physics collision calling
// CrateCollide::onCollide), in parallel per chunk: every other body whose
// footprint overlaps a crate resting on the ground (a crate still coming down
// under its parachute cannot be picked up), in entity order per crate.
export namespace generalszh::gameplay
{
struct CrateTouchSystem
{
	using Query = ecs::Query<ecs::Read<Crate>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::Collider>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::ColliderIndex>, ecs::Read<engine::gameplay::GroundHeight>, ecs::Write<CrateTouches>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<CrateTouches>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const auto &index = context.Read<engine::gameplay::ColliderIndex>();
		const auto &ground = context.Read<engine::gameplay::GroundHeight>();
		auto &out = context.Write<CrateTouches>().Slot(context);
		const auto transforms = chunk.Get<engine::gameplay::Transform>();
		const auto colliders = chunk.Get<engine::gameplay::Collider>();
		const auto entities = chunk.Entities();
		std::vector<ecs::Entity> touching;
		for (std::size_t row = 0; row < transforms.size(); ++row)
		{
			const auto &at = transforms[row].position;
			if (at.z - ground.At(at.XY()) > Engine::Math::Fixed::FromRatio(1, 10))
				continue; // Thing::isAboveTerrain
			touching.clear();
			index.ForEachWithin(at.XY(), colliders[row].radius, [&](const engine::gameplay::SpatialEntry &entry) {
				if (entry.entity == entities[row])
					return;
				const Engine::Math::Fixed reach = colliders[row].radius + entry.radius;
				if (Engine::Math::DistanceSquared(at.XY(), entry.position.XY()) <= reach * reach)
					touching.push_back(entry.entity);
			});
			std::sort(touching.begin(), touching.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
			for (const ecs::Entity toucher : touching)
				out.push_back({entities[row], toucher});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::CrateTouchSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.crate_touch";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
