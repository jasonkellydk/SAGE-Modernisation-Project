export module engine.gameplay.rts.vision.systems.object_shroud_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.vision.algorithms.footprint_cells;
export import engine.gameplay.common.spatial.components.object_shroud;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;

// Before the step (and its spatial index, which carries it): how each player sees each object through the shroud as the
// last step left it (Object::getShroudedStatus, asked during the original's frame before its partition update). An
// ALWAYS_VISIBLE object, or one inside another (out of the partition), is clear to everyone. Chunk-parallel: each
// object only reads the shroud.
export namespace engine::gameplay
{
struct ObjectShroudSystem
{
	using Query = ecs::Query<ecs::Write<ObjectShroud>, ecs::Read<PartitionFootprint>, ecs::Read<Transform>, ecs::Optional<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<ShroudMap>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const ShroudMap &map = context.Read<ShroudMap>();
		auto shrouds = chunk.Get<ObjectShroud>();
		const auto footprints = chunk.Get<PartitionFootprint>();
		const auto transforms = chunk.Get<Transform>();
		const auto offMap = chunk.Get<OffMap>();
		// Working space for a footprint too large for the set on the stack (none is, usually: never allocated).
		std::vector<std::uint32_t> seen;
		const std::uint64_t players = map.Players() >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << map.Players()) - 1;
		for (std::size_t row = 0; row < shrouds.size(); ++row)
		{
			ObjectShroud &shroud = shrouds[row];
			if (footprints[row].alwaysVisible != 0 || !offMap.empty() || map.Players() == 0)
			{
				shroud = ObjectShroud{};
				continue;
			}
			// ShroudLevelOf for every player at once, from the cells' player masks: clear where every cell is clear,
			// partly clear where some are, fogged where none is clear but some are fogged, else shrouded (none: all).
			std::uint64_t allClear = ~std::uint64_t{0}, anyClear = 0, anyFogged = 0;
			bool anyCell = false;
			VisitFootprintCells(map, footprints[row], transforms[row].position.XY(), transforms[row].facing,
				[&](std::int32_t x, std::int32_t y) {
					const std::uint64_t clear = map.ClearMask(x, y);
					allClear &= clear;
					anyClear |= clear;
					anyFogged |= map.FogMask(x, y);
					anyCell = true;
				},
				seen);
			allClear = anyCell ? allClear : 0;
			shroud = {allClear & players, anyClear & ~allClear & players, ~anyClear & anyFogged & players};
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::ObjectShroudSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.object_shroud";
	// Its rows are independent: large chunks are shared out in pieces of 32 rows.
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	// The game orders it before the spatial index it feeds.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
