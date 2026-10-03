export module games.generalszh.presentation.scripted.algorithms.client_script_effects;
import std;

export import games.generalszh.presentation.scripted.resources.scripted_presentation;
export import games.generalszh.scripting.presentation_vocabulary;
export import games.generalszh.session.session_view;
import engine.ecs.core.world;
import Engine.Core.Math.FixedPresentation;
import games.generalszh.presentation.objects.resources.breeze;
import games.generalszh.presentation.objects.resources.look_catalog;
import games.generalszh.presentation.objects.components.object_presentation;
import games.generalszh.presentation.objects.components.object_icons;
import games.generalszh.presentation.objects.resources.presentation_resources;
import games.generalszh.presentation.interaction.components.selected;
import games.generalszh.presentation.audio.components.sound_loops;
import games.generalszh.presentation.hud.algorithms.screen_fade_steps;
import games.generalszh.presentation.camera.algorithms.screen_filters;
import games.generalszh.presentation.hud.algorithms.radar_event_rules;
import games.generalszh.presentation.hud.algorithms.named_timer_lines;
import games.generalszh.presentation.hud.algorithms.cameo_flash_steps;
import games.generalszh.presentation.hud.algorithms.message_list;
import games.generalszh.presentation.hud.algorithms.cinematic_text_layout;
import games.generalszh.presentation.hud.algorithms.popup_message_layout;
import games.generalszh.presentation.hud.algorithms.military_caption_typing;
import engine.gameplay.common.appearance.components.indicator_color;
import engine.gameplay.common.identity.components.owner;

// What the local player's scripts do to their presentation (ScriptEngine's client-side actions: music and levels, EVA,
// the radar, letterbox and fades, flashes, timers, messages, cinematic text, popups, captions, victory and defeat), as
// each command arrives: the scripts' settings (ScriptedPresentation) and the presentation's own resources and side
// tables in the world. What the host keeps for them comes with it (ClientScriptHost).
export namespace generalszh::presentation
{
struct ClientScriptHost
{
	session::SessionView &game;
	std::function<std::u16string(std::string_view)> labels; // the game's strings (GameText::fetch)
	std::function<float(float, float)> terrainHeight;       // the ground's height there
	std::function<void(std::uint64_t)> stopSound;           // an ambient sound to stop at once
	std::uint64_t scriptTick{0};                            // the local scripts' tick
	std::uint64_t presentationTicks{0};
	float viewportWidth{800}, viewportHeight{600};
	std::uint32_t logicTicksPerSecond{30};
	float &speed; // the simulation's ticks a second against LogicTicksPerSecond (setLogicTimeScaleFps)

	Engine::Math::Vector3 Ground(const Engine::Math::FixedVector3 &position) const
	{
		const float x = Engine::Math::ToFloat(position.x), y = Engine::Math::ToFloat(position.y);
		return {x, y, terrainHeight ? terrainHeight(x, y) : 0.0f};
	}
};

inline void ApplyClientScript(ecs::World &world, ScriptedPresentation &settings, ClientScriptHost &host, const scripting::ClientScriptCommand &command)
{
	using Kind = scripting::ClientScriptCommand::Kind;
	switch (command.kind)
	{
	case Kind::MusicTrack: settings.musicTrack = command.text; break;
	case Kind::MusicVolume: settings.musicVolume = command.percent; break;
	case Kind::SoundVolume: settings.soundVolume = command.percent; break;
	case Kind::SpeechVolume: settings.speechVolume = command.percent; break;
	case Kind::SpeechPlay: settings.speechQueue.push_back(command.text); break;
	case Kind::Movie: settings.movies.push_back(command.text); break;
	case Kind::QuickVictory:
		settings.matchEnd = ScriptedPresentation::MatchEnd::QuickVictory;
		settings.matchEndTick = host.scriptTick;
		settings.inputDisabled = true;
		break;
	case Kind::SoundDisable: settings.disabledSounds.push_back(command.text); break;
	case Kind::AudioVolumeOverride: settings.soundVolumeOverrides.emplace_back(command.text, command.percent); break;
	case Kind::EvaEnabled: settings.evaEnabled = command.flag; break;
	case Kind::RadarForceEnable: settings.radarForced = true; break;
	case Kind::RadarRevertToNormal: settings.radarForced = false; break;
	case Kind::RadarHidden: settings.radarHidden = command.flag; break;
	case Kind::BorderShroudDisabled: settings.borderShroudDisabled = true; break;
	case Kind::BorderShroudEnabled: settings.borderShroudDisabled = false; break;
	case Kind::DrawIconUi: settings.drawIconUi = command.flag; break;
	case Kind::TreeSway:
		// ScriptEngine::doSetTreeSway: a new breeze (trees roll their sway again); a period of at least a frame.
		if (auto *breeze = world.FindResource<Breeze>())
		{
			++breeze->version;
			breeze->direction = Engine::Math::ToFloat(command.numbers[0]);
			breeze->directionX = std::sin(breeze->direction);
			breeze->directionY = std::cos(breeze->direction);
			breeze->intensity = Engine::Math::ToFloat(command.numbers[1]);
			breeze->lean = Engine::Math::ToFloat(command.numbers[2]);
			breeze->periodFrames = std::max(Engine::Math::ToFloat(command.numbers[3]), 1.0f);
			breeze->randomness = Engine::Math::ToFloat(command.numbers[4]);
		}
		break;
	case Kind::OcclusionMode: settings.occlusion = command.flag; break;
	case Kind::ParticleCapMode: settings.particleCap = command.flag; break;
	case Kind::SpecialPowerDisplay: settings.specialPowerDisplayDisabled = !command.flag; break;
	case Kind::InputEnabled:
		// doDisableInput (the selection dropped: deselectAllDrawables) / doEnableInput.
		settings.inputDisabled = !command.flag;
		if (!command.flag)
			world.Side<Selected>().Clear();
		break;
	case Kind::Letterbox:
		if (settings.letterbox != command.flag)
			++settings.letterboxChanges;
		settings.letterbox = command.flag;
		break;
	case Kind::BlackWhite:
		settings.blackWhite = command.flag;
		settings.blackWhiteFrames = command.percent;
		// doBlackWhiteMode: the view's filter grey and fading in, or (while it is the grey one) fading out.
		if (auto *filter = world.FindResource<ViewFilter>())
		{
			if (command.flag)
				StartBlackWhite(*filter, static_cast<std::int32_t>(command.percent));
			else
				EndBlackWhite(*filter, static_cast<std::int32_t>(command.percent));
		}
		break;
	case Kind::Fade:
		// ScriptEngine::setFade, first stepped the tick after its script's.
		if (auto *fade = world.FindResource<ScreenFade>())
		{
			const auto real = [](Engine::Math::Fixed value) { return Engine::Math::ToFloat(value); };
			SetFade(*fade, static_cast<FadeKind>(command.percent), real(command.numbers[0]), real(command.numbers[1]),
				command.numbers[2].Round(), command.numbers[3].Round(), command.numbers[4].Round(), host.presentationTicks + 1);
		}
		break;
	case Kind::RadarEvent:
		// Radar::createEvent (4 seconds), from the logic frame its script ran on.
		if (auto *radar = world.FindResource<RadarEvents>())
		{
			const auto at = host.Ground(command.position);
			CreateRadarEvent(*radar, {at.x, at.y, at.z}, static_cast<RadarEventType>(command.percent), host.presentationTicks);
		}
		break;
	case Kind::LogicRate:
	{
		// setLogicTimeScaleFps (0: GameData's FramesPerSecondLimit): the simulation's ticks a second, rendering apart.
		const std::int64_t fps = command.percent != 0 ? command.percent : host.game.Content().gameData.framesPerSecondLimit;
		if (fps > 0)
			host.speed = std::clamp(static_cast<float>(fps) / static_cast<float>(host.logicTicksPerSecond), 0.05f, 16.0f);
		break;
	}
	case Kind::ObjectSound:
	{
		// Drawable::enableAmbientSoundFromScript on the named unit's drawable.
		ecs::World &world = world;
		const ecs::Entity unit = host.game.Named(command.subject);
		if (!world.IsAlive(unit))
			break;
		auto &loops = world.Side<SoundLoops>();
		if (loops.Get(unit) == nullptr)
		{
			SoundLoops fresh;
			loops.Insert(unit, &fresh);
		}
		SoundLoops &mine = *loops.Get(unit);
		mine.scriptOff = command.flag ? 0u : 1u;
		if (command.flag)
			mine.scriptStart = 1;
		else if (mine.ambient != 0 && host.stopSound)
		{
			host.stopSound(mine.ambient);
			mine.ambient = 0;
		}
		break;
	}
	case Kind::NamedTimer:
		// InGameUI::addNamedTimer with TheGameText->fetch of the label.
		if (auto *timers = world.FindResource<NamedTimers>())
			AddNamedTimer(*timers, command.subject,
				host.labels ? host.labels(command.text) : std::u16string(command.text.begin(), command.text.end()), command.flag);
		break;
	case Kind::NamedTimerHide:
		if (auto *timers = world.FindResource<NamedTimers>())
			RemoveNamedTimer(*timers, command.subject);
		break;
	case Kind::NamedTimersShown:
		if (auto *timers = world.FindResource<NamedTimers>())
			timers->shown = command.flag;
		break;
	case Kind::Flash:
	{
		// doNamedFlash (only for some seconds) / doTeamFlash: LOGICFRAMES_PER_SECOND * seconds / DRAWABLE_FRAMES_PER_FLASH
		// flashes, white or its player's colour (getIndicatorColor).
		ecs::World &world = world;
		std::vector<ecs::Entity> targets;
		if (command.flag)
			targets = host.game.TeamMembers(command.subject);
		else if (command.percent > 0)
			targets.push_back(host.game.Named(command.subject));
		const auto *catalog = world.FindResource<LookCatalog>();
		const std::int32_t count = static_cast<std::int32_t>(30 * command.percent / 15);
		for (const ecs::Entity target : targets)
		{
			if (!world.IsAlive(target))
				continue;
			std::array<float, 3> color{1.0f, 1.0f, 1.0f};
			// Object::getIndicatorColor: a colour a script gave it, else its player's.
			if (const auto *custom = world.Get<engine::gameplay::IndicatorColor>(target); command.text != "WHITE" && custom != nullptr && custom->argb != 0)
				color = {static_cast<float>((custom->argb >> 16) & 0xFF) / 255.0f, static_cast<float>((custom->argb >> 8) & 0xFF) / 255.0f,
					static_cast<float>(custom->argb & 0xFF) / 255.0f};
			else if (command.text != "WHITE" && catalog != nullptr)
				if (const auto *owner = world.Get<engine::gameplay::Owner>(target))
				{
					const auto house = catalog->ColorOf(owner->player);
					color = {house[0], house[1], house[2]};
				}
			auto &flashes = world.Side<ScriptFlash>();
			ScriptFlash flash{count, color};
			if (auto *existing = flashes.Get(target))
				*existing = flash;
			else
				flashes.Insert(target, &flash);
		}
		break;
	}
	case Kind::Emoticon:
	{
		// Drawable::setEmoticon: the old emoticon goes; a known Animation2D is made now, kept through the script's frame
		// plus the duration (a negative one: FOREVER). The overlay leaves out a name the game has no animation for.
		std::vector<ecs::Entity> targets;
		if (command.flag)
			targets = host.game.TeamMembers(command.subject);
		else
			targets.push_back(host.game.Named(command.subject));
		const auto *frame = world.FindResource<PresentationFrame>();
		const double clock = frame != nullptr ? frame->clock : 0.0;
		auto &emoticons = world.Side<ObjectEmoticon>();
		for (const ecs::Entity target : targets)
		{
			if (!world.IsAlive(target))
				continue;
			ObjectEmoticon emoticon{command.text, clock, IconRoll(target, static_cast<ObjectIcon>(ObjectIconCount), clock),
				command.percent >= 0 ? host.scriptTick + static_cast<std::uint64_t>(command.percent) : ~std::uint64_t{0}};
			if (auto *existing = emoticons.Get(target))
				*existing = std::move(emoticon);
			else
				emoticons.Insert(target, &emoticon);
		}
		break;
	}
	case Kind::CameoFlash:
		// doCameoFlash: TheControlBar->findCommandButton (none: nothing), its flash count, the bar's flash check on.
		if (auto *flashes = world.FindResource<CameoFlashes>())
			StartCameoFlash(*flashes, command.text, command.percent, host.game.Content().commands.Button(command.text) != nullptr);
		break;
	case Kind::DisplayText:
		// doDisplayText: InGameUI::message (TheGameText->fetch of the label), on the logic frame its script ran on.
		if (auto *messages = world.FindResource<InGameMessages>())
			AddMessage(*messages, host.labels ? host.labels(command.text) : std::u16string(command.text.begin(), command.text.end()), host.presentationTicks);
		break;
	case Kind::CinematicText:
		// doDisplayCinematicText: TheGameText->fetch of the label, the font from its description, the time in frames.
		if (auto *cinematic = world.FindResource<CinematicText>())
			SetCinematicText(*cinematic, host.labels ? host.labels(command.text) : std::u16string(command.text.begin(), command.text.end()),
				command.subject, command.percent);
		break;
	case Kind::PopupMessage:
		// doInGamePopupMessage: InGameUI::popupMessage (TheGameText->fetch), placed on the display, pausing the game when
		// asked (the host holds its ticks while it shows).
		if (auto *popup = world.FindResource<PopupMessage>())
			ShowPopupMessage(*popup, host.labels ? host.labels(command.text) : std::u16string(command.text.begin(), command.text.end()),
				command.numbers[0].Round(), command.numbers[1].Round(), command.numbers[2].Round(), command.flag,
				static_cast<std::int32_t>(host.viewportWidth), static_cast<std::int32_t>(host.viewportHeight));
		break;
	case Kind::SkyBox: settings.skyBox = command.flag; break;
	case Kind::Weather: settings.weatherShown = command.flag; break;
	case Kind::InfantryLighting: settings.infantryLightOverride = Engine::Math::ToFloat(command.numbers[0]); break;
	case Kind::RadarRefresh: ++settings.radarRefreshes; break;
	case Kind::FlatSoundsPaused: break; // heard (Hear)
	case Kind::MilitaryCaption:
		// InGameUI::militarySubtitle (TheGameText->fetch of the label), from the logic frame its script ran on.
		if (auto *caption = world.FindResource<MilitaryCaption>())
			ShowCaption(*caption, host.labels ? host.labels(command.text) : std::u16string(command.text.begin(), command.text.end()), command.percent,
				host.presentationTicks);
		break;
	// doVictory / doDefeat: the window closes whatever showed, input goes off; doLocalDefeat keeps input.
	case Kind::Victory:
	case Kind::Defeat:
	case Kind::LocalDefeat:
		settings.matchEnd = command.kind == Kind::Victory ? ScriptedPresentation::MatchEnd::Victory
			: command.kind == Kind::Defeat ? ScriptedPresentation::MatchEnd::Defeat : ScriptedPresentation::MatchEnd::LocalDefeat;
		settings.matchEndTick = host.scriptTick;
		settings.inputDisabled = settings.inputDisabled || command.kind != Kind::LocalDefeat;
		break;
	}
}
}
