export module games.generalszh.presentation.objects.algorithms.bridge_radar;
import std;

export import engine.ecs.core.world;
export import games.generalszh.gameplay.bridges.components.bridge;
export import games.generalszh.content.terrain.bridge_content;
import engine.ecs.query.query;
import engine.gameplay.common.spatial.resources.deck_surfaces;
import Engine.Core.Math.FixedPresentation;

// The radar's view of the bridges (W3DRadar::buildTerrainTexture): the bridge at a point (TerrainLogic::findBridgeAt:
// the first whose deck covers it), when its object's body is not RUBBLE (workingBridge), shows its Roads.ini RadarColor
// (white when its template is not found) at its deck's height, the average of its four corners' z.
export namespace generalszh::presentation
{
struct RadarBridge
{
	std::array<float, 3> color{1.0f, 1.0f, 1.0f};
	float height{0.0f};
};

inline std::optional<RadarBridge> WorkingBridgeAt(ecs::World &world, const content::BridgeCatalog &catalog, float x, float y)
{
	const Engine::Math::FixedVector2 at{Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>(std::llround(static_cast<double>(x) * 65536.0))),
		Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>(std::llround(static_cast<double>(y) * 65536.0)))};
	std::optional<gameplay::Bridge> found;
	ecs::Query<ecs::Read<gameplay::Bridge>> query(world);
	query.ForEachChunk([&](auto chunk) {
		for (const gameplay::Bridge &bridge : chunk.template Get<gameplay::Bridge>())
		{
			if (found)
				return;
			const engine::gameplay::DeckGeometry deck{bridge.from, bridge.to, bridge.fromLeft, bridge.fromRight, bridge.toLeft, bridge.toRight};
			if (engine::gameplay::PointOnDeck(deck, at))
				found = bridge;
		}
	});
	if (!found || found->bodyState == gameplay::body_state::Rubble)
		return std::nullopt;
	RadarBridge shown;
	if (found->bridgeTemplate < catalog.size())
	{
		const auto &color = std::next(catalog.begin(), found->bridgeTemplate)->second.radarColor;
		shown.color = {static_cast<float>(color[0]) / 255.0f, static_cast<float>(color[1]) / 255.0f, static_cast<float>(color[2]) / 255.0f};
	}
	shown.height = (Engine::Math::ToFloat(found->fromLeft.z) + Engine::Math::ToFloat(found->fromRight.z) + Engine::Math::ToFloat(found->toLeft.z) +
					   Engine::Math::ToFloat(found->toRight.z)) /
		4.0f;
	return shown;
}
}
