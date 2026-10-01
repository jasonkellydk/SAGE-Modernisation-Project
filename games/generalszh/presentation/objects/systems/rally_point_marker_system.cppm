export module games.generalszh.presentation.objects.systems.rally_point_marker_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.objects.resources.rally_point_markers;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.interaction.components.selected;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.rts.production.components.rally_point;
import Engine.Core.Math.FixedPresentation;

// ControlBar::evaluateContextUI / populateCommand / populateUnderConstruction / populateOCLTimer -> showRallyPoint: with
// one thing selected that the viewed player owns and that has a rally point set (its production exit's), the
// RallyPointMarker flag stands there on the ground, turned to the wind (DownwindAngle), in the viewed player's colour,
// its LOOP animation running from when it went up; anything else selected, or nothing, takes it down.
export namespace generalszh::presentation
{
struct RallyPointMarkerSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::RallyPoint>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::RallyPoint>>;
	using SideTables = ecs::SideTables<ecs::Read<Selected>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<LookCatalog>, ecs::Read<TerrainHeightHandle>, ecs::Write<RallyPointMarkers>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		RallyPointMarkers &markers = context.Write<RallyPointMarkers>();
		markers.instances.clear();
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const auto selected = context.SideRead<SideTables, Selected>().Entities();
		const auto lookup = context.Lookup<Lookup>();
		const engine::gameplay::RallyPoint *rally = nullptr;
		if (selected.size() == 1 && lookup.IsAlive(selected.front()))
			if (const auto *owner = lookup.Get<engine::gameplay::Owner>(selected.front()); owner != nullptr && owner->player == frame.viewer)
				rally = lookup.Get<engine::gameplay::RallyPoint>(selected.front());
		const DefinitionLooks *looks = markers.definition != RallyPointMarkers::NoDefinition ? catalog.Of(markers.definition) : nullptr;
		if (rally == nullptr || looks == nullptr || looks->stateLooks.empty())
		{
			markers.shown = false;
			return;
		}
		if (!markers.shown)
		{
			markers.shown = true;
			markers.since = frame.clock;
		}
		const float x = Engine::Math::ToFloat(rally->at.x), y = Engine::Math::ToFloat(rally->at.y);
		const auto &ground = context.Read<TerrainHeightHandle>().at;
		const float z = ground ? ground(x, y) : 0.0f;
		const std::size_t state = looks->states.Empty() ? 0 : content::SelectModelState(looks->states, {});
		const float c = std::cos(markers.downwindAngle) * looks->scale, s = std::sin(markers.downwindAngle) * looks->scale;
		ObjectInstance marker;
		marker.look = looks->stateLooks[std::min(state, looks->stateLooks.size() - 1)];
		marker.clipLook = marker.look;
		marker.world = {c, -s, 0, x, s, c, 0, y, 0, 0, looks->scale, z, 0, 0, 0, 1};
		marker.teamColor = catalog.ColorOf(frame.viewer);
		marker.animationSeconds = static_cast<float>(frame.clock - markers.since);
		markers.instances.push_back(marker);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::RallyPointMarkerSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.rally_point_marker";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
