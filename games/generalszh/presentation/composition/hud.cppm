export module games.generalszh.presentation.composition.hud;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
export import games.generalszh.session.session_view;
export import games.generalszh.content.loading.game_content;
export import games.generalszh.content.eva.eva_content;
export import games.generalszh.content.global.in_game_ui;
export import games.generalszh.content.global.language_fonts;
import engine.gameplay.rts.teams.resources.team_roster;
import games.generalszh.presentation.hud.resources.in_game_overlay;
import games.generalszh.presentation.hud.resources.cameo_flashes;
import games.generalszh.presentation.hud.resources.cinematic_text;
import games.generalszh.presentation.hud.resources.popup_message;
import games.generalszh.presentation.hud.resources.eva_state;
import games.generalszh.presentation.hud.resources.radar_events;
import games.generalszh.presentation.hud.resources.in_game_messages;
import games.generalszh.presentation.hud.resources.named_timers;
import games.generalszh.presentation.hud.resources.screen_fade;
import games.generalszh.presentation.hud.resources.military_caption;
import games.generalszh.presentation.hud.algorithms.screen_fade_steps;
import games.generalszh.presentation.hud.systems.in_game_message_system;
import games.generalszh.presentation.hud.systems.military_caption_system;
import games.generalszh.presentation.hud.systems.screen_fade_system;
import games.generalszh.presentation.hud.systems.eva_system;
import games.generalszh.presentation.hud.systems.production_presentation_system;
import games.generalszh.presentation.hud.systems.overcharge_notice_system;
import games.generalszh.presentation.hud.systems.rally_notice_system;
import games.generalszh.presentation.hud.systems.beacon_system;
import games.generalszh.presentation.hud.systems.radar_event_system;
import games.generalszh.presentation.objects.systems.script_flash_systems;
import games.generalszh.presentation.objects.systems.disable_presentation_systems;
import games.generalszh.presentation.objects.systems.crate_presentation_systems;
import games.generalszh.presentation.objects.systems.ability_presentation_systems;

// The interface's domain of the presentation (InGameUI's messages, the military caption, named timers, fades, the
// radar's events, EVA, cameo flashes, cinematic text, popups): the presentation's composition of which resources it
// keeps in the simulation's world, configured from the game's content, and the systems that run once a tick.
export namespace generalszh::presentation::composition
{
// What the interface is built from: the game's strings, InGameUI.ini, Language.ini, Eva.ini, the players' shown names
// and the game's content (the objects' and upgrades' names, MiscAudio).
struct HudSetup
{
	std::function<std::u16string(std::string_view)> labels; // GameText::fetch (none: the interface shows no words)
	const content::InGameUiContent *inGameUi{nullptr};
	const content::LanguageFonts *language{nullptr};
	const content::EvaCatalog *eva{nullptr};
	std::span<const std::pair<std::string, std::u16string>> playerNames;
	std::u16string defeatedText{u"%ls has been defeated."};
	const content::GameContent *content{nullptr}; // the objects' and upgrades' names, MiscAudio's sounds
};

// Object::getRadarPriority / Radar::isPriorityVisible by definition; the damage that never warns; the words and sounds.
inline void EmplaceRadarFeedback(ecs::World &world, session::SessionView &game, const HudSetup &setup)
{
	auto &radar = world.EmplaceResource<RadarFeedback>();
	for (std::size_t index = 0; index < game.DefinitionCount(); ++index)
	{
		const content::ObjectDefinition &kind = game.Definition(static_cast<std::uint32_t>(index));
		bool shown = kind.radarPriority != "NOT_ON_RADAR";
		if (kind.radarPriority.empty() || kind.radarPriority == "INVALID")
			shown = kind.Is("CAPTURABLE") ||
				std::any_of(kind.modules.begin(), kind.modules.end(), [](const content::ModuleEntry &module) { return module.type == "GarrisonContain"; });
		radar.onRadar.push_back(shown);
	}
	radar.penaltyDamage = content::DamageTypeIndex("PENALTY").value_or(0xFFFFFFFFu);
	radar.healingDamage = content::DamageTypeIndex("HEALING").value_or(0xFFFFFFFFu);
	if (setup.labels)
	{
		const auto &labels = setup.labels;
		radar.underAttack = labels("RADAR:UnderAttack");
		radar.unitUnderAttack = labels("RADAR:UnitUnderAttack");
		radar.harvesterUnderAttack = labels("RADAR:HarvesterUnderAttack");
		radar.structureUnderAttack = labels("RADAR:StructureUnderAttack");
		radar.infiltration = labels("RADAR:Infiltration");
		// BattlePlanUpdate's message labels, by definition.
		for (std::size_t index = 0; index < game.DefinitionCount(); ++index)
		{
			std::array<std::u16string, 3> messages;
			for (const content::ModuleEntry &module : game.Definition(static_cast<std::uint32_t>(index)).modules)
				if (module.block != nullptr && module.type == "BattlePlanUpdate")
				{
					constexpr std::array<std::string_view, 3> keys{"BombardmentMessageLabel", "HoldTheLineMessageLabel", "SearchAndDestroyMessageLabel"};
					for (std::size_t plan = 0; plan < keys.size(); ++plan)
						if (const auto *node = module.block->Find(keys[plan]); node != nullptr && !node->values.empty())
							messages[plan] = labels(node->Value());
					break;
				}
			radar.battlePlanMessages.push_back(std::move(messages));
		}
	}
	const auto misc = [&](std::string_view field) {
		const auto found = setup.content->miscAudio.find(field);
		return found != setup.content->miscAudio.end() ? found->second : std::string{};
	};
	radar.harvesterSound = misc("RadarNotifyHarvesterUnderAttackSound");
	radar.structureSound = misc("RadarNotifyStructureUnderAttackSound");
	radar.infiltrationSound = misc("RadarNotifyInfiltrationSound");
}

// InGameUI's messages: its colours and delay (MessageDelayMS / 30 / 1000, whole numbers), the words and the players'
// names.
inline void EmplaceInGameMessages(ecs::World &world, session::SessionView &game, const HudSetup &setup)
{
	auto &messages = world.EmplaceResource<InGameMessages>();
	messages.color1 = setup.inGameUi->messageColor1;
	messages.color2 = setup.inGameUi->messageColor2;
	messages.timeoutTicks = static_cast<std::uint64_t>(std::max<std::int64_t>(setup.inGameUi->messageDelayMs / 30 / 1000, 0));
	messages.defeatedText = setup.defeatedText;
	if (setup.labels)
	{
		const auto &labels = setup.labels;
		messages.upgradeCompleteText = labels("UPGRADE:UpgradeComplete");
		messages.overchargeExhaustedText = labels("GUI:OverchargeExhausted");
		messages.rallySetText = labels("GUI:RallyPointSet");
		messages.rallyNoPathText = labels("GUI:RallyPointNoPath");
		messages.beaconPlacedText = labels("GUI:BeaconPlaced");
		messages.tooManyBeaconsText = labels("GUI:TooManyBeacons");
		messages.beaconFailedText = labels("GUI:BeaconPlacementFailed");
		for (const auto &[name, object] : setup.content->objects)
			if (!object.displayName.empty() && !messages.displayNames.contains(object.displayName))
				messages.displayNames.emplace(object.displayName, labels(object.displayName));
		for (const content::UpgradeContent &upgrade : setup.content->upgrades.upgrades)
			messages.upgradeNames.push_back(upgrade.displayName.empty() ? std::u16string{} : labels(upgrade.displayName));
	}
	if (const auto *roster = world.FindResource<engine::gameplay::TeamRoster>())
		for (const auto &[name, shown] : setup.playerNames)
			if (const auto player = roster->FindPlayer(name))
			{
				if (messages.playerNames.size() <= *player)
					messages.playerNames.resize(*player + 1);
				messages.playerNames[*player] = shown;
			}
}

inline void EmplaceHudResources(ecs::World &world, session::SessionView &game, const HudSetup &setup)
{
	world.EmplaceResource<CameoFlashes>();
	world.EmplaceResource<CinematicText>();
	world.EmplaceResource<PopupMessage>().color = setup.inGameUi->popupMessageColor;
	world.EmplaceResource<EvaState>().catalog = *setup.eva;
	world.EmplaceResource<RadarEvents>();
	world.EmplaceResource<hud::SuperweaponFlash>(); // the ready countdowns' flash (InGameUI's, kept across frames)
	EmplaceRadarFeedback(world, game, setup);
	EmplaceInGameMessages(world, game, setup);
	// InGameUI's named timers: their flash every NamedTimerCountdownFlashDuration frames (whole ticks).
	world.EmplaceResource<NamedTimers>().flashTicks =
		static_cast<std::uint64_t>(std::max<std::int64_t>(setup.inGameUi->namedTimerFlashFrames.Floor(), 0));
	// ScriptEngine::newMap: every map starts on a fade in from black.
	world.EmplaceResource<ScreenFade>(StartFade());
	// The military caption's colour (InGameUI.ini) and typing (Language.ini: letters MilitaryCaptionSpeed ticks apart,
	// MilitaryCaptionDelayMS before the first and each line, in whole ticks).
	auto &caption = world.EmplaceResource<MilitaryCaption>();
	caption.baseColor = setup.inGameUi->militaryCaptionColor;
	caption.speedTicks = static_cast<std::uint64_t>(std::max(setup.language->militaryCaptionSpeed, 0));
	caption.delayTicks = static_cast<std::uint64_t>(std::max(30 * setup.language->militaryCaptionDelayMs / 1000, 0));
}

// The interface's systems, once a tick (stateless: one shared instance each), and what they run after: the messages
// before the caption, the caption before EVA, the notices in InGameUI's order, the radar's events last before EVA.
inline void RegisterHudTickSystems(ecs::SystemRegistry &registry)
{
	static InGameMessageSystem inGameMessages;
	static MilitaryCaptionSystem militaryCaption;
	static ScreenFadeSystem screenFade;
	static ScriptFlashSystem scriptFlash;
	static EvaSystem eva;
	static ProductionPresentationSystem productionPresentation;
	static OverchargeNoticeSystem overchargeNotices;
	static RallyNoticeSystem rallyNotices;
	static BeaconSystem beacons;
	static RadarEventSystem radarEvents;
	registry.Register(inGameMessages);
	registry.Register(militaryCaption);
	registry.Register(screenFade);
	registry.Register(scriptFlash);
	// Drawable::updateDrawable after the logic's own colour flashes (EMPUpdate).
	registry.OrderBefore<EmpSparkSystem, ScriptFlashSystem>();
	registry.OrderBefore<InGameMessageSystem, MilitaryCaptionSystem>();
	registry.OrderBefore<MilitaryCaptionSystem, EvaSystem>();
	registry.Register(eva);
	registry.OrderBefore<InGameMessageSystem, EvaSystem>();
	registry.Register(productionPresentation);
	registry.OrderBefore<InGameMessageSystem, ProductionPresentationSystem>();
	registry.OrderBefore<MilitaryCaptionSystem, ProductionPresentationSystem>();
	registry.OrderBefore<CratePresentationSystem, ProductionPresentationSystem>();
	registry.Register(overchargeNotices);
	registry.OrderBefore<InGameMessageSystem, OverchargeNoticeSystem>();
	registry.OrderBefore<ProductionPresentationSystem, OverchargeNoticeSystem>();
	registry.Register(rallyNotices);
	registry.OrderBefore<AbilityFeedbackSystem, RallyNoticeSystem>();
	registry.OrderBefore<DisabledSoundSystem, RallyNoticeSystem>();
	registry.OrderBefore<OverchargeNoticeSystem, RallyNoticeSystem>();
	registry.Register(beacons);
	registry.OrderBefore<RallyNoticeSystem, BeaconSystem>();
	registry.Register(radarEvents);
	registry.OrderBefore<BeaconSystem, RadarEventSystem>();
	registry.OrderBefore<OverchargeNoticeSystem, RadarEventSystem>();
	registry.OrderBefore<RallyNoticeSystem, RadarEventSystem>();
	registry.OrderBefore<ProductionPresentationSystem, RadarEventSystem>();
	registry.OrderBefore<RadarEventSystem, EvaSystem>();
	registry.OrderBefore<ProductionPresentationSystem, DisabledSoundSystem>();
	registry.OrderBefore<EvaSystem, EmpSparkSystem>();
}
}
