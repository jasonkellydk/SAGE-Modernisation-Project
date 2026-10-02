export module games.generalszh.presentation.objects.algorithms.scenery_setup;
import std;

export import engine.level.model.level;
export import games.generalszh.session.session_view;
export import games.generalszh.gameplay.world.resources.map_scenery;
export import games.generalszh.presentation.objects.resources.scenery;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import engine.gameplay.common.spatial.resources.ground_height;
import games.generalszh.presentation.objects.algorithms.tree_breeze_sway;
import games.generalszh.presentation.objects.algorithms.look_setup;
import Engine.Core.Math.FixedPresentation;

// The client's scenery as the map's objects make it (GameLogic::startNewGame, or loadingSaveGame's pass): each placed
// object whose role is a tree or a prop (MapObjectRoleOf, by the match's rules) where it was put, raised by the ground
// there, its turn normalized and its template's scale (Drawable: m_instanceScale = getAssetScale; the tree buffer adds
// no randomness: W3DTreeDraw passes 0). Each tree takes one of the sway types at random (addTree) and, when it topples
// or leans out over more than 2 frames, is filed by place. Road and bridge points, scorch marks and waypoints are not
// objects; nor is a type the game does not know. The scenery's definitions are catalogued as it is made.
export namespace generalszh::presentation
{
inline void BuildScenery(Scenery &scenery, std::span<const engine::level::Placement> placements, session::SessionView &view, LookCatalog &catalog,
	const gameplay::MapSceneryRules &rules, const engine::gameplay::GroundHeight &ground, std::array<float, 2> mapSize)
{
	scenery = Scenery{};
	// The scenery's definitions are known to the game (DefinitionIndex makes them) and their looks catalogued first.
	for (const engine::level::Placement &placement : placements)
		(void)view.DefinitionIndex(placement.type);
	KnowLooks(catalog, view);
	for (const engine::level::Placement &placement : placements)
	{
		if ((placement.flags & 0x36u) != 0 || placement.properties.Contains("scorchType") || placement.properties.Contains("waypointID"))
			continue;
		const auto index = view.DefinitionIndex(placement.type);
		if (!index)
			continue;
		const gameplay::MapObjectRole role = gameplay::MapObjectRoleOf(view.Definition(*index), rules);
		if (role != gameplay::MapObjectRole::Tree && role != gameplay::MapObjectRole::Prop)
			continue;
		const DefinitionLooks *looks = catalog.Of(*index);
		const float ground_z = Engine::Math::ToFloat(ground.At(placement.position.XY()));
		scenery.definition.push_back(*index);
		scenery.kind.push_back(role == gameplay::MapObjectRole::Tree ? Scenery::Tree : Scenery::Prop);
		scenery.place.push_back(placement.position.XY());
		scenery.at.push_back({Engine::Math::ToFloat(placement.position.x), Engine::Math::ToFloat(placement.position.y),
			Engine::Math::ToFloat(placement.position.z) + ground_z});
		// normalizeAngle: into (-pi, pi].
		float angle = static_cast<float>(static_cast<std::int32_t>(placement.orientation.units)) * (2.0f * std::numbers::pi_v<float> / 4294967296.0f);
		scenery.angle.push_back(angle);
		scenery.scale.push_back(looks != nullptr ? looks->scale : 1.0f);
		scenery.swayType.push_back(static_cast<std::uint8_t>(TreeSwayType(tree_breeze_sway_detail::Unit(0x5CE4E000ull + scenery.definition.size()))));
		scenery.removed.push_back(0);
		scenery.bends.push_back({});
	}
	// The bending trees by cell.
	scenery.columns = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::ceil(std::max(mapSize[0], 1.0f) / Scenery::CellSize)));
	scenery.rows = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::ceil(std::max(mapSize[1], 1.0f) / Scenery::CellSize)));
	std::vector<std::uint32_t> cellOf(scenery.Size(), ~0u);
	scenery.cellStart.assign(static_cast<std::size_t>(scenery.columns) * scenery.rows + 1, 0);
	for (std::uint32_t item = 0; item < scenery.Size(); ++item)
	{
		if (scenery.kind[item] != Scenery::Tree)
			continue;
		const DefinitionLooks *looks = catalog.Of(scenery.definition[item]);
		if (looks == nullptr || !looks->bufferTree || !(looks->treeMotion.doTopple || looks->treeMotion.framesToMoveOutward > 2.0f))
			continue;
		cellOf[item] = scenery.Row(scenery.at[item][1]) * scenery.columns + scenery.Column(scenery.at[item][0]);
		++scenery.cellStart[cellOf[item] + 1];
	}
	for (std::size_t cell = 1; cell < scenery.cellStart.size(); ++cell)
		scenery.cellStart[cell] += scenery.cellStart[cell - 1];
	scenery.cellItems.resize(scenery.cellStart.back());
	std::vector<std::uint32_t> fill(scenery.cellStart.begin(), scenery.cellStart.end() - 1);
	for (std::uint32_t item = 0; item < scenery.Size(); ++item)
		if (cellOf[item] != ~0u)
			scenery.cellItems[fill[cellOf[item]]++] = item;
}
}
