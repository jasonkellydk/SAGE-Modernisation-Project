export module games.generalszh.presentation.composition.interaction;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
export import games.generalszh.session.session_view;
export import games.generalszh.content.global.in_game_ui;
export import games.generalszh.content.global.mouse;
import Engine.Core.Math.FixedPresentation;
import games.generalszh.presentation.interaction.resources.interaction_resources;
import games.generalszh.presentation.interaction.resources.build_placement;
import games.generalszh.presentation.interaction.algorithms.selection_setup;
import games.generalszh.presentation.interaction.systems.interaction_systems;
import games.generalszh.presentation.interaction.systems.build_placement_system;
import games.generalszh.presentation.objects.resources.placement_ghosts;
import games.generalszh.presentation.objects.resources.radius_cursor;
import games.generalszh.presentation.objects.systems.placement_ghost_system;
import games.generalszh.presentation.objects.systems.rally_point_marker_system;
import games.generalszh.presentation.objects.systems.move_hint_system;
import games.generalszh.presentation.objects.systems.radius_cursor_system;
import games.generalszh.presentation.models.pointer_pick_system;

// The player's interaction (pointer, view, selection, orders, building placement and its ghosts, the radius cursor):
// the presentation's composition of which resources it keeps in the simulation's world for the local seat's player,
// configured from Mouse.ini and InGameUI.ini, and the systems that run each frame.
export namespace generalszh::presentation::composition
{
inline void EmplaceInteractionResources(ecs::World &world, session::SessionView &game, const content::MouseContent &mouse,
	const content::InGameUiContent &inGameUi)
{
	world.EmplaceResource<PointerInput>();
	world.EmplaceResource<InteractionView>();
	world.EmplaceResource<PointerHits>();
	world.EmplaceResource<InteractionState>();
	auto &settings = world.EmplaceResource<MouseSettings>();
	settings.dragTolerance = static_cast<float>(mouse.dragTolerance);
	settings.dragTolerance3D = static_cast<float>(mouse.dragTolerance3D);
	settings.dragToleranceMs = mouse.dragToleranceMs;
	for (std::size_t kind = 0; kind < settings.cursorDirections.size(); ++kind)
		settings.cursorDirections[kind] = mouse.cursors[kind].directions;
	world.EmplaceResource<SelectionBox>();
	world.EmplaceResource<CursorState>();
	world.EmplaceResource<GuiTargeting>();
	world.EmplaceResource<PlayerOrders>();
	world.EmplaceResource<BuildPlacement>();
	world.EmplaceResource<PlacementGhosts>().opacity = Engine::Math::ToFloat(game.Content().gameData.objectPlacementOpacity);
	world.EmplaceResource<MoveHints>();
	auto &rally = world.EmplaceResource<RallyPointMarkers>();
	rally.definition = game.DefinitionIndex("RallyPointMarker").value_or(RallyPointMarkers::NoDefinition);
	rally.downwindAngle = Engine::Math::ToFloat(game.Content().gameData.downwindAngle);
	world.EmplaceResource<RadiusCursorLooks>().looks = inGameUi.radiusCursors;
	world.EmplaceResource<RadiusCursor>();
	KnowSelectables(world.EmplaceResource<SelectionCatalog>(), game);
	if (const auto seat = game.SeatPlayer(game.LocalSeat()))
		world.EmplaceResource<LocalPlayer>(LocalPlayer{*seat, true});
	else
		world.EmplaceResource<LocalPlayer>();
}

// The interaction's frame systems (stateless: one shared instance each): the pointer's pick and what it does, the
// building placement and its ghosts, the radius cursor.
inline void RegisterInteractionFrameSystems(ecs::SystemRegistry &registry)
{
	static PointerInteractionSystem pointerInteraction;
	static BuildPlacementSystem buildPlacement;
	static PlacementGhostSystem placementGhosts;
	static RallyPointMarkerSystem rallyPointMarker;
	static MoveHintSystem moveHints;
	static RadiusCursorSystem radiusCursor;
	static PointerPickSystem pointerPick;
	registry.Register(pointerInteraction);
	registry.Register(buildPlacement);
	registry.Register(placementGhosts);
	registry.Register(rallyPointMarker);
	registry.Register(moveHints);
	registry.OrderBefore<PointerInteractionSystem, MoveHintSystem>(); // the frame's orders first
	registry.Register(radiusCursor);
	registry.Register(pointerPick);
}
}
