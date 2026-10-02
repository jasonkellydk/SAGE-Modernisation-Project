export module engine.gameplay.rts.navigation.resources.goal_cells;
import std;

export import engine.ecs.core.entity;
export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// Whose goal each ground pathfinding cell is (PathfindCell's m_goalUnitID, set by Pathfinder::updateGoal and cleared by
// removeGoal): a mover about to go somewhere claims the cells its footprint will cover there, so the next mover's
// destination is adjusted off them (adjustDestination: allies never share a goal). One entity per cell, in the grid's
// index order (y * width + x); none: nobody's goal. Simulation state: checkpointed (a later claim overwrites an earlier
// one on the cells they share, and a removal clears only its own, so the owners are not rebuilt from the movers).
export namespace engine::gameplay
{
struct GoalCells
{
	std::int32_t width{0}, height{0};
	std::vector<ecs::Entity> owner;

	// Sized to the grid (cleared when the size changes).
	void Fit(std::int32_t gridWidth, std::int32_t gridHeight)
	{
		if (gridWidth == width && gridHeight == height && owner.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
			return;
		width = std::max(gridWidth, 0);
		height = std::max(gridHeight, 0);
		owner.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), ecs::Entity{});
	}

	bool Contains(std::int32_t x, std::int32_t y) const noexcept { return x >= 0 && y >= 0 && x < width && y < height; }
	std::size_t Index(std::int32_t x, std::int32_t y) const noexcept
	{
		return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
	}
	ecs::Entity At(std::int32_t x, std::int32_t y) const noexcept { return Contains(x, y) ? owner[Index(x, y)] : ecs::Entity{}; }
	void Set(std::int32_t x, std::int32_t y, ecs::Entity entity) noexcept
	{
		if (Contains(x, y))
			owner[Index(x, y)] = entity;
	}

	// The claimed cells only, in index order.
	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(width));
		writer.U32(static_cast<std::uint32_t>(height));
		std::uint32_t claimed = 0;
		for (const ecs::Entity entity : owner)
			claimed += entity != ecs::Entity{} ? 1u : 0u;
		writer.U32(claimed);
		for (std::size_t index = 0; index < owner.size(); ++index)
			if (owner[index] != ecs::Entity{})
			{
				writer.U32(static_cast<std::uint32_t>(index));
				writer.U32(owner[index].index);
				writer.U32(owner[index].generation);
			}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto savedWidth = reader.U32();
		const auto savedHeight = reader.U32();
		const auto claimed = reader.U32();
		if (!savedWidth || !savedHeight || !claimed)
			return false;
		width = height = 0;
		owner.clear();
		Fit(static_cast<std::int32_t>(*savedWidth), static_cast<std::int32_t>(*savedHeight));
		for (std::uint32_t count = 0; count < *claimed; ++count)
		{
			const auto index = reader.U32();
			const auto entityIndex = reader.U32();
			const auto generation = reader.U32();
			if (!index || !entityIndex || !generation || *index >= owner.size())
				return false;
			owner[*index].index = *entityIndex;
			owner[*index].generation = *generation;
		}
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::GoalCells>
{
	static constexpr std::string_view StableName = "engine.gameplay.goal_cells";
};
}
