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
		std::vector<std::array<std::int32_t, 2>> cells;
		for (std::size_t row = 0; row < shrouds.size(); ++row)
		{
			ObjectShroud &shroud = shrouds[row];
			if (footprints[row].alwaysVisible != 0 || !offMap.empty() || map.Players() == 0)
			{
				shroud = ObjectShroud{};
				continue;
			}
			FootprintCells(map, footprints[row], transforms[row].position.XY(), transforms[row].facing, cells);
			shroud = {0, 0, 0};
			for (std::uint32_t player = 0; player < map.Players() && player < 64; ++player)
			{
				const std::uint64_t bit = std::uint64_t{1} << player;
				switch (ShroudLevelOf(map, player, cells))
				{
				case ObjectShroudLevel::Clear: shroud.clear |= bit; break;
				case ObjectShroudLevel::PartialClear: shroud.partial |= bit; break;
				case ObjectShroudLevel::Fogged: shroud.fogged |= bit; break;
				case ObjectShroudLevel::Shrouded: break;
				}
			}
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
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	// The game orders it before the spatial index it feeds.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
