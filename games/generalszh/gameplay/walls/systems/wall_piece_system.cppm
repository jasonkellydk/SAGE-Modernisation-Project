export module games.generalszh.gameplay.walls.systems.wall_piece_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import games.generalszh.gameplay.walls.components.wall_piece;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.rts.death.components.dying;

// The wall's pieces, chunk-parallel: a piece still holding the wall up whose body has fallen to rubble (no health left,
// or dead: ActiveBody's BODY_RUBBLE for a structure, which takes it out of the pathfinder: Pathfinder::
// classifyObjectFootprint removing a WALK_ON_TOP_OF_WALL piece) goes out as its chunk's WallEvents, for the session to
// carry out once the systems have run (ApplyWallEvents: the piece out of the wall, what stood on it falls, the wall's
// cells classified afresh).
export namespace generalszh::gameplay
{
struct WallEvents : ecs::ChunkOutputs<ecs::Entity>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::WallEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.wall_events";
};
}

export namespace generalszh::gameplay
{
struct WallPieceSystem
{
	using Query = ecs::Query<ecs::Read<WallPiece>, ecs::Read<engine::gameplay::Health>, ecs::Optional<engine::gameplay::Dying>>;
	using Resources = ecs::Resources<ecs::Write<WallEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<WallEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		auto &events = context.Write<WallEvents>().Slot(context);
		const auto pieces = chunk.Get<WallPiece>();
		const auto healths = chunk.Get<engine::gameplay::Health>();
		const auto entities = chunk.Entities();
		const bool dying = !chunk.Get<engine::gameplay::Dying>().empty();
		for (std::size_t row = 0; row < pieces.size(); ++row)
			if (pieces[row].valid != 0 && (dying || healths[row].current <= Engine::Math::Fixed{}))
				events.push_back(entities[row]);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::WallPieceSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.wall_piece";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
