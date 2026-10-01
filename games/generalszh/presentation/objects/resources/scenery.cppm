export module games.generalszh.presentation.objects.resources.scenery;
import std;

export import games.generalszh.presentation.objects.components.tree_bend;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// The map's scenery the client alone keeps (no logic object: MapObjectRoleOf): the trees of its tree buffer
// (W3DTreeBuffer's TTree: where each stands, its turn and scale, its sway type, its topple and push-aside state, cleared
// for construction) and the props of its prop buffer (W3DPropBuffer). The trees that topple or lean out over more
// than 2 frames are filed by place for the units passing them (addTree's area partition; here a grid of square cells),
// and how many of the logic's clearings for construction have been applied so far.
export namespace generalszh::presentation
{
struct Scenery
{
	static constexpr std::uint8_t Tree = 0;
	static constexpr std::uint8_t Prop = 1;
	static constexpr float CellSize = 50.0f;

	// Per item.
	std::vector<std::uint32_t> definition;
	std::vector<std::uint8_t> kind;
	std::vector<std::array<float, 3>> at;
	std::vector<Engine::Math::FixedVector2> place; // where the map put it, exactly (the logic's footprints test it)
	std::vector<float> angle; // radians
	std::vector<float> scale;
	std::vector<std::uint8_t> swayType;
	std::vector<std::uint8_t> removed; // cleared for construction (DELETED_TREE_TYPE; a prop's m_robj gone)
	std::vector<TreeBend> bends;
	// The bending trees by cell (each cell's run of item indices, in the order added).
	std::uint32_t columns{0};
	std::uint32_t rows{0};
	std::vector<std::uint32_t> cellStart; // columns * rows + 1
	std::vector<std::uint32_t> cellItems;
	std::size_t clearingsApplied{0};

	std::size_t Size() const noexcept { return definition.size(); }

	// The cell of a point, clamped to the grid.
	std::uint32_t Column(float x) const noexcept
	{
		return static_cast<std::uint32_t>(std::clamp(static_cast<std::int64_t>(std::floor(x / CellSize)), std::int64_t{0}, std::int64_t{columns} - 1));
	}
	std::uint32_t Row(float y) const noexcept
	{
		return static_cast<std::uint32_t>(std::clamp(static_cast<std::int64_t>(std::floor(y / CellSize)), std::int64_t{0}, std::int64_t{rows} - 1));
	}

	// Each bending tree within the square of `reach` about (x, y).
	template<class Visit>
	void ForEachBendingNear(float x, float y, float reach, Visit &&visit) const
	{
		if (columns == 0 || rows == 0)
			return;
		const std::uint32_t left = Column(x - reach), right = Column(x + reach), bottom = Row(y - reach), top = Row(y + reach);
		for (std::uint32_t row = bottom; row <= top; ++row)
			for (std::uint32_t column = left; column <= right; ++column)
			{
				const std::uint32_t cell = row * columns + column;
				for (std::uint32_t item = cellStart[cell]; item < cellStart[cell + 1]; ++item)
					visit(cellItems[item]);
			}
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::Scenery>
{
	static constexpr std::string_view StableName = "generalszh.presentation.scenery";
};
}
