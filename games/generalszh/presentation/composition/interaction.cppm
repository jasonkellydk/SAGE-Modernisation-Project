export module games.generalszh.presentation.composition.interaction;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
export import games.generalszh.session.session_view;
export import games.generalszh.content.global.in_game_ui;
export import games.generalszh.presentation.interaction.resources.academy_client_records;
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
import games.generalszh.presentation.objects.systems.waypoint_path_system;
import games.generalszh.presentation.objects.systems.radius_cursor_system;
import games.generalszh.presentation.models.pointer_pick_system;
import games.generalszh.presentation.interaction.algorithms.mouseover_names;
import games.generalszh.presentation.interaction.algorithms.hotkey_teams;
import games.generalszh.presentation.interaction.systems.mouse_tooltip_system;
import games.generalszh.presentation.interaction.resources.unit_voice_cues;
import games.generalszh.presentation.audio.systems.unit_voice_system;
import games.generalszh.presentation.audio.systems.audio_systems;
import games.generalszh.presentation.objects.systems.effect_attachment_systems;
import games.generalszh.presentation.objects.systems.vehicle_motion_systems;

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
	world.EmplaceResource<AcademyClientRecords>();
	auto &settings = world.EmplaceResource<MouseSettings>();
	settings.dragTolerance = static_cast<float>(mouse.dragTolerance);
	settings.dragTolerance3D = static_cast<float>(mouse.dragTolerance3D);
	settings.dragToleranceMs = mouse.dragToleranceMs;
	settings.maxSelectionSize = inGameUi.maxSelectionSize;
	for (std::size_t kind = 0; kind < settings.cursorDirections.size(); ++kind)
		settings.cursorDirections[kind] = mouse.cursors[kind].directions;
	world.EmplaceResource<SelectionBox>();
	world.EmplaceResource<CursorState>();
	world.EmplaceResource<GuiTargeting>();
	world.EmplaceResource<PlayerOrders>();
	// What the selection is told to answer (pickAndPlayUnitVoiceResponse), and MiscAudio's car bomb lines and cheer.
	world.EmplaceResource<UnitVoiceCues>();
	auto &voices = world.EmplaceResource<UnitVoiceSounds>();
	const auto &misc = game.Content().miscAudio;
	const auto miscSound = [&](std::string_view field) {
		const auto found = misc.find(field);
		return found != misc.end() ? found->second : std::string{};
	};
	voices.carBombAttack = miscSound("TerroristInCarAttackVoice");
	voices.carBombMove = miscSound("TerroristInCarMoveVoice");
	voices.carBombSelect = miscSound("TerroristInCarSelectVoice");
	voices.allCheer = miscSound("AllCheerSound");
	world.EmplaceResource<HotkeyTaps>();
	world.EmplaceResource<BuildPlacement>();
	world.EmplaceResource<PlacementGhosts>().opacity = Engine::Math::ToFloat(game.Content().gameData.objectPlacementOpacity);
	world.EmplaceResource<MoveHints>();
	world.EmplaceResource<WaypointPaths>();
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

// The mouse's tooltip and cursor text (Mouse.ini's look; each cursor's CursorText fetched, Mouse::setCursor) and what
// the in-game mouse-over names (InGameUI::createMouseoverHint): the objects' DisplayName texts and "ThingTemplate:<name>"
// for those without, OBJECT:Prop, TOOLTIP:SupplyWarehouse, ValuePerSupplyBox, the players' names as shown (by name) and
// colours (Player::getPlayerColor, 0xRRGGBB, by player index), and whether the game is a multiplayer one.
struct MouseTooltipSetup
{
	std::function<std::u16string(std::string_view)> labels; // GameText::fetch (none: no words)
	std::span<const std::pair<std::string, std::u16string>> playerNames;
	std::span<const std::uint32_t> playerColors;
	bool multiplayer{false};
};

inline void EmplaceMouseTooltipResources(ecs::World &world, session::SessionView &game, const content::MouseContent &mouse, const MouseTooltipSetup &setup)
{
	auto &settings = world.EmplaceResource<MouseTooltipSettings>();
	settings.look = mouse.tooltip;
	for (std::size_t kind = 0; kind < MouseCursorKinds; ++kind)
	{
		const content::MouseCursorDefinition &cursor = mouse.cursors[kind];
		if (!cursor.cursorText.empty() && setup.labels)
			settings.cursorTexts[kind] = setup.labels(cursor.cursorText);
		settings.cursorTextColors[kind] = cursor.cursorTextColor;
		settings.cursorTextDropColors[kind] = cursor.cursorTextDropColor;
	}
	world.EmplaceResource<MouseTooltip>();
	auto &names = world.EmplaceResource<MouseoverNames>();
	const content::GameContent &content = game.Content();
	names.valuePerSupplyBox = content.gameData.valuePerSupplyBox;
	names.multiplayer = setup.multiplayer;
	names.playerColors.assign(setup.playerColors.begin(), setup.playerColors.end());
	if (setup.labels)
	{
		names.propText = setup.labels("OBJECT:Prop");
		names.warehouseText = setup.labels("TOOLTIP:SupplyWarehouse");
		for (const auto &[name, object] : content.objects)
		{
			if (!object.displayName.empty() && !names.labelTexts.contains(object.displayName))
				names.labelTexts.emplace(object.displayName, setup.labels(object.displayName));
			if (object.displayName.empty())
				names.templateTexts.emplace(name, setup.labels("ThingTemplate:" + name));
		}
	}
	if (const auto *roster = world.FindResource<engine::gameplay::TeamRoster>())
		for (const auto &[name, shown] : setup.playerNames)
			if (const auto player = roster->FindPlayer(name))
			{
				if (names.playerNames.size() <= *player)
					names.playerNames.resize(*player + 1);
				names.playerNames[*player] = shown;
			}
	KnowMouseoverNames(names, game);
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
	// The selection's paths in waypoint mode, after the frame's clicks.
	static WaypointPathSystem waypointPaths;
	registry.Register(waypointPaths);
	registry.OrderBefore<PointerInteractionSystem, WaypointPathSystem>();
	registry.OrderBefore<BuildPlacementSystem, WaypointPathSystem>();
	registry.Register(radiusCursor);
	registry.Register(pointerPick);
	// The mouse's tooltip and cursor text after the frame's pick and interaction.
	static MouseTooltipSystem mouseTooltip;
	registry.Register(mouseTooltip);
	registry.OrderBefore<PointerPickSystem, MouseTooltipSystem>();
	registry.OrderBefore<PointerInteractionSystem, MouseTooltipSystem>();
	registry.OrderBefore<MouseTooltipSystem, WaypointPathSystem>(); // a listening outpost's line: the frame's moused-over enemy
	// The selection's answers to the frame's orders, after them and the frame's other sounds, before the mix.
	static UnitVoiceSystem unitVoices;
	registry.Register(unitVoices);
	registry.OrderBefore<PointerInteractionSystem, UnitVoiceSystem>();
	registry.OrderBefore<BuildPlacementSystem, UnitVoiceSystem>();
	registry.OrderBefore<FxPlaybackSystem, UnitVoiceSystem>();
	registry.OrderBefore<MotionEmitterSystem, UnitVoiceSystem>();
	registry.OrderBefore<UnitVoiceSystem, AudioMixSystem>();
}
}
