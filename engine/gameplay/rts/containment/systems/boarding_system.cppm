export module engine.gameplay.rts.containment.systems.boarding_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.rts.combat.systems.targeting_system;

// Walks boarding units to their transport, chunk-parallel, and asks to get
// in once they reach it (the session puts them aboard between ticks); on the
// way, it lets the transport know it is awaited. A transport still in the air
// (more than BoardingHeight above the unit) cannot be reached until it comes
// down. A transport that is gone cancels the boarding (a request without
// transport).
export namespace engine::gameplay
{
// How far above a boarding unit its transport may still be to get in.
inline Engine::Math::Fixed BoardingHeight() noexcept { return Engine::Math::Fixed::FromInt(15); }

struct BoardingSystem
{
	using Query = ecs::Query<ecs::Read<Transform>, ecs::Read<Boarding>, ecs::OptionalWrite<MoveOrder>, ecs::Exclude<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Write<BoardRequests>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<BoardRequests>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		BoardRequests &requests = context.Write<BoardRequests>();
		const auto transforms = chunk.Get<Transform>();
		const auto boardings = chunk.Get<Boarding>();
		auto moves = chunk.Get<MoveOrder>();
		const auto entities = chunk.Entities();
		auto &out = requests.Slot(context);
		for (std::size_t row = 0; row < transforms.size(); ++row)
		{
			const SpatialEntry *transport = spatial.Find(boardings[row].transport);
			if (transport == nullptr)
			{
				out.push_back({entities[row], {}});
				continue;
			}
			// Touching it (AIEnterState: the collision that puts it in), where its path ends: its footprint as the grid
			// blocks it reaches up to two cells past its bounding circle.
			const Engine::Math::Fixed reach = transport->radius + Engine::Math::Fixed::FromInt(20);
			const bool near = Engine::Math::DistanceSquared(transforms[row].position.XY(), transport->position.XY()) <= reach * reach;
			const bool down = transport->position.z - transforms[row].position.z <= BoardingHeight();
			if (near && down)
			{
				out.push_back({entities[row], boardings[row].transport, true, boardings[row].touchOnly != 0});
				continue;
			}
			out.push_back({entities[row], boardings[row].transport, false, boardings[row].touchOnly != 0});
			if (!near && !moves.empty())
				moves[row] = MoveToPoint(transport->position.XY(), GoalClaim::None); // AIEnterState: no adjusting, no claim
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::BoardingSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.boarding";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<engine::gameplay::MovementSystem>;
	using After = SystemTypeList<engine::gameplay::TargetingSystem>;
};
}
