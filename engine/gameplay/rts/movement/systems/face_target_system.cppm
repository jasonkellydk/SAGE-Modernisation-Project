export module engine.gameplay.rts.movement.systems.face_target_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.movement.components.face_target;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.common.spatial.components.transform;

// Facing an object (AIFaceState with a goal object), each tick before movement, chunk-parallel: the point to face is
// where the object is now; once it is gone there is nothing to face and the unit idles (STATE_FAILURE).
export namespace engine::gameplay
{
struct FaceTargetSystem
{
	using Query = ecs::Query<ecs::Write<MoveOrder>, ecs::Read<FaceTarget>>;
	using Lookup = ecs::Lookup<ecs::Read<Transform>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		auto orders = chunk.Get<MoveOrder>();
		const auto targets = chunk.Get<FaceTarget>();
		for (std::size_t row = 0; row < orders.size(); ++row)
		{
			if (orders[row].mode != MoveMode::FaceObject)
				continue;
			const Transform *at = lookup.template Get<Transform>(targets[row].target);
			if (at == nullptr)
				orders[row].mode = MoveMode::Idle;
			else
				orders[row].destination = at->position.XY();
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::FaceTargetSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.face_target";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it before movement.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
