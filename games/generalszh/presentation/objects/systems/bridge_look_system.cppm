export module games.generalszh.presentation.objects.systems.bridge_look_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.bridges.components.bridge;
export import games.generalszh.presentation.objects.components.bridge_look;
export import games.generalszh.presentation.objects.resources.bridge_art;
export import games.generalszh.presentation.objects.algorithms.bridge_geometry;
import Engine.Core.Math.FixedPresentation;

// W3DBridgeBuffer::drawBridges each frame: every bridge the terrain logic holds is drawn (landmark bridges are objects
// drawn by their own models, not by the bridge buffer); one whose recorded damage state (the terrain logic's
// curDamageState) differs from the one it shows takes the new state and loads its model, or puts the model of the
// state it showed back when the new one cannot load (BridgeModelAfterChange). The bridges as drawn go to BridgeViews,
// its version moved on when anything drawn changed (the original then reloads its vertex and index buffers).
export namespace generalszh::presentation
{
struct BridgeLookSystem
{
	using Query = ecs::Query<ecs::Read<gameplay::Bridge>>;
	using SideTables = ecs::SideTables<ecs::Write<BridgeLook>>;
	using Resources = ecs::Resources<ecs::Read<BridgeArt>, ecs::Write<BridgeViews>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const BridgeArt &art = context.Read<BridgeArt>();
		BridgeViews &views = context.Write<BridgeViews>();
		auto &looks = context.Side<SideTables, BridgeLook>();
		views.next.clear();
		query.ForEachChunk([&](auto chunk) {
			const auto bridges = chunk.template Get<gameplay::Bridge>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < bridges.size(); ++row)
			{
				const gameplay::Bridge &bridge = bridges[row];
				if (bridge.landmark != 0)
					continue;
				const BridgeKind *kind = art.Kind(bridge.bridgeTemplate);
				if (kind == nullptr)
					continue;
				BridgeLook *look = looks.Get(entities[row]);
				BridgeLook now = look != nullptr ? *look : BridgeLook{};
				if (now.shown != bridge.curDamageState)
				{
					now.model = BridgeModelAfterChange(*kind, now.shown, bridge.curDamageState);
					now.shown = bridge.curDamageState;
				}
				if (look != nullptr)
					*look = now;
				else
					context.Commands().Add<BridgeLook>(entities[row], now);
				BridgeView view;
				view.bridgeTemplate = bridge.bridgeTemplate;
				view.model = now.model;
				view.from = {Engine::Math::ToFloat(bridge.from.x), Engine::Math::ToFloat(bridge.from.y), Engine::Math::ToFloat(bridge.from.z)};
				view.to = {Engine::Math::ToFloat(bridge.to.x), Engine::Math::ToFloat(bridge.to.y), Engine::Math::ToFloat(bridge.to.z)};
				view.scale = kind->scale;
				views.next.push_back(view);
			}
		});
		if (views.next != views.bridges)
		{
			std::swap(views.next, views.bridges);
			++views.version;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::BridgeLookSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.bridge_look";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
