export module engine.gameplay.rts.blocking.systems.unit_cell_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.blocking.components.blocking_unit;
export import engine.gameplay.rts.navigation.resources.unit_cells;
export import engine.gameplay.rts.navigation.resources.navigation_grid;
export import engine.gameplay.rts.navigation.components.navigation;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.movement.algorithms.steering;
export import engine.gameplay.rts.collision.components.collider;
export import engine.gameplay.rts.collision.components.squishable;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.components.carried;
export import engine.gameplay.common.spatial.components.surface_layer;
export import engine.gameplay.common.identity.components.object_id;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.identity.resources.relationships;

// Where the ground units stand, for the pathfinder (Pathfinder::updatePos for every unit whose AI moves it on the ground,
// AIPathfind.cpp): before the tick's simulation (after physics has set the bodies down), from where the last tick left them, every such unit not dying, carried or off
// the map marks the cells its footprint covers about its cell (FootprintAt), the lower ObjectID holding a cell two share.
// Gathered in chunk order, then marked newest first (the original's update order), so the result is the same on any number
// of workers. (Units on a bridge deck are not marked yet: the decks keep no unit cells.)
export namespace engine::gameplay
{
struct UnitCellGather
{
	struct Entry
	{
		std::uint32_t id{0};
		UnitFootprint footprint{};
		UnitOccupant occupant;
	};
	std::vector<Entry> entries;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::UnitCellGather>
{
	static constexpr std::string_view StableName = "engine.gameplay.unit_cell_gather";
};

}

export namespace engine::gameplay
{
struct UnitCellSystem
{
	using Query = ecs::Query<ecs::Read<BlockingUnit>, ecs::Read<Transform>, ecs::Read<NavigationAgent>, ecs::Read<Locomotion>, ecs::Optional<ObjectId>,
		ecs::Optional<Owner>, ecs::Optional<TeamMember>, ecs::Optional<Collider>, ecs::Optional<Squishable>, ecs::Optional<Disabled>, ecs::Optional<SurfaceLayer>,
		ecs::Exclude<OffMap>, ecs::Exclude<Carried>, ecs::Exclude<Dying>>;
	using Resources = ecs::Resources<ecs::Write<UnitCells>, ecs::Write<UnitCellGather>, ecs::Read<NavigationGrid>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		UnitCells &cells = context.Write<UnitCells>();
		auto &entries = context.Write<UnitCellGather>().entries;
		const NavigationGrid &grid = context.Read<NavigationGrid>();
		cells.Fit(grid.Width(), grid.Height());
		cells.Clear();
		entries.clear();
		query.ForEachChunk([&](auto chunk) {
			const auto units = chunk.template Get<BlockingUnit>();
			const auto transforms = chunk.template Get<Transform>();
			const auto agents = chunk.template Get<NavigationAgent>();
			const auto motions = chunk.template Get<Locomotion>();
			const auto ids = chunk.template Get<ObjectId>();
			const auto owners = chunk.template Get<Owner>();
			const auto members = chunk.template Get<TeamMember>();
			const auto colliders = chunk.template Get<Collider>();
			const bool squishable = !chunk.template Get<Squishable>().empty();
			const auto disabledRows = chunk.template Get<Disabled>();
			const auto layers = chunk.template Get<SurfaceLayer>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < units.size(); ++row)
			{
				// isDoingGroundMovement: a ground locomotor, not held; on the ground's layer.
				const bool held = !disabledRows.empty() && (disabledRows[row].mask & disabled_type::Held) != 0;
				if (!IsGroundLocomotor(motions[row].locomotor) || held || (!layers.empty() && layers[row].layer != GroundLayer))
					continue;
				UnitCellGather::Entry entry;
				entry.id = ids.empty() ? 0u : ids[row].value;
				const auto &at = transforms[row].position;
				entry.footprint = FootprintAt(at.x.Raw(), at.y.Raw(), agents[row].radius, agents[row].centered != 0);
				UnitOccupant &occupant = entry.occupant;
				occupant.entity = entities[row];
				occupant.player = owners.empty() ? 0u : owners[row].player;
				occupant.team = members.empty() ? Relationships::NoTeam : members[row].team;
				occupant.crushableLevel = colliders.empty() ? 255u : colliders[row].crushableLevel;
				const BlockingUnit &unit = units[row];
				occupant.flags = static_cast<std::uint8_t>((unit.Is(blocking_kind::Infantry) ? unit_cell_flag::Infantry : 0u) |
					(squishable ? unit_cell_flag::Squishable : 0u) | (unit.Is(blocking_kind::Dozer) ? unit_cell_flag::Dozer : 0u) | (unit.Is(blocking_kind::Harvester) ? unit_cell_flag::Harvester : 0u));
				entries.push_back(entry);
			}
		});
		// Newest first, so the oldest marks last and holds what they share.
		std::sort(entries.begin(), entries.end(), [](const UnitCellGather::Entry &a, const UnitCellGather::Entry &b) {
			return a.id != b.id ? a.id > b.id : a.occupant.entity.index > b.occupant.entity.index;
		});
		cells.occupants.reserve(entries.size());
		for (const UnitCellGather::Entry &entry : entries)
		{
			const auto index = static_cast<std::uint32_t>(cells.occupants.size());
			cells.occupants.push_back(entry.occupant);
			for (std::int32_t y = entry.footprint.y0; y <= entry.footprint.y1; ++y)
				for (std::int32_t x = entry.footprint.x0; x <= entry.footprint.x1; ++x)
					cells.Mark(x, y, index);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::UnitCellSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.unit_cells";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	// Before the tick's simulation; the composition orders it after physics.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
