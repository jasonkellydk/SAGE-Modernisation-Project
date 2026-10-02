export module engine.gameplay.rts.movement.systems.path_points_sweep_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.movement.components.move_path;
export import engine.gameplay.rts.movement.resources.path_points;

// The goal paths' points let go (the original frees a goal path as its state machine clears it): as a tick ends, each
// block of PathPoints whose owner is gone, follows no path, or follows another block is freed for the next path to take.
// A batch: the blocks are few, walked in their order.
export namespace engine::gameplay
{
struct PathPointsSweepSystem
{
	using Query = ecs::Query<ecs::Read<MovePath>>;
	using Lookup = ecs::Lookup<ecs::Read<MovePath>>;
	using Resources = ecs::Resources<ecs::Write<PathPoints>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		PathPoints &points = context.Write<PathPoints>();
		const auto lookup = context.Lookup<Lookup>();
		for (std::uint32_t block = 0; block < points.BlockCount(); ++block)
		{
			if (!points.Used(block))
				continue;
			const ecs::Entity owner = points.Owner(block);
			const MovePath *path = lookup.IsAlive(owner) ? lookup.Get<MovePath>(owner) : nullptr;
			if (path == nullptr || path->block != block)
				points.Free(block);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::PathPointsSweepSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.path_points_sweep";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
