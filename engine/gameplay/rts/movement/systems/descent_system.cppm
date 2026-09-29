export module engine.gameplay.rts.movement.systems.descent_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.movement.components.descent;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.spatial.systems.spatial_index_system;

// Lowers descending entities toward the surface, chunk-parallel; on touching
// down an entity drops its descent (through the tick's command buffer) and
// moves normally again.
export namespace engine::gameplay
{
struct DescentSystem
{
	using Query = ecs::Query<ecs::Write<Transform>, ecs::Read<Descent>, ecs::Exclude<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<GroundHeight>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const GroundHeight &ground = context.Read<GroundHeight>();
		auto transforms = chunk.Get<Transform>();
		const auto descents = chunk.Get<Descent>();
		const auto entities = chunk.Entities();
		auto &commands = context.Commands();
		for (std::size_t row = 0; row < transforms.size(); ++row)
		{
			auto &position = transforms[row].position;
			const Engine::Math::Fixed surface = ground.Surface(position.XY());
			position.z -= descents[row].rate;
			if (position.z <= surface)
			{
				position.z = surface;
				commands.Remove<Descent>(entities[row]);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::DescentSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.descent";
	// Before the spatial index, so this tick's queries see where it came down to.
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<engine::gameplay::SpatialIndexSystem>;
	using After = SystemTypeList<>;
};
}
