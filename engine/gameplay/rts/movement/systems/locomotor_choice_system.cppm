export module engine.gameplay.rts.movement.systems.locomotor_choice_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.movement.components.locomotor_choice;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.surface_layer;
export import engine.gameplay.rts.navigation.resources.navigation_grid;

// AIUpdateInterface::doLocomotor's chooseGoodLocomotorFromCurrentSet, each tick before the unit moves (EA AIPathfind.cpp
// Pathfinder::chooseBestLocomotorForPosition, validLocomotorSurfacesForCellType): the first locomotor of its set legal on
// the type of the cell it stands in on its layer (a cell off the map or off its deck counting as clear); none legal there
// keeps the one it has (it slid somewhere its set cannot go). A change turns off its precise height and ultra accuracy.
// Chunk-parallel.
export namespace engine::gameplay
{
// validLocomotorSurfacesForCellType.
inline std::uint8_t SurfacesForCell(PathfindCellType type) noexcept
{
	switch (type)
	{
	case PathfindCellType::Clear: return locomotor_surface::Ground | locomotor_surface::Air;
	case PathfindCellType::Water: return locomotor_surface::Water | locomotor_surface::Air;
	case PathfindCellType::Rubble: return locomotor_surface::Rubble | locomotor_surface::Air;
	case PathfindCellType::Cliff: return locomotor_surface::Cliff | locomotor_surface::Air;
	case PathfindCellType::Obstacle:
	case PathfindCellType::Impassable:
	case PathfindCellType::BridgeImpassable: return locomotor_surface::Air;
	}
	return 0;
}

// Pathfinder::getCell(layer, x, y)'s type: the ground's, or the deck of a bridge layer; no cell there: clear.
inline PathfindCellType CellTypeAt(const NavigationGrid &grid, std::uint8_t layer, Engine::Math::FixedVector2 at) noexcept
{
	const std::int32_t x = static_cast<std::int32_t>((at.x / Engine::Math::Fixed::FromInt(PathfindCellSize)).Floor());
	const std::int32_t y = static_cast<std::int32_t>((at.y / Engine::Math::Fixed::FromInt(PathfindCellSize)).Floor());
	if (layer == 0)
		return grid.Contains(x, y) ? grid.Type(x, y) : PathfindCellType::Clear;
	const auto &decks = grid.Decks();
	if (layer > decks.size() || !decks[layer - 1].Contains(x, y))
		return PathfindCellType::Clear;
	const DeckLayer &deck = decks[layer - 1];
	return deck.type[deck.Index(x, y)];
}

struct LocomotorChoiceSystem
{
	using Query = ecs::Query<ecs::Write<Locomotion>, ecs::Write<LocomotorChoice>, ecs::Read<Transform>, ecs::Optional<SurfaceLayer>>;
	using Resources = ecs::Resources<ecs::Read<NavigationGrid>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const NavigationGrid &grid = context.Read<NavigationGrid>();
		auto motions = chunk.Get<Locomotion>();
		auto choices = chunk.Get<LocomotorChoice>();
		const auto transforms = chunk.Get<Transform>();
		const auto layers = chunk.Get<SurfaceLayer>();
		for (std::size_t row = 0; row < motions.size(); ++row)
		{
			LocomotorChoice &choice = choices[row];
			const std::uint8_t legal = SurfacesForCell(CellTypeAt(grid, layers.empty() ? std::uint8_t{0} : layers[row].layer, transforms[row].position.XY()));
			std::size_t best = choice.count;
			for (std::size_t index = 0; index < choice.count; ++index)
				if ((choice.options[index].surfaces & legal) != 0)
				{
					best = index;
					break;
				}
			if (best == choice.count || best == choice.current)
				continue;
			choice.current = static_cast<std::uint8_t>(best);
			Locomotion &motion = motions[row];
			motion.locomotor = choice.options[best];
			motion.preciseZ = 0;
			motion.ultraAccurate = 0;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::LocomotorChoiceSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.locomotor_choice";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it before the locomotor's damage rates and movement.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
