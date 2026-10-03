module;
#include <algorithm>
#include <array>
#include <map>
#include <cmath>
#include <random>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <type_traits>
#include <vector>

module games.generalszh.hosts.game.game_client;

import games.generalszh.presentation.objects.algorithms.scenery_setup;
import games.generalszh.presentation.objects.systems.rope_view_systems;
import games.generalszh.gameplay.beacons.resources.beacons;
import games.generalszh.presentation.interaction.algorithms.force_select;
import games.generalszh.presentation.models.model_library_system;
import games.generalszh.presentation.scripted.algorithms.client_script_effects;
import games.generalszh.presentation.scripted.algorithms.camera_script_effects;
import games.generalszh.presentation.composition.schedules;
import games.generalszh.presentation.composition.objects;
import games.generalszh.presentation.composition.hud;
import games.generalszh.presentation.composition.interaction;
import games.generalszh.presentation.audio.systems.unit_voice_system;
import games.generalszh.presentation.objects.resources.waypoint_paths;
import games.generalszh.presentation.composition.audio;
import games.generalszh.presentation.interaction.algorithms.rule_queries;
import games.generalszh.presentation.hud.algorithms.overlay_extract;
import games.generalszh.presentation.hud.resources.military_caption;
import games.generalszh.presentation.interaction.components.selected;
import games.generalszh.presentation.hud.resources.in_game_messages;
import games.generalszh.presentation.hud.algorithms.message_list;
import games.generalszh.gameplay.world.resources.chat_inbox;
import games.generalszh.presentation.hud.resources.screen_fade;
import engine.camera.model.rts_camera;
import engine.time.simulation_time;
import games.generalszh.session.session_view;
import games.generalszh.hud.superweapon_timers;
import games.generalszh.presentation.hud.systems.eva_system;
import games.generalszh.content.eva.eva_content;
import games.generalszh.scripting.core_vocabulary;
import games.generalszh.scripting.legacy_calls;
import games.generalszh.content.objects.kind_of;
import games.generalszh.scripting.presentation_vocabulary;
import games.generalszh.scripting.match_vocabulary;
import engine.gameplay.common.appearance.components.indicator_color;
import games.generalszh.presentation.hud.algorithms.radar_event_rules;
import games.generalszh.presentation.hud.algorithms.named_timer_lines;
import games.generalszh.content.global.language_fonts;
import games.generalszh.content.global.in_game_ui;
import games.generalszh.content.global.mouse;
import engine.gameplay.rts.match.resources.match_outcome;
import engine.gameplay.rts.sciences.resources.player_ranks;
import engine.gameplay.rts.teams.resources.team_roster;
import games.generalszh.presentation.audio.algorithms.audio_schedule;
import games.generalszh.presentation.audio.sound_files;
import engine.audio.adapters.preferred_output;
import games.generalszh.presentation.objects.algorithms.tick_effects;
import engine.ecs.scheduler.scheduler;
import games.generalszh.presentation.objects.algorithms.presentation_schedule;
import games.generalszh.presentation.objects.systems.placement_ghost_system;
import games.generalszh.presentation.objects.systems.radius_decal_view_system;
import games.generalszh.presentation.interaction.algorithms.selection_setup;
import games.generalszh.presentation.interaction.algorithms.mouseover_names;
import games.generalszh.content.stealth.stealth_content;
import games.generalszh.content.fire.fire_content;
import games.generalszh.content.death.death_content;
import games.generalszh.content.topple.topple_content;
import games.generalszh.presentation.rendering.model_bones;
import engine.gameplay.rts.vision.resources.shroud_map;
import engine.jobs.job_system;
import games.generalszh.content.loading.game_content;
import games.generalszh.content.loading.map_overrides;
import games.generalszh.content.objects.model_draw;
import games.generalszh.content.locomotors.locomotor_catalog;
import games.generalszh.presentation.animation.tread_roll;
import games.generalszh.content.objects.model_states;
import games.generalszh.content.objects.model_conditions;
import Engine.Core.Math.FixedPresentation;
import Engine.Core.Math.Vector3;
import Graphics.Scene.AffineTransform;
import games.generalszh.presentation.camera.algorithms.camera_shaking;
import games.generalszh.presentation.camera.algorithms.motion_blur_steps;
import games.generalszh.presentation.camera.algorithms.screen_filters;
import games.generalszh.presentation.camera.algorithms.selection_focus;
import games.generalszh.content.global.draw_group_info;
import games.generalszh.presentation.hud.algorithms.in_game_ui_layout;
import games.generalszh.presentation.camera.algorithms.camera_slave;
import games.generalszh.presentation.camera.algorithms.camera_keys;
import engine.gameplay.rts.containment.components.transport;
import games.generalszh.presentation.interaction.algorithms.select_keys;
import games.generalszh.presentation.interaction.resources.unit_voice_cues;
import games.generalszh.presentation.interaction.resources.interaction_resources;
import games.generalszh.content.objects.object_status;
import engine.gameplay.common.identity.components.object_id;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.status.components.status_flags;
import engine.gameplay.rts.slaves.components.slaved;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.death.components.dying;
import games.generalszh.presentation.hud.resources.radar_events;
import games.generalszh.presentation.rendering.infantry_lighting;
import games.generalszh.presentation.rendering.bridge_models;
import games.generalszh.presentation.roads.algorithms.road_network;
import games.generalszh.presentation.hud.algorithms.cameo_flash_steps;
import games.generalszh.presentation.hud.algorithms.cinematic_text_layout;
import games.generalszh.presentation.hud.algorithms.popup_message_layout;

namespace generalszh::host
{
namespace
{
std::uint64_t Key(ecs::Entity entity) noexcept { return (static_cast<std::uint64_t>(entity.index) << 32) | entity.generation; }
constexpr double LogicTicksPerSecond = 30.0;
}

// The local player's match scripts and what they run on (the runtime refers to its scenario and vocabulary).
struct LocalScripts
{
	engine::level::Scenario scenario;
	engine::scripting::Vocabulary vocabulary;
	std::vector<scripting::ClientScriptCommand> commands;
	std::unique_ptr<scripting::LocalMatchHost> host;
	std::optional<engine::scripting::ScriptRuntime> runtime;
	std::uint64_t tick{0};
	std::map<std::string, std::int64_t> recorded; // PLAYER_LOST_OBJECT_TYPE's counts seen, on this machine
};

struct GameClient::State
{
	TerrainHeight terrainHeight;
	std::u16string addCashText{u"$%d"}; // GUI:AddCash
	std::u16string loseCashText{u"-$%d"}; // GUI:LoseCash
	std::optional<std::pair<float, float>> pinnedLook;
	content::GameContent content;
	// This match's content with its map's Object overrides (none: `content`); declared before the match that reads it.
	std::optional<content::GameContent> matchContent;
	// Single player is a match of one through an in-process relay.
	std::unique_ptr<session::ClientMatch> simulation;
	std::unique_ptr<LocalScripts> localScripts;
	// GameLogic::isInMultiplayerGame (a network match) and isInReplayGame: beacons are placed and removed only in the one
	// and never in the other.
	bool multiplayerMatch{false};
	bool replayMatch{false};
	std::uint64_t generation{0};
	engine::camera::RtsCamera camera;
	float speed{1.0f};
	float baseSpeed{1.0f}; // the player's own speed (a script's SET_FPS_LIMIT lasts only its match)
	ClientSettings settings;
	presentation::ShroudCells shroud; // the viewer's shroud as last drawn
	std::int32_t detailLevel{presentation::detail_level::High}; // the options' StaticGameLOD
	presentation::CustomDetail customDetail;                     // the player's own detail (Custom)
	bool retaliation{true};          // the options' Retaliation
	// Whose eyes the world is seen through without a seat (the shell map): PlayerList::newGame's local player, the
	// map's first human player, else its first that is not neutral.
	std::uint32_t eyes{presentation::PresentationFrame::NoViewer};
	// This frame's objects as drawn (gathered from presentation's output, in order).
	std::vector<ObjectInstance> instances;

	double clock{0};
	// Each player's colour (the map's playerColor), for team-coloured parts.
	std::vector<std::array<float, 4>> playerColors;
	bool night{false}; // the map's time of day is night
	// Each player's day colour (Player::getPlayerColor, 0xRRGGBB: the mouse-over tooltip's), and whether the match is a
	// multiplayer one (RecorderClass::isMultiplayer: a skirmish, a network game or a replay of one).
	std::vector<std::uint32_t> playerDayColors;
	bool multiplayerGame{false};
	// The map's weather (TheWeatherSetting with its map override), applied to the snow each time it is bound.
	std::optional<presentation::WeatherSetting> weather;
	// The next match's map.ini and its loader, and the match's content with its Object overrides (none: the game's).
	const engine::config::Document *mapIni{nullptr};
	content::ContentLoader *mapLoader{nullptr};
	bool scriptTrace{false}; // matches write each script action run to stderr
	std::string musicBeforeMenu; // what played before a menu's music
	std::vector<engine::audio::SoundHandle> hostSounds; // the load screens' sounds (PlayHostSound), kept over a match start


	// Sound: content, the mixer the output pulls from, decoded files, the
	// player sequencing them (sound's systems decide what plays). The output
	// is declared last so it stops pulling before anything it reads goes.
	presentation::AudioContent audio;
	engine::audio::Mixer mixer{engine::audio::AudioOutput::SampleRate};
	std::unique_ptr<presentation::SoundFiles> soundFiles;
	std::unique_ptr<engine::audio::SoundPlayer> player;
	std::unique_ptr<engine::audio::AudioOutput> output;

	// Visual effects: particle systems and FX lists, stepped on their own
	// 30 Hz presentation clock (scaled by the game speed) and updated in
	// parallel on a small job pool of their own.
	presentation::EffectsContent effects;
	// The game's own sounds and particle systems a match's map.ini overrides (none: one it added), restored at the next start.
	using SavedSound = std::pair<std::string, std::optional<engine::audio::SoundEventDefinition>>;
	using SavedParticle = std::pair<std::string, std::optional<engine::effects::ParticleSystemDefinition>>;
	std::vector<SavedSound> soundsBefore;
	std::vector<SavedParticle> particlesBefore;
	// The map-drawn bridges' models and textures by damage state (W3DBridge::load), read once from the install.
	presentation::BridgeArt bridgeArt;
	// The map's roads as the road buffer draws them.
	presentation::RoadGeometry roads;
	std::unique_ptr<engine::jobs::JobSystem> effectJobs;
	std::unique_ptr<engine::effects::ParticleWorld> particles;
	// Where models' bones are, for effects that sit on them.
	presentation::ModelBones bones;
	// Presentation systems over the simulation's own world (its components
	// read between ticks, their state in side tables on its entities): once a
	// tick and once a frame. Rebuilt when a restore replaces the world.
	std::unique_ptr<ecs::SystemRegistry> tickSystems;
	std::unique_ptr<ecs::SystemRegistry> frameSystems;
	std::unique_ptr<ecs::Scheduler> tickScheduler;
	std::unique_ptr<ecs::Scheduler> frameScheduler;
	std::function<std::u16string(std::string_view)> labels; // the game's strings (GameText::fetch)
	content::EvaCatalog evaCatalog; // Eva.ini
	content::InGameUiContent inGameUi;
	content::MouseContent mouse;
	content::LanguageFonts language;
	content::DrawGroupInfoContent drawGroupInfo; // DrawGroupInfo.ini with Language.ini's DrawGroupInfoFont
	Engine::Math::FixedVector2 playableExtent{}; // the active boundary the camera keeps within
	std::vector<std::pair<std::string, std::u16string>> playerNames;
	std::u16string defeatedText{u"%ls has been defeated."};
	// The player's pointer since the last frame (presses and releases gather until the interaction reads them).
	PointerState pointer;
	float viewportWidth{800}, viewportHeight{600};
	std::uint64_t presentationTicks{0};
	std::uint64_t presentationFrames{0};
	// W3DView's slave mode (cameraEnableSlaveMode): the named unit and bone the camera rides, and this frame's bone
	// transform once the renderer has handed it back.
	bool cameraSlaved{false};
	std::string slaveUnit;
	std::string slaveBone;
	std::optional<std::array<float, 12>> slaveView;

	// W3DView::isCameraMovementFinished: a zoom motion blur counts as finished, else the camera's scripted moves.
	bool CameraMovementFinished()
	{
		const auto *filter = simulation ? Game().World().FindResource<presentation::ViewFilter>() : nullptr;
		return (filter != nullptr && presentation::MotionBlurZooming(*filter)) || camera.IsCameraMovementFinished();
	}

	void BindPresentation()
	{
		ecs::World &world = Game().World();
		namespace composition = presentation::composition;
		// Where models' bones sit, from the renderer's loaded models (effects ride on them).
		presentation::BonePoses poses{[this](std::string_view model, std::string_view bone) {
			presentation::BoneLookup lookup;
			lookup.ready = bones.Ready(model);
			if (lookup.ready)
				if (const auto pose = bones.Pose(model, bone))
				{
					lookup.found = true;
					lookup.position = pose->position;
					lookup.yaw = pose->yaw;
				}
			return lookup;
		}, [this](std::string_view model, std::string_view bone, bool family) { return bones.Find(model, bone, family); },
			[this](std::string_view model, std::string_view bone, std::string_view ancestor) { return bones.Descends(model, bone, ancestor); }};
		composition::EmplaceObjectResources(world, Game(),
			{playerColors, night && content.gameData.forceModelsToFollowTimeOfDay,
				content.gameData.snowyWeather && content.gameData.forceModelsToFollowWeather, particles.get(), &effects, std::move(poses), addCashText,
				loseCashText, &bridgeArt, &roads});
		if (weather)
			presentation::ApplyWeatherSetting(world.Resource<presentation::SnowField>(), *weather);
		// InGameUI.ini's floating text timing (FloatingTextTimeOut, FloatingTextMoveUpSpeed, FloatingTextVanishRate).
		{
			const presentation::FloatingTextMotion motion = presentation::FloatingTextMotionFor(inGameUi);
			auto &texts = world.Resource<presentation::FloatingTextSettings>();
			texts.timeoutTicks = motion.timeoutTicks;
			texts.riseRate = motion.riseRate;
			texts.vanishPerTick = motion.vanishPerTick;
		}
		ApplyDetail();
		composition::EmplaceAudioResources(world, presentation::AudioHandle{&audio, player.get(), &mixer});
		composition::EmplaceHudResources(world, Game(), {labels, &inGameUi, &language, &evaCatalog, playerNames, defeatedText, matchContent ? &*matchContent : &content});
		composition::EmplaceInteractionResources(world, Game(), mouse, inGameUi);
		composition::EmplaceMouseTooltipResources(world, Game(), mouse, {labels, playerNames, playerDayColors, multiplayerGame});
		world.EmplaceResource<presentation::TerrainHeightHandle>(presentation::TerrainHeightHandle{[this](float x, float y) {
			return terrainHeight ? terrainHeight(x, y) : 0.0f;
		}});
		composition::BindSoundReleases(world, player.get());
		composition::BindObjectEffectReleases(world, particles.get());
		tickScheduler.reset();
		frameScheduler.reset();
		tickSystems = std::make_unique<ecs::SystemRegistry>();
		frameSystems = std::make_unique<ecs::SystemRegistry>();
		composition::RegisterPresentationSystems(*tickSystems, *frameSystems);
		tickSystems->Finalize(world.Components());
		frameSystems->Finalize(world.Components());
		tickScheduler = std::make_unique<ecs::Scheduler>(world, *tickSystems, *effectJobs);
		frameScheduler = std::make_unique<ecs::Scheduler>(world, *frameSystems, *effectJobs);
		tickScheduler->Finalize(engine::time::FixedStep{30});
		frameScheduler->Finalize(engine::time::FixedStep{30});
	}

	std::vector<presentation::PresentedObject> presented;
	double effectClock{0};
	float effectAlpha{0};

	// The tick's effects: muzzle effects where shots were fired, detonations
	// where they landed, and death effects where units fell.

	static engine::audio::Vec3 ToAudio(const Engine::Math::FixedVector3 &position)
	{
		return {Engine::Math::ToFloat(position.x), Engine::Math::ToFloat(position.y), Engine::Math::ToFloat(position.z)};
	}


	// The scripts' music, speech and levels, for sound's mix system.
	void Hear(const scripting::ClientScriptCommand &command)
	{
		auto *commands = simulation ? Game().World().FindResource<presentation::AudioCommands>() : nullptr;
		if (commands == nullptr)
			return;
		using Kind = scripting::ClientScriptCommand::Kind;
		using Audio = presentation::AudioCommand::Kind;
		const float share = static_cast<float>(command.percent) / 100.0f;
		switch (command.kind)
		{
		case Kind::MusicTrack: commands->pending.push_back({Audio::MusicTrack, command.text, command.flag}); break;
		case Kind::MusicVolume: commands->pending.push_back({Audio::MusicVolume, {}, false, share}); break;
		case Kind::SoundVolume: commands->pending.push_back({Audio::SoundVolume, {}, false, share}); break;
		case Kind::SpeechVolume: commands->pending.push_back({Audio::SpeechVolume, {}, false, share}); break;
		case Kind::SpeechPlay: commands->pending.push_back({Audio::SpeechPlay, command.text}); break;
		case Kind::SoundDisable: commands->pending.push_back({Audio::SoundDisable, command.text}); break;
		case Kind::AudioVolumeOverride: commands->pending.push_back({Audio::VolumeOverride, command.text, false, share}); break;
		case Kind::SoundEnable: commands->pending.push_back({Audio::SoundEnable, command.text}); break;
		case Kind::SoundStop: commands->pending.push_back({Audio::SoundStop, command.text}); break;
		case Kind::SoundStopMuted: commands->pending.push_back({Audio::SoundStopMuted}); break;
		case Kind::FlatSoundsPaused: commands->pending.push_back({Audio::FlatSoundsPaused, {}, command.flag}); break;
		case Kind::SoundEffect:
		case Kind::SoundEffectAt:
		case Kind::SoundFromNamed:
		{
			// doPlaySoundEffect / doPlaySoundEffectAt: the local player's; doSoundPlayFromNamed: where the unit is, its
			// owner's (AudioEventRTS on its object).
			ecs::World &world = Game().World();
			auto *sounds = world.FindResource<presentation::SoundRequests>();
			if (sounds == nullptr)
				break;
			presentation::SoundRequest request{command.text};
			const auto &local = world.Resource<presentation::LocalPlayer>();
			request.owner = local.valid ? local.player : presentation::SoundRequest::NoOwner;
			if (command.kind == Kind::SoundEffect)
				request.positioned = false;
			else if (command.kind == Kind::SoundEffectAt)
			{
				const auto at = ToAudio(command.position);
				request.at = {at.x, at.y, at.z};
			}
			else
			{
				const ecs::Entity unit = Game().Named(command.subject);
				const auto *transform = world.IsAlive(unit) ? world.Get<engine::gameplay::Transform>(unit) : nullptr;
				if (transform == nullptr)
					break;
				const auto at = ToAudio(transform->position);
				request.at = {at.x, at.y, at.z};
				const auto *owner = world.Get<engine::gameplay::Owner>(unit);
				request.owner = owner != nullptr ? owner->player : presentation::SoundRequest::NoOwner;
			}
			sounds->pending.push_back(std::move(request));
			break;
		}
		case Kind::ForceSelect:
		{
			// doForceObjectSelection: the team's oldest `text` selected alone, its sound the local player's, the view onto
			// it at once when asked.
			ecs::World &world = Game().World();
			const std::vector<ecs::Entity> members = Game().TeamMembers(command.subject);
			const auto pick = presentation::ForceSelectPick(world, members, command.text, [&](ecs::Entity entity) -> std::string_view {
				const auto definition = Game().DefinitionOf(entity);
				return definition ? std::string_view(Game().Definition(*definition).name) : std::string_view{};
			});
			if (!pick)
				break;
			auto &selected = world.Side<presentation::Selected>();
			selected.Clear();
			selected.Emplace(*pick);
			if (auto *sounds = world.FindResource<presentation::SoundRequests>(); sounds != nullptr && !command.detail.empty())
			{
				presentation::SoundRequest request{command.detail};
				const auto &local = world.Resource<presentation::LocalPlayer>();
				request.owner = local.valid ? local.player : presentation::SoundRequest::NoOwner;
				request.positioned = false;
				sounds->pending.push_back(std::move(request));
			}
			if (command.flag)
				if (const auto *transform = world.Get<engine::gameplay::Transform>(*pick))
					camera.LookAt(Ground(transform->position));
			break;
		}
		default: break;
		}
	}

	Engine::Math::Vector3 Ground(const Engine::Math::FixedVector3 &position) const
	{
		const float x = Engine::Math::ToFloat(position.x), y = Engine::Math::ToFloat(position.y);
		return {x, y, terrainHeight ? terrainHeight(x, y) : 0.0f};
	}

	// LookAtTranslator for the default setup: a right-button drag scrolls at the drag's length from its anchor,
	// arrow keys at SCROLL_AMT (200), each scaled by the scroll speed factors (GameData: horizontal 1.6, vertical
	// 2.0), the frame's share of 30 frames a second and the user's scroll factor (0.5); W3DView::scrollBy turns the
	// screen offset into a move of the pivot (250 pixels a unit through the view plane, turned by the camera's
	// yaw). The wheel: View::zoom by -notches * ZoomHeightPerSecond (10).
	struct Look
	{
		bool scrolling{false};
		float anchorX{0}, anchorY{0};
		// This frame's scroll (for the cursor): scrolling by a drag or the keys, and its offset in pixels.
		bool moving{false};
		float offsetX{0}, offsetY{0};
		// The middle button's drag turning the view (m_isRotating), from where it went down and when.
		bool rotating{false};
		float rotateAnchorX{0}, rotateFirstX{0}, rotateFirstY{0};
		std::uint64_t rotateFrame{0};
	} look;
	// InGameUI's keyboard camera flags, LookAtTranslator's view bookmarks and InGameUI's m_cameraTrackingDrawable.
	presentation::CameraKeys cameraKeys;
	presentation::ViewBookmarks bookmarks;
	bool trackingDrawable{false};

	// The player's scroll speed (OptionPreferences ScrollFactor over 100; none: GameData's KeyboardDefaultScrollSpeedFactor).
	float scrollFactor{-1.0f};

	void LookAround(double gameSeconds)
	{
		if (!simulation)
			return;
		// InGameUI::update's scroll: SCROLL_MULTIPLIER 2, SCROLL_AMT 100 x it; the player's scroll speed
		// (OptionPreferences::getScrollFactor: m_keyboardScrollFactor) and GameData's Horizontal/VerticalScrollSpeedFactor.
		constexpr float scrollMultiplier = 2.0f, scrollAmount = 100.0f * scrollMultiplier;
		const float scrollFactor = this->scrollFactor >= 0.0f ? this->scrollFactor : Engine::Math::ToFloat(content.gameData.keyboardDefaultScrollFactor);
		const float horizontalFactor = Engine::Math::ToFloat(content.gameData.horizontalScrollFactor);
		const float verticalFactor = Engine::Math::ToFloat(content.gameData.verticalScrollFactor);
		// InGameUI::update: a held keypad key turns the view by KeyboardCameraRotateSpeed or zooms it by 10 each client
		// frame (here each 1/30 s of the frame's time).
		{
			const float frames = gameSeconds > 0.0 ? static_cast<float>(30.0 * gameSeconds) : 0.0f;
			const presentation::CameraKeyMotion motion =
				presentation::CameraKeyStep(cameraKeys, frames, Engine::Math::ToFloat(content.gameData.keyboardCameraRotateSpeed));
			if (motion.angle != 0.0f)
				camera.SetAngle(camera.Angle() + motion.angle);
			if (motion.height != 0.0f)
				camera.ZoomBy(motion.height);
		}
		if (pointer.wheel != 0.0f)
		{
			camera.BeginUserAction();
			camera.ZoomBy(-pointer.wheel * 10.0f);
		}
		// The middle button: a drag turns the view 0.01 radians a pixel across; a click (moved 5 pixels at most, let go
		// within 5 frames) puts the angle, pitch and zoom back to their defaults.
		if ((pointer.pressed & 4u) != 0)
		{
			look.rotating = true;
			look.rotateAnchorX = look.rotateFirstX = pointer.x;
			look.rotateFirstY = pointer.y;
			look.rotateFrame = presentationFrames;
		}
		if (look.rotating && pointer.x != look.rotateAnchorX)
		{
			camera.BeginUserAction();
			camera.SetAngle(camera.Angle() + 0.01f * (pointer.x - look.rotateAnchorX));
			look.rotateAnchorX = pointer.x;
		}
		if ((pointer.released & 4u) != 0 && look.rotating)
		{
			look.rotating = false;
			const bool moved = std::abs(pointer.x - look.rotateFirstX) > 5.0f || std::abs(pointer.y - look.rotateFirstY) > 5.0f;
			if (!moved && presentationFrames - look.rotateFrame < 5)
			{
				camera.SetAngleToDefault();
				camera.SetPitchToDefault();
				camera.SetZoomToDefault();
			}
		}
		if ((pointer.pressed & 2u) != 0 && !look.scrolling)
		{
			look.scrolling = true;
			look.anchorX = pointer.x;
			look.anchorY = pointer.y;
		}
		if ((pointer.down & 2u) == 0)
			look.scrolling = false;
		// MoveRMBScrollAnchor (InGameUI::shouldMoveRMBScrollAnchor): the anchor drawn along to within half the screen.
		if (look.scrolling && inGameUi.moveRmbScrollAnchor)
		{
			int anchorX = static_cast<int>(look.anchorX), anchorY = static_cast<int>(look.anchorY);
			presentation::FollowRmbScrollAnchor(anchorX, anchorY, static_cast<int>(pointer.x), static_cast<int>(pointer.y), static_cast<int>(viewportWidth),
				static_cast<int>(viewportHeight));
			look.anchorX = static_cast<float>(anchorX);
			look.anchorY = static_cast<float>(anchorY);
		}
		const float fpsRatio = gameSeconds > 0.0 ? static_cast<float>(std::min(30.0 * gameSeconds, 6.0)) : 0.0f;
		float offsetX = 0.0f, offsetY = 0.0f;
		if (look.scrolling)
		{
			const float vx = pointer.x - look.anchorX, vy = pointer.y - look.anchorY;
			const float length = std::hypot(vx, vy);
			if (length > 0.0f)
			{
				offsetX = horizontalFactor * fpsRatio * length * (vx / length) * scrollMultiplier * scrollFactor;
				offsetY = verticalFactor * fpsRatio * length * (vy / length) * scrollMultiplier * scrollFactor;
			}
		}
		else if (pointer.arrows != 0)
		{
			if ((pointer.arrows & 1u) != 0) offsetY -= verticalFactor * fpsRatio * scrollAmount * scrollFactor;
			if ((pointer.arrows & 2u) != 0) offsetY += verticalFactor * fpsRatio * scrollAmount * scrollFactor;
			if ((pointer.arrows & 4u) != 0) offsetX -= horizontalFactor * fpsRatio * scrollAmount * scrollFactor;
			if ((pointer.arrows & 8u) != 0) offsetX += horizontalFactor * fpsRatio * scrollAmount * scrollFactor;
		}
		look.moving = look.scrolling || pointer.arrows != 0;
		look.offsetX = offsetX;
		look.offsetY = offsetY;
		if (offsetX == 0.0f && offsetY == 0.0f)
			return;
		// W3DView::scrollBy: the offset at 250 pixels a unit, through the view plane, turned by the camera's yaw.
		const auto &view = camera.View();
		const float planeWidth = 2.0f * std::tan(view.horizontalFieldOfView * 0.5f), planeHeight = planeWidth / std::max(view.aspectRatio, 0.01f);
		const float viewX = offsetX * 250.0f / viewportWidth * planeWidth, viewY = -offsetY * 250.0f / viewportHeight * planeHeight;
		const Engine::Math::Vector3 right = view.transform.Basis_X();
		const float yaw = std::atan2(right.y, right.x);
		const float worldX = viewX * std::cos(yaw) - viewY * std::sin(yaw), worldY = viewX * std::sin(yaw) + viewY * std::cos(yaw);
		camera.BeginUserAction();
		camera.ScrollBy({worldX, worldY}, {offsetX, offsetY});
	}

	session::SessionView &Game() { return simulation->View(); }
	const session::SessionView &Game() const { return simulation->View(); }

	void TakeSnapshot()
	{
		// Presentation's once-a-tick systems (definitions and models the session took on first).
		if (tickScheduler)
		{
			presentation::KnowMotionLooks(Game().World().Resource<presentation::MotionLooks>(), Game());
			presentation::KnowLooks(Game().World().Resource<presentation::LookCatalog>(), Game());
			presentation::KnowPartLooks(Game().World().Resource<presentation::LookCatalog>(), Game().World());
			presentation::KnowSupplyLooks(Game().World().Resource<presentation::LookCatalog>(), Game().World(), Game().World().Resource<presentation::BonePoses>());
			presentation::KnowSelectables(Game().World().Resource<presentation::SelectionCatalog>(), Game());
			presentation::KnowMouseoverNames(Game().World().Resource<presentation::MouseoverNames>(), Game());
			presentation::QueueTickSounds(Game().World().Resource<presentation::SoundRequests>(), Game());
			presentation::KnowExhausts(Game().World().Resource<presentation::WeaponExhausts>(), Game());
			presentation::KnowRecoils(Game().World().Resource<presentation::WeaponRecoils>(), Game());
			presentation::KnowFireLoops(Game().World().Resource<presentation::WeaponFireLoops>(), Game());
			presentation::KnowLasers(Game().World().Resource<presentation::WeaponLasers>(), Game());
			presentation::KnowStreams(Game().World().Resource<presentation::WeaponStreams>(), Game());
			presentation::QueueTickLasers(Game().World().Resource<presentation::LaserRequests>(), Game().World().Resource<presentation::WeaponLasers>(), Game());
			presentation::QueueAssistLasers(Game().World().Resource<presentation::LaserRequests>(), Game().World().Resource<presentation::WeaponLasers>(), Game());
			presentation::QueueTickEffects(Game().World().Resource<presentation::FxRequests>(), Game().World().Resource<presentation::LookCatalog>(), Game());
			tickScheduler->Execute(engine::time::SimulationTime{++presentationTicks, engine::time::FixedStep{30}});
		}
		// VictoryConditions::update: the local player (not an observer) defeated, their radar is forced on, once.
		if (!settings.localDefeatSeen)
			if (const auto *outcome = Game().World().FindResource<engine::gameplay::MatchOutcome>(); outcome != nullptr && outcome->enabled)
				if (const auto *local = Game().World().FindResource<presentation::LocalPlayer>(); local != nullptr && local->valid)
					for (const engine::gameplay::MatchStanding &standing : outcome->players)
						if (standing.player == local->player && standing.defeated)
						{
							settings.localDefeatSeen = true;
							settings.radarForced = true;
						}
	}


	void Apply(const scripting::ClientScriptCommand &command)
	{
		if (!simulation)
			return;
		presentation::ClientScriptHost host{Game(), labels, terrainHeight,
			[this](std::uint64_t sound) {
				if (player)
					player->Stop(sound);
			},
			localScripts ? localScripts->tick : 0, presentationTicks, viewportWidth, viewportHeight, static_cast<std::uint32_t>(LogicTicksPerSecond), speed};
		presentation::ApplyClientScript(Game().World(), settings, host, command);
	}

	// A named unit as the camera sees it (where it is drawn this frame; gone: nullopt, which ends a lock).
	engine::camera::LockTargetProvider LockOn(ecs::Entity unit)
	{
		return [this, unit]() -> std::optional<engine::camera::LockTargetState> {
			for (const presentation::PresentedObject &object : presented)
				if (object.entity == unit)
				{
					engine::camera::LockTargetState state{{object.position[0], object.position[1], object.position[2]}};
					if (Game().FlyingAboveSurface(unit))
						state.airborneOrientation = object.facing;
					return state;
				}
			return std::nullopt;
		};
	}

	void Apply(const scripting::CameraScriptCommand &command)
	{
		if (!simulation)
			return;
		presentation::CameraScriptHost host{Game(), terrainHeight, [this](ecs::Entity unit) { return LockOn(unit); }, presented, cameraSlaved, slaveUnit,
			slaveBone};
		presentation::ApplyCameraScript(camera, Game().World(), host, command);
	}
	// TerrainTracksRenderObjClassSystem::setDetail and init: GameData's MakeTrackMarks and MaxTerrainTracks, and the
	// detail level's GameLOD track limits.
	// GameLOD::applyStaticLODLevel's UseCloudMap (the player's own for the Custom level), shown only by day
	// (BaseHeightMapRenderObjClass::useCloudMap: not TIME_OF_DAY_NIGHT).
	// GameLODManager::applyStaticLODLevel: the detail the level gives (DetailSettings), then what follows from it.
	void ApplyDetail()
	{
		ecs::World &world = Game().World();
		const presentation::DetailSettings detail = presentation::ApplyDetailLevel(content.staticLods, detailLevel, customDetail);
		if (auto *kept = world.FindResource<presentation::DetailSettings>())
			*kept = detail;
		else
			world.EmplaceResource<presentation::DetailSettings>(detail);
		// GlobalData m_maxParticleCount: ParticleSystem::createParticle's cap.
		if (particles)
			particles->SetMaxParticles(detail.maxParticleCount);
		ApplyTrackSettings();
		ApplyCloudSettings();
	}

	// BaseHeightMapRenderObjClass::useCloudMap: the detail's UseCloudMap, shown only by day (not TIME_OF_DAY_NIGHT).
	void ApplyCloudSettings()
	{
		auto *clouds = Game().World().FindResource<presentation::CloudLayer>();
		const auto *detail = Game().World().FindResource<presentation::DetailSettings>();
		if (clouds == nullptr || detail == nullptr)
			return;
		clouds->enabled = detail->useCloudMap && !night;
	}

	// TerrainTracksRenderObjClassSystem::setDetail and init: GameData's MakeTrackMarks and MaxTerrainTracks, and the
	// detail's track limits.
	void ApplyTrackSettings()
	{
		auto *settings = Game().World().FindResource<presentation::TrackSettings>();
		const auto *detail = Game().World().FindResource<presentation::DetailSettings>();
		if (settings == nullptr || detail == nullptr)
			return;
		settings->make = content.gameData.makeTrackMarks;
		settings->maxTracks = content.gameData.maxTerrainTracks;
		if (detailLevel != presentation::detail_level::Custom)
		{
			settings->maxEdges = detail->maxTankTrackEdges;
			settings->maxOpaqueEdges = detail->maxTankTrackOpaqueEdges;
			settings->fadeMilliseconds = detail->maxTankTrackFadeDelay;
		}
	}
};

GameClient::GameClient() : m_state(std::make_unique<State>()) {}
GameClient::~GameClient() = default;

void GameClient::Load(content::ContentLoader &loader, const engine::filesystem::VirtualFileSystem &files, const engine::level::Level &level,
	TerrainHeight terrainHeight, std::array<float, 2> playable, float aspectRatio, std::uint64_t seed)
{
	State &state = *m_state;
	state.content = content::LoadGameContent(loader, engine::time::FixedStep{30});

	state.audio = presentation::LoadAudioContent(loader);
	state.soundFiles = std::make_unique<presentation::SoundFiles>(files, state.audio.settings, engine::audio::AudioOutput::SampleRate);
	state.player = std::make_unique<engine::audio::SoundPlayer>(state.mixer, *state.soundFiles);
	presentation::ConfigurePlayer(*state.player, state.audio.settings);
	state.output = engine::audio::OpenPreferredOutput(state.mixer);

	state.inGameUi = content::BindInGameUi(loader.Load({"Data/INI/Default/InGameUI", "Data/INI/InGameUI"}));
	state.mouse = content::BindMouse(loader.Load({"Data/INI/Mouse"}));
	state.language = content::ReadLanguageFonts(loader.Load({"Data/English/Language"}));
	// GameClient::init: DrawGroupInfo.ini, then Language.ini's DrawGroupInfoFont over it.
	content::BindDrawGroupInfo(loader.Load({"Data/INI/DrawGroupInfo"}), state.drawGroupInfo);
	content::ApplyLanguageFont(state.drawGroupInfo, state.language.drawGroupInfo);
	state.evaCatalog = content::BindEva(loader.Load({"Data/INI/Default/Eva", "Data/INI/Eva"}), engine::time::FixedStep{30});
	state.effects = presentation::LoadEffectsContent(loader);
	state.bridgeArt = presentation::LoadBridgeArt(files, state.content.bridges);
	state.effectJobs = std::make_unique<engine::jobs::JobSystem>(engine::jobs::JobSystemConfig{2});
	state.particles = std::make_unique<engine::effects::ParticleWorld>(
		[&effects = state.effects](std::string_view name) { return effects.particles.Find(name); },
		[&state](float x, float y) { return state.terrainHeight ? state.terrainHeight(x, y) : 0.0f; });

	Start(level, std::move(terrainHeight), playable, aspectRatio, seed);
}

void GameClient::TraceScripts(bool on) { m_state->scriptTrace = on; }

bool GameClient::Start(const engine::level::Level &level, TerrainHeight terrainHeight, std::array<float, 2> playable, float aspectRatio,
	std::uint64_t seed, std::vector<std::string> seats, std::vector<PlayerStart> starts, std::string cameraMarker,
	std::optional<std::uint8_t> soloDifficulty, bool challenge, std::span<const std::byte> checkpoint, std::optional<session::NetworkMatchOptions> network,
	std::int32_t rankPoints, const ReplayStart *replay, bool record, std::optional<session::ScenerySetup> scenery)
{
	State &state = *m_state;
	if (state.simulation)
	{
		// What the last match played and showed goes with it (but the load screen's sounds, LoadScreen's own).
		std::erase_if(state.hostSounds, [&state](engine::audio::SoundHandle sound) { return !state.player->Playing(sound); });
		if (state.hostSounds.empty())
			state.player->StopAll();
		else
			state.player->StopAllExcept(state.hostSounds);
		state.particles = std::make_unique<engine::effects::ParticleWorld>(
			[&effects = state.effects](std::string_view name) { return effects.particles.Find(name); },
			[&state](float x, float y) { return state.terrainHeight ? state.terrainHeight(x, y) : 0.0f; });
		state.simulation.reset();
		state.localScripts.reset();
		presentation::ResetForNewMatch(state.settings);
		state.instances.clear();
		state.playerColors.clear();
		state.playerDayColors.clear();
		state.pinnedLook.reset();
	}
	// GameLogic::startNewGame: the map's map.ini overrides for this match only (GameLogic::reset drops them).
	state.matchContent.reset();
	if (state.mapIni != nullptr && state.mapLoader != nullptr && content::HasMapOverrides(*state.mapIni))
		state.matchContent = content::WithMapOverrides(state.content, *state.mapIni, *state.mapLoader, engine::time::FixedStep{30});
	// Its sounds and particle systems too (the original's audio manager and particle system manager took a map's blocks
	// for good, a leak fixed: the game's own come back with the next match). Nothing plays or flies at this point.
	// Restored in place, so what points at a definition keeps pointing at it; a name the map added stays, unused.
	for (auto &[name, saved] : state.soundsBefore)
		if (auto *sound = state.audio.events.Find(name); sound != nullptr && saved)
			*sound = *saved;
	state.soundsBefore.clear();
	for (auto &[name, saved] : state.particlesBefore)
		if (auto *particle = state.effects.particles.Find(name); particle != nullptr && saved)
			*particle = *saved;
	state.particlesBefore.clear();
	if (state.mapIni != nullptr && state.mapLoader != nullptr)
	{
		engine::config::BindContext bind{state.mapLoader->DiagnosticsFor(*state.mapIni), engine::time::FixedStep{30}};
		bool sounds = false;
		for (const engine::config::Node &root : state.mapIni->Roots())
		{
			const std::string name(root.Value());
			if (root.key == "AudioEvent" || root.key == "DialogEvent" || root.key == "MusicTrack")
			{
				sounds = true;
				if (std::ranges::find(state.soundsBefore, name, &State::SavedSound::first) == state.soundsBefore.end())
				{
					const auto *sound = state.audio.events.Find(name);
					state.soundsBefore.emplace_back(name, sound != nullptr ? std::optional(*sound) : std::nullopt);
				}
			}
			else if (root.key == "ParticleSystem" && std::ranges::find(state.particlesBefore, name, &State::SavedParticle::first) == state.particlesBefore.end())
			{
				const auto *particle = state.effects.particles.Find(name);
				state.particlesBefore.emplace_back(name, particle != nullptr ? std::optional(*particle) : std::nullopt);
			}
		}
		if (sounds)
		{
			const engine::config::Document *one[] = {state.mapIni};
			presentation::BindAudio(one, state.audio, bind);
		}
		if (!state.particlesBefore.empty())
			presentation::BindParticleOverrides(*state.mapIni, state.effects, bind);
	}
	state.speed = state.baseSpeed;
	// GlobalData MaxFieldParticleCount: ParticleSystem::createParticle's limit on ground-aligned AREA_EFFECT particles.
	if (state.particles)
		state.particles->SetMaxFieldParticles(static_cast<std::size_t>((std::max)(state.content.gameData.maxFieldParticleCount, std::int32_t{0})));
	state.terrainHeight = terrainHeight;
	state.camera = engine::camera::RtsCamera({}, std::move(terrainHeight));
	state.camera.SetMapExtent(engine::camera::MapExtent{{0.0f, 0.0f}, {playable[0], playable[1]}});
	state.camera.SetAspectRatio(aspectRatio);
	// GameLogic::startNewGame: the camera marker (a skirmish's local human: its Player_<n>_Start), else
	// "just look somewhere": (50, 50, 0).
	Engine::Math::Vector3 start{50.0f, 50.0f, 0.0f};
	for (const auto &marker : level.markers)
		if (marker.name == cameraMarker)
		{
			start = state.Ground(marker.position);
			break;
		}
	state.camera.LookAt(start);
	state.camera.InitHeightForMap();
	state.camera.SetZoomToDefault();
	state.camera.SetOkToAdjustHeight(true);

	session::SessionOptions options;
	options.seed = seed;
	// A LAN game: this machine's player is its seat's (each machine sees and plays its own).
	options.localSeat = network ? network->seat : replay != nullptr ? replay->seat : 0;
	options.record = record && replay == nullptr;
	// The scenery as given (a match's plan), else by this machine's detail (the shell map).
	if (scenery)
		options.scenery = *scenery;
	else
	{
		const presentation::DetailSettings detail = presentation::ApplyDetailLevel(state.content.staticLods, state.detailLevel, state.customDetail);
		options.scenery = session::ScenerySetupFor(detail.level, detail.useTrees, network.has_value());
	}
	const std::string localPlayer = options.localSeat < seats.size() ? seats[options.localSeat] : std::string{};
	options.seats = std::move(seats);
	options.scriptTrace = state.scriptTrace;
	// A campaign or challenge mission (GAME_SINGLE_PLAYER) at its difficulty.
	options.singlePlayer = soloDifficulty.has_value();
	options.challenge = challenge;
	options.difficulty = soloDifficulty.value_or(1);
	options.rankPointsAtStart = rankPoints;
	for (const PlayerStart &start : starts)
		options.starts.push_back({start.player, start.team, start.playerTemplate, start.startPosition});
	options.cameraMovementFinished = [&state] { return state.CameraMovementFinished(); };
	// GameEngine::isTimeFrozen: a camera move with CAMERA_MOD_FREEZE_TIME holds the game while it lasts; nothing freezes
	// a network game.
	options.timeFreezes = !network.has_value();
	if (!network)
	{
		options.cameraFreezesTime = [&state] { return state.camera.IsTimeFrozen() && !state.CameraMovementFinished(); };
		// evaluateNamedSelected: in the local player's selection (not in a multiplayer game).
		options.selected = [&state](ecs::Entity entity) { return state.simulation && state.Game().World().Side<presentation::Selected>().Get(entity) != nullptr; };
	}
	options.soundLengthTicks = [&state](std::string_view name) -> std::uint64_t {
		const auto *sound = state.audio.Find(name);
		return sound != nullptr && state.soundFiles ? state.soundFiles->LengthTicks(*sound) : 0;
	};
	options.presentationComponents = [](ecs::World &world) {
		presentation::RegisterPresentationComponents(world);
		presentation::RegisterSoundComponents(world);
		world.RegisterComponent<presentation::Selected>();
		world.RegisterComponent<presentation::SuperweaponEvaReady>();
	};
	state.night = level.lighting.current == 3; // the original's TIME_OF_DAY_NIGHT (morning, afternoon, evening, night)
	state.eyes = presentation::PresentationFrame::NoViewer;
	std::uint32_t firstPlayer = presentation::PresentationFrame::NoViewer;
	for (std::uint32_t index = 0; index < level.scenario.participants.size(); ++index)
	{
		const auto &properties = level.scenario.participants[index].properties;
		if (properties.Get<std::string>("playerName").value_or("").empty())
			continue; // neutral
		if (firstPlayer == presentation::PresentationFrame::NoViewer)
			firstPlayer = index;
		if (state.eyes == presentation::PresentationFrame::NoViewer && properties.Get<bool>("playerIsHuman").value_or(false) &&
			((!network && replay == nullptr) || properties.Get<std::string>("playerName").value_or("") == localPlayer))
			state.eyes = index;
	}
	if (state.eyes == presentation::PresentationFrame::NoViewer)
		state.eyes = firstPlayer;
	for (const auto &participant : level.scenario.participants)
	{
		// Its own colour (on a night map its night colour: Drawable::friend_bindToObject's getNightIndicatorColor), else its
		// faction's (PlayerTemplate.ini), else the neutral white.
		auto own = participant.properties.Get<std::int64_t>("playerColor");
		if (const auto nightColor = participant.properties.Get<std::int64_t>("playerNightColor"); state.night && nightColor)
			own = nightColor;
		const auto argb = content::SideColor(own, state.content.factionColors,
			participant.properties.Get<std::string>("playerFaction").value_or(""));
		state.playerColors.push_back({static_cast<float>((argb >> 16) & 0xFF) / 255.0f, static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
			static_cast<float>(argb & 0xFF) / 255.0f, 1.0f});
		state.playerDayColors.push_back(static_cast<std::uint32_t>(content::SideColor(participant.properties.Get<std::int64_t>("playerColor"),
			state.content.factionColors, participant.properties.Get<std::string>("playerFaction").value_or(""))) & 0xFFFFFFu);
	}
	// RecorderClass::isMultiplayer: not a campaign mission nor the shell map; a skirmish, a network game or a replay.
	state.multiplayerGame = !soloDifficulty.has_value() && (!starts.empty() || network.has_value() || replay != nullptr);
	const content::GameContent &matchContent = state.matchContent ? *state.matchContent : state.content;
	state.simulation = replay != nullptr  ? session::MakeReplayMatch(level, matchContent, std::move(options), replay->recording)
		: network                         ? session::MakeNetworkMatch(level, matchContent, std::move(options), *network)
		: checkpoint.empty() ? session::MakeClientMatch(level, matchContent, std::move(options))
							 : session::ResumeClientMatch(level, matchContent, std::move(options), checkpoint);
	if (!state.simulation)
		return false;
	state.multiplayerMatch = network.has_value() && replay == nullptr;
	state.replayMatch = replay != nullptr;
	state.playableExtent = state.Game().PlayableExtent();
	// The map's roads (W3DRoadBuffer::loadRoads), built once for the map.
	state.roads = presentation::BuildRoadGeometry(presentation::BuildRoadNetwork(level.placements, state.content.roads,
		state.content.gameData.maxRoadSegments, state.content.gameData.maxRoadTypes), level.terrain);
	state.BindPresentation();
	// The map's scenery the client alone keeps (GameLogic::startNewGame's trees and props), by the match's rules.
	{
		ecs::World &world = state.Game().World();
		if (auto *scenery = world.FindResource<presentation::Scenery>())
			presentation::BuildScenery(*scenery, level.placements, state.Game(), world.Resource<presentation::LookCatalog>(),
				world.Resource<gameplay::MapSceneryRules>(), world.Resource<engine::gameplay::GroundHeight>(), playable);
	}
	state.TakeSnapshot();
	state.TakeSnapshot();
	return true;
}

const engine::net::CommandRecording *GameClient::Recording() const
{
	return m_state->simulation ? m_state->simulation->Recording() : nullptr;
}

std::optional<session::ClientMatch::Playback> GameClient::PlaybackState() const
{
	return m_state->simulation ? m_state->simulation->PlaybackState() : std::nullopt;
}

const presentation::ShroudCells *GameClient::Shroud()
{
	State &state = *m_state;
	if (!state.simulation)
		return nullptr;
	const auto local = LocalPlayer();
	const auto *map = state.Game().World().FindResource<engine::gameplay::ShroudMap>();
	if (!local || map == nullptr || *local >= map->Players())
		return nullptr;
	const auto &data = state.content.gameData;
	presentation::FillShroudCells(*map, *local, {data.shroudColor, data.clearAlpha, data.fogAlpha, data.shroudAlpha}, state.settings.borderShroudDisabled,
		state.shroud);
	return &state.shroud;
}

void GameClient::ShowMessage(const std::u16string &text)
{
	State &state = *m_state;
	if (!state.simulation)
		return;
	if (auto *messages = state.Game().World().FindResource<presentation::InGameMessages>())
		presentation::AddMessage(*messages, text, state.Game().CurrentTick());
}

void GameClient::ShowMessage(const std::u16string &text, std::uint32_t argb)
{
	State &state = *m_state;
	if (!state.simulation)
		return;
	if (auto *messages = state.Game().World().FindResource<presentation::InGameMessages>())
		presentation::AddMessage(*messages, text, state.Game().CurrentTick(),
			std::array<std::uint8_t, 3>{static_cast<std::uint8_t>((argb >> 16) & 0xFF), static_cast<std::uint8_t>((argb >> 8) & 0xFF),
				static_cast<std::uint8_t>(argb & 0xFF)});
}

std::vector<GameClient::ChatArrived> GameClient::TakeChat()
{
	State &state = *m_state;
	std::vector<ChatArrived> arrived;
	if (!state.simulation)
		return arrived;
	if (auto *inbox = state.Game().World().FindResource<gameplay::ChatInbox>())
	{
		for (gameplay::ChatLine &line : inbox->lines)
			arrived.push_back({line.player, std::move(line.text), line.slots});
		inbox->lines.clear();
	}
	return arrived;
}

void GameClient::SaveClientState(engine::core::serialization::ByteWriter &writer) const
{
	const State &state = *m_state;
	const auto real = [&](float value) { writer.U32(std::bit_cast<std::uint32_t>(value)); };
	const auto &position = state.camera.Position();
	real(position.x);
	real(position.y);
	real(position.z);
	real(state.camera.Angle());
	real(state.camera.Pitch());
	real(state.camera.Zoom());
	presentation::SaveScriptedPresentation(state.settings, writer);
	// The local player's own scripts (MultiplayerScripts.scb and the skirmish human's): their runtime's state (flags,
	// counters, timers, scripts on or off, sequences), its tick and PLAYER_LOST_OBJECT_TYPE's counts seen
	// (GameState's saved ScriptEngine covers every script list).
	const LocalScripts *local = state.localScripts.get();
	writer.Flag(local != nullptr && local->runtime.has_value());
	if (local != nullptr && local->runtime)
	{
		writer.U64(local->tick);
		local->runtime->SaveState(writer);
		writer.U32(static_cast<std::uint32_t>(local->recorded.size()));
		for (const auto &[key, count] : local->recorded)
		{
			writer.Text(key);
			writer.I64(count);
		}
	}
}

bool GameClient::LoadClientState(engine::core::serialization::ByteReader &reader)
{
	State &state = *m_state;
	const auto real = [&] { return std::bit_cast<float>(reader.U32().value_or(0)); };
	const float x = real(), y = real(), z = real();
	const float angle = real(), pitch = real(), zoom = real();
	ClientSettings settings = state.settings;
	if (!presentation::LoadScriptedPresentation(settings, reader))
		return false;
	// The local scripts' state, when the save has it (older saves end before it).
	if (reader.Flag().value_or(false))
	{
		LocalScripts *local = state.localScripts.get();
		const std::uint64_t tick = reader.U64().value_or(0);
		if (local == nullptr || !local->runtime || !local->runtime->LoadState(reader))
			return false;
		local->tick = tick;
		local->recorded.clear();
		for (std::uint32_t count = reader.U32().value_or(0); count > 0 && !reader.Failed(); --count)
		{
			std::string key = reader.Text().value_or("");
			local->recorded[std::move(key)] = reader.I64().value_or(0);
		}
		if (reader.Failed())
			return false;
	}
	state.settings = std::move(settings);
	state.camera.SetPosition({x, y, z});
	state.camera.SetAngle(angle);
	state.camera.SetPitch(pitch);
	state.camera.SetZoom(zoom);
	return true;
}

std::vector<shell::ScorePlayer> GameClient::ScoreBoard() const
{
	const State &state = *m_state;
	std::vector<shell::ScorePlayer> board;
	if (!state.simulation)
		return board;
	const std::vector<session::PlayerScore> scores = state.Game().Scores();
	for (std::size_t player = 0; player < scores.size(); ++player)
	{
		const session::PlayerScore &score = scores[player];
		shell::ScorePlayer &row = board.emplace_back();
		row.name = std::u16string(score.name.begin(), score.name.end());
		row.playerName = score.name;
		for (const auto &[name, shown] : state.playerNames)
			if (name == score.name)
				row.name = shown;
		for (const auto &faction : state.content.playerTemplates.templates)
			if (faction.name == score.playerTemplate)
			{
				row.baseSide = faction.baseSide;
				row.observer = faction.observer;
				row.scoreScreenImage = faction.scoreScreenImage;
				row.sideIconImage = faction.sideIconImage;
				row.scoreScreenMusic = faction.scoreScreenMusic;
			}
		if (player < state.playerColors.size())
		{
			const auto &color = state.playerColors[player];
			const auto channel = [](float value) { return static_cast<std::uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
			row.color = 0xFF000000u | (channel(color[0]) << 16) | (channel(color[1]) << 8) | channel(color[2]);
		}
		row.local = score.local;
		row.listed = score.listed;
		row.relation = score.relation;
		row.unitsBuilt = score.unitsBuilt, row.unitsLost = score.unitsLost, row.unitsDestroyed = score.unitsDestroyed;
		row.buildingsBuilt = score.buildingsBuilt, row.buildingsLost = score.buildingsLost, row.buildingsDestroyed = score.buildingsDestroyed;
		row.moneyEarned = score.moneyEarned;
		row.score = score.score;
	}
	return board;
}

std::vector<std::byte> GameClient::Checkpoint() const { return m_state->simulation ? m_state->simulation->Checkpoint() : std::vector<std::byte>{}; }

void GameClient::SetGameSpeed(float speed) noexcept { m_state->speed = m_state->baseSpeed = std::clamp(speed, 0.05f, 16.0f); }
float GameClient::GameSpeed() const noexcept { return m_state->speed; }

GameClient::MouseTooltipFrame GameClient::MouseTooltips()
{
	State &state = *m_state;
	if (!state.simulation)
		return {};
	ecs::World &world = state.Game().World();
	const auto *fade = world.FindResource<presentation::ScreenFade>();
	return {world.FindResource<presentation::MouseTooltip>(), world.FindResource<presentation::MouseTooltipSettings>(),
		fade != nullptr && fade->kind != presentation::FadeKind::None};
}

std::optional<std::array<std::uint8_t, 2>> GameClient::MouseCursor() const
{
	if (!m_state->simulation)
		return std::nullopt;
	const auto *cursor = m_state->Game().World().FindResource<presentation::CursorState>();
	if (cursor == nullptr)
		return std::nullopt;
	return std::array<std::uint8_t, 2>{static_cast<std::uint8_t>(cursor->cursor), cursor->direction};
}
// A script's time multiplier (View::setTimeMultiplier) runs the logic that many times faster.
double GameClient::TicksPerSecond() const noexcept
{
	return LogicTicksPerSecond * m_state->speed * engine::camera::FastForwardFactor(m_state->camera.TimeMultiplier());
}

void GameClient::Tick()
{
	State &state = *m_state;
	if (!state.simulation->Advance())
		return;
	if (state.simulation->Generation() != state.generation)
	{
		// Restored from a checkpoint: nothing to interpolate from.
		state.generation = state.simulation->Generation();
		state.BindPresentation();
	}
	// Player::update for the local player: on each whole second, its Retaliation option told to the simulation when
	// they differ (a command, as every player action).
	if (const auto &local = state.Game().World().Resource<presentation::LocalPlayer>();
		local.valid && state.Game().CurrentTick() % 30 == 0 && state.Game().RetaliationEnabled(local.player) != state.retaliation)
		state.simulation->Submit(commands::EnableRetaliation{state.retaliation});
	// The named timers' ready flash, a logic frame on.
	if (auto *timers = state.Game().World().FindResource<presentation::NamedTimers>())
		presentation::StepNamedTimerFlash(*timers, [&state](std::string_view name) { return state.Game().ScriptCounter(name); }, state.Game().CurrentTick());
	// TerrainLogic::setActiveBoundary: the camera's area follows (forceCameraAreaConstraintRecalc).
	if (const auto extent = state.Game().PlayableExtent(); extent.x != state.playableExtent.x || extent.y != state.playableExtent.y)
	{
		state.playableExtent = extent;
		state.camera.SetMapExtent(engine::camera::MapExtent{{0.0f, 0.0f}, {Engine::Math::ToFloat(extent.x), Engine::Math::ToFloat(extent.y)}});
	}
	for (const auto &command : state.Game().CameraCommands())
		state.Apply(command);
	for (const auto &command : state.Game().ClientCommands())
	{
		state.Apply(command);
		state.Hear(command);
	}
	// The local player's own scripts, after the tick they answer about (ScriptEngine::update in the logic frame).
	if (state.localScripts && state.localScripts->runtime)
	{
		LocalScripts &local = *state.localScripts;
		local.commands.clear();
		local.runtime->Tick(++local.tick);
		for (const auto &command : local.commands)
		{
			state.Apply(command);
			state.Hear(command);
		}
	}
	state.camera.StepFixed();
	// ScreenMotionBlurFilter's zoom count, once a logic frame from the frame its script ran: at a jump's peak the camera
	// looks at its point (TheTacticalView->lookAt).
	if (auto *filter = state.Game().World().FindResource<presentation::ViewFilter>())
	{
		if (const auto step = presentation::StepMotionBlur(*filter, state.Game().CurrentTick()); step.jumpTo)
			state.camera.LookAt({(*step.jumpTo)[0], (*step.jumpTo)[1], (*step.jumpTo)[2]});
	}
	state.TakeSnapshot();
}

void GameClient::Update(float deltaSeconds, float alpha)
{
	State &state = *m_state;

	// The camera runs on elapsed game time (real time scaled by the game
	// speed; uncapped rendering). A long stall (loading, a breakpoint) is
	// clamped so the camera does not jump.
	engine::camera::CameraFrameTiming timing;
	timing.deltaMilliseconds = std::clamp(deltaSeconds, 0.0f, 0.25f) * 1000.0f * state.speed;
	state.camera.Update(timing);
	// The view's screen filters by the original's drawn frames (the grey fade, a pan blur's end), on real time.
	if (state.simulation)
		if (auto *filter = state.Game().World().FindResource<presentation::ViewFilter>())
			presentation::StepScreenFilterFrames(*filter, std::clamp(deltaSeconds, 0.0f, 0.25f));
	if (state.pinnedLook)
	{
		const auto [x, y] = *state.pinnedLook;
		state.camera.LookAt({x, y, state.terrainHeight ? state.terrainHeight(x, y) : 0.0f});
	}

	// Animations run on game time too (a script's time multiplier running the game that many times faster while the
	// view moves on at its own pace).
	const double gameSeconds = static_cast<double>(std::clamp(deltaSeconds, 0.0f, 0.25f) * state.speed *
		static_cast<float>(engine::camera::FastForwardFactor(state.camera.TimeMultiplier())));
	if (state.simulation)
	{
		ecs::World &world = state.Game().World();
		const float realSeconds = std::clamp(deltaSeconds, 0.0f, 0.25f);
		// CameraShakeSystemClass: the shakers age by the frame's real time and sum their roll at the camera's position.
		if (auto *shakers = world.FindResource<presentation::CameraShakers>())
		{
			presentation::StepCameraShakers(*shakers, realSeconds);
			const auto &eye = state.camera.View().eye;
			shakers->angles = presentation::CameraShaking(*shakers)
				? presentation::ShakerAngles(*shakers, {eye.x, eye.y, eye.z}, world.Resource<presentation::PresentationRandom>().engine)
				: std::array<float, 3>{};
		}
		if (auto *cinematic = world.FindResource<presentation::CinematicText>())
			presentation::AdvanceCinematicText(*cinematic, realSeconds);
		// A slaved camera whose unit is gone (or undrawn) stops riding it.
		state.slaveView.reset();
		if (state.cameraSlaved && !world.IsAlive(state.Game().Named(state.slaveUnit)))
			state.cameraSlaved = false;
	}
	state.clock += gameSeconds;

	// Effects step at the data's 30 frames a second of game time; drawing
	// places particles between frames.
	if (state.particles)
	{
		state.effectClock += gameSeconds * 30.0;
		for (int steps = 0; state.effectClock >= 1.0 && steps < 8; ++steps)
		{
			state.particles->Step(state.effectJobs.get());
			state.effectClock -= 1.0;
		}
		state.effectClock = std::min(state.effectClock, 1.0);
		state.effectAlpha = static_cast<float>(state.effectClock);
	}

	if (state.frameScheduler)
	{
		const auto &view = state.camera.View();
		const Engine::Math::Vector3 right = view.transform.Basis_X();
		state.Game().World().Resource<presentation::ListenerPose>() = {
			{view.eye.x, view.eye.y, view.eye.z}, {view.target.x, view.target.y, view.target.z}, {right.x, right.y, right.z}, true};
		auto &frame = state.Game().World().Resource<presentation::PresentationFrame>();
		frame.seconds = static_cast<float>(gameSeconds);
		frame.realSeconds = std::clamp(deltaSeconds, 0.0f, 0.25f);
		frame.clock = state.clock;
		frame.alpha = std::clamp(alpha, 0.0f, 1.0f);
		frame.frame = static_cast<std::uint32_t>(state.presentationFrames + 1);
		frame.tick = state.Game().CurrentTick();
		frame.drawIconUi = state.settings.drawIconUi;
		if (const auto *fade = state.Game().World().FindResource<presentation::ScreenFade>())
			frame.scriptFade = fade->kind != presentation::FadeKind::None;
		// An observer's player (IsObserver) watches with nobody's eyes: nothing shrouded, everyone's ally (StealthUpdate:
		// "Observer players are friends to everyone!"), as with no seat.
		if (const auto &local = state.Game().World().Resource<presentation::LocalPlayer>(); local.valid && !LocalPlayerObserver())
			frame.viewer = local.player;
		else if (local.valid)
			frame.viewer = presentation::PresentationFrame::NoViewer;
		else
			frame.viewer = state.eyes;
		// EVA speaks in the watcher's side's voice, while the scripts leave it on (setEvaEnabled).
		if (auto *eva = state.Game().World().FindResource<presentation::EvaState>())
		{
			eva->side = frame.viewer != presentation::PresentationFrame::NoViewer ? state.Game().PlayerSide(frame.viewer) : std::string{};
			presentation::SetEvaEnabled(*eva, state.settings.evaEnabled);
		}
		// The player's camera (LookAtTranslator): the wheel zooms; arrow keys and a right-button drag scroll.
		state.LookAround(gameSeconds);
		// GameClient::update: the camera tracking a drawable follows the first one selected (userLookAt), and stops
		// tracking when none is.
		if (state.trackingDrawable)
		{
			const auto selected = Selection();
			const auto *transform = selected.empty() ? nullptr : state.Game().World().Get<engine::gameplay::Transform>(selected.front());
			if (transform != nullptr)
				UserLookAt(Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y));
			else
				state.trackingDrawable = false;
		}
		// The interaction sees the pointer as it is and the camera as it looks this frame.
		{
			auto &interaction = state.Game().World().Resource<presentation::InteractionView>();
			const Engine::Math::Vector3 up = view.transform.Basis_Y(), back = view.transform.Basis_Z();
			interaction = {{view.eye.x, view.eye.y, view.eye.z}, {right.x, right.y, right.z}, {up.x, up.y, up.z}, {-back.x, -back.y, -back.z},
				std::tan(view.horizontalFieldOfView * 0.5f), std::tan(view.horizontalFieldOfView * 0.5f) / std::max(view.aspectRatio, 0.01f),
				state.viewportWidth, state.viewportHeight, true, view.farClip};
			const PointerState &pointer = state.pointer;
			auto &input = state.Game().World().Resource<presentation::PointerInput>();
			input = {pointer.x, pointer.y, pointer.down, pointer.pressed, pointer.released, pointer.shift, pointer.ctrl, pointer.alt, pointer.timeMs,
				pointer.overInterface, state.look.moving, state.look.offsetX, state.look.offsetY};
			input.windowTooltip = pointer.windowTooltip;
			input.overRadar = pointer.overRadar;
			input.hasRadar = pointer.hasRadar;
			input.playback = state.replayMatch;
			state.pointer.pressed = state.pointer.released = 0;
			state.pointer.wheel = 0;
		}
		// What the interaction asks the rules: where the structure being placed may go, what a waiting command may target.
		presentation::QueryPlacementLegality(state.Game().World(), state.Game());
		presentation::QueryTargetingValidity(state.Game().World(), state.Game());
		presentation::QueryQuickPath(state.Game().World(), state.Game());
		state.frameScheduler->Execute(engine::time::SimulationTime{++state.presentationFrames, engine::time::FixedStep{30}});
		// SelectionTranslator::selectFriends hit MaxSelectionSize: GUI:MaxSelectionSize with the cap.
		if (auto &interaction = state.Game().World().Resource<presentation::InteractionState>(); std::exchange(interaction.maxSelectionWarning, false))
			ShowMessage(presentation::BookmarkSetMessage(state.labels ? state.labels("GUI:MaxSelectionSize") : u"GUI:MaxSelectionSize",
				state.Game().World().Resource<presentation::MouseSettings>().maxSelectionSize));
		// The player's orders go to the match (they apply on the tick the relay gives them).
		auto &orders = state.Game().World().Resource<presentation::PlayerOrders>().pending;
		for (const auto &order : orders)
			state.simulation->Submit(order);
		orders.clear();
		// A single-player mission's MUSIC_TRACK_HAS_COMPLETED: the music's progress told the simulation as it changes (a
		// recorded command, so a replay plays it back; never in a network match, whose music scripts run here).
		if (auto *audio = state.Game().World().FindResource<presentation::AudioState>(); audio != nullptr && !state.multiplayerMatch && !state.replayMatch &&
			(audio->reportedMusic != audio->musicName || audio->reportedCompletions != audio->musicCompletions))
		{
			audio->reportedMusic = audio->musicName;
			audio->reportedCompletions = audio->musicCompletions;
			state.simulation->Submit(commands::MusicProgress{audio->musicName, audio->musicCompletions});
		}
	}

	state.instances.clear();
	state.presented.clear();
	if (state.frameScheduler)
	{
		ecs::World &world = state.Game().World();
		world.Resource<presentation::ObjectInstances>().AppendTo(state.instances);
		// The structure being placed, with the objects.
		const auto &ghosts = world.Resource<presentation::PlacementGhosts>().instances;
		state.instances.insert(state.instances.end(), ghosts.begin(), ghosts.end());
		// The rally point flag of the one thing selected (showRallyPoint).
		const auto &markers = world.Resource<presentation::RallyPointMarkers>().instances;
		state.instances.insert(state.instances.end(), markers.begin(), markers.end());
		// Where moves were just ordered (W3DInGameUI::drawMoveHints).
		const auto &hints = world.Resource<presentation::MoveHints>().instances;
		state.instances.insert(state.instances.end(), hints.begin(), hints.end());
		// The waypoint nodes in waypoint mode (W3DWaypointBuffer::drawWaypoints).
		if (const auto *paths = world.FindResource<presentation::WaypointPaths>())
			state.instances.insert(state.instances.end(), paths->nodes.begin(), paths->nodes.end());
		world.Resource<presentation::PresentedObjects>().AppendTo(state.presented);
		// Explosions shake the camera (the original's View::shake: by type, falling off with distance, a random direction).
		auto &shakes = world.Resource<presentation::ShakeRequests>().pending;
		const auto &intensities = state.Game().Content().gameData.shakeIntensity;
		auto &random = world.Resource<presentation::PresentationRandom>().engine;
		for (const presentation::ShakeRequest &shake : shakes)
			state.camera.Shake({shake.at[0], shake.at[1], shake.at[2]}, Engine::Math::ToFloat(intensities[static_cast<std::size_t>(shake.type) % intensities.size()]),
				std::uniform_real_distribution<float>(0.0f, 6.2831853f)(random));
		shakes.clear();
	}


}

std::string GameClient::EffectsSummary() const
{
	State &state = *m_state;
	std::size_t riding = 0;
	if (state.simulation)
	{
		const auto &conditions = state.Game().World().Side<presentation::ConditionEmission>();
		for (std::size_t index = 0; index < conditions.Size(); ++index)
			riding += conditions.Value(index).systems.size();
	}
	std::size_t damageSystems = 0;
	presentation::EffectStats stats;
	if (state.simulation)
	{
		const auto &damage = state.Game().World().Side<presentation::DamageEmission>();
		for (std::size_t index = 0; index < damage.Size(); ++index)
			damageSystems += damage.Value(index).systems.size();
		if (const auto *found = state.Game().World().FindResource<presentation::EffectStats>())
			stats = *found;
	}
	return std::to_string(state.particles ? state.particles->ParticleCount() : 0) + " particles, " + std::to_string(riding) + " condition systems, " +
		std::to_string(damageSystems) + " damage systems (" + std::to_string(stats.hurt) + " damage state changes, " +
		std::to_string(stats.hurtWithEffects) + " with effects), " + std::to_string(stats.fxPlayed) + " FX played, " +
		std::to_string(Scorches() != nullptr ? Scorches()->marks.size() : 0) + " scorch marks, " + std::to_string(Tracks().size()) + " tracks";
}

void GameClient::Point(const PointerState &pointer) noexcept
{
	PointerState &state = m_state->pointer;
	const std::uint8_t pressed = state.pressed | pointer.pressed, released = state.released | pointer.released;
	const float wheel = state.wheel + pointer.wheel;
	state = pointer;
	state.pressed = pressed;
	state.released = released;
	state.wheel = wheel;
}

void GameClient::SetViewport(float width, float height) noexcept
{
	m_state->viewportWidth = std::max(width, 1.0f);
	m_state->viewportHeight = std::max(height, 1.0f);
}

InGameOverlay GameClient::Overlay() const
{
	State &state = *m_state;
	if (!state.simulation || !state.frameScheduler)
		return {};
	InGameOverlay overlay = presentation::ExtractInGameOverlay(state.Game().World(), state.Game(),
		{&state.inGameUi, &state.content.animations2d, state.settings.drawIconUi, state.settings.specialPowerDisplayDisabled, state.camera.Zoom(),
			state.labels ? state.labels("CONTROLBAR:UnderConstructionDesc") : std::u16string{}, &state.drawGroupInfo});
	// DrawRMBScrollAnchor: the anchor while a right-button scroll runs.
	if (state.inGameUi.drawRmbScrollAnchor && state.look.scrolling)
	{
		overlay.rmbAnchorShown = true;
		overlay.rmbAnchor = {static_cast<int>(state.look.anchorX), static_cast<int>(state.look.anchorY)};
	}
	return overlay;
}

void GameClient::SetAddCashText(std::u16string pattern)
{
	State &state = *m_state;
	state.addCashText = pattern;
	if (state.simulation)
		if (auto *settings = state.Game().World().FindResource<presentation::FloatingTextSettings>())
			settings->addCash = std::move(pattern);
}

void GameClient::SetLabels(std::function<std::u16string(std::string_view)> labels) { m_state->labels = std::move(labels); }

void GameClient::SetLoseCashText(std::u16string pattern)
{
	State &state = *m_state;
	state.loseCashText = pattern;
	if (state.simulation)
		if (auto *settings = state.Game().World().FindResource<presentation::FloatingTextSettings>())
			settings->loseCash = std::move(pattern);
}

void GameClient::SignalUiInteraction(std::string_view hook) { m_state->simulation->Submit(commands::SignalUi{std::string(hook)}); }

session::SessionView *GameClient::View() noexcept { return m_state->simulation ? &m_state->Game() : nullptr; }

std::vector<ecs::Entity> GameClient::Selection() const
{
	std::vector<ecs::Entity> selected;
	if (!m_state->simulation)
		return selected;
	ecs::World &world = const_cast<State &>(*m_state).Game().World();
	for (const ecs::Entity entity : world.Side<presentation::Selected>().Entities())
		if (world.IsAlive(entity))
			selected.push_back(entity);
	return selected;
}

engine::audio::Mixer &GameClient::AudioMixer() noexcept { return m_state->mixer; }

std::vector<std::string> GameClient::TakeMovies() { return std::exchange(m_state->settings.movies, {}); }

std::int32_t GameClient::LocalSkillPoints() const
{
	const auto player = LocalPlayer();
	if (!player || !m_state->simulation)
		return 0;
	const auto *ranks = const_cast<State &>(*m_state).Game().World().FindResource<engine::gameplay::PlayerRanks>();
	const engine::gameplay::PlayerRank *rank = ranks != nullptr ? ranks->Find(*player) : nullptr;
	return rank != nullptr ? rank->skillPoints : 0;
}

std::optional<std::uint32_t> GameClient::LocalPlayer() const
{
	if (!m_state->simulation)
		return std::nullopt;
	const auto *local = const_cast<State &>(*m_state).Game().World().FindResource<presentation::LocalPlayer>();
	return local != nullptr && local->valid ? std::optional(local->player) : std::nullopt;
}

bool GameClient::LocalPlayerObserver() const
{
	const auto player = LocalPlayer();
	if (!player)
		return true;
	const State &state = *m_state;
	const std::string name = const_cast<State &>(state).Game().PlayerTemplateName(*player);
	const content::PlayerTemplateInfo *faction = state.content.playerTemplates.Find(name);
	return faction != nullptr && faction->observer;
}

void GameClient::SelectOnly(const std::vector<ecs::Entity> &entities)
{
	State &state = *m_state;
	if (!state.simulation)
		return;
	auto &world = state.Game().World();
	auto &selected = world.Side<presentation::Selected>();
	selected.Clear();
	for (const ecs::Entity entity : entities)
		if (world.IsAlive(entity) && selected.Get(entity) == nullptr)
			selected.Emplace(entity);
}

void GameClient::BeginTargeting(ecs::Entity source, std::string_view name, std::string_view shortcutType)
{
	State &state = *m_state;
	if (!state.simulation)
		return;
	const content::CommandButtonContent *button = state.content.commands.Button(name);
	if (button == nullptr)
		return;
	auto &targeting = state.Game().World().Resource<presentation::GuiTargeting>();
	targeting = presentation::GuiTargeting{};
	targeting.active = true;
	targeting.source = source;
	targeting.power = button->specialPower;
	targeting.options = button->options;
	targeting.shortcutType = std::string(shortcutType);
	if (button->commandName == "SET_RALLY_POINT")
		targeting.kind = presentation::GuiCommandKind::RallyPoint;
	if (button->commandName == "ATTACK_MOVE")
		targeting.kind = presentation::GuiCommandKind::AttackMove;
	if (button->commandName == "PLACE_BEACON")
		targeting.kind = presentation::GuiCommandKind::PlaceBeacon;
	if (button->commandName == "COMBATDROP")
		targeting.kind = presentation::GuiCommandKind::CombatDrop;
	if (button->commandName == "FIRE_WEAPON")
	{
		targeting.kind = presentation::GuiCommandKind::FireWeapon;
		targeting.weaponSlot = button->weaponSlot;
		targeting.maxShots = button->maxShotsToFire;
	}
	if (button->commandName.starts_with("GUARD"))
	{
		targeting.kind = presentation::GuiCommandKind::Guard;
		targeting.guardMode = button->commandName == "GUARD_WITHOUT_PURSUIT" ? 1 : button->commandName == "GUARD_FLYING_UNITS_ONLY" ? 2 : 0;
	}
	if (const auto power = state.content.powers.Template(button->specialPower))
	{
		targeting.powerType = state.content.powers.templates[*power].type;
		targeting.powerCursorRadius = state.content.powers.templates[*power].radiusCursorRadius;
	}
	// setGUICommand's setRadiusCursor: the button's RadiusCursorType, its weapon slot for a weapon's radius.
	targeting.radiusCursor = button->radiusCursor;
	targeting.weaponSlot = button->weaponSlot;
	// Mouse::getCursorIndex (case-insensitive); not a cursor: CROSS (the INI's Target).
	const auto cursorNamed = [](std::string_view cursor) {
		for (std::size_t index = 0; index < content::MouseCursorNames.size(); ++index)
			if (std::ranges::equal(content::MouseCursorNames[index], cursor,
					[](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }))
				return static_cast<content::MouseCursorKind>(index);
		return content::MouseCursorKind::Target;
	};
	targeting.cursor = cursorNamed(button->cursorName);
	targeting.invalidCursor = cursorNamed(button->invalidCursorName);
}

void GameClient::BeginPlacement(ecs::Entity builder, std::string_view structure, std::string_view specialPower, std::uint32_t options)
{
	State &state = *m_state;
	if (!state.simulation)
		return;
	auto &placement = state.Game().World().Resource<presentation::BuildPlacement>();
	const auto definition = state.Game().DefinitionIndex(structure);
	const content::ObjectDefinition *what = state.Game().ObjectNamed(structure);
	if (!definition || what == nullptr)
		return;
	placement = presentation::BuildPlacement{};
	placement.active = true;
	placement.structure = std::string(structure);
	placement.definition = *definition;
	placement.builder = builder;
	placement.specialPower = std::string(specialPower);
	placement.options = options;
	// ThingTemplate PlacementViewAngle (degrees).
	placement.facing = Engine::Math::ToFloat(what->placementViewAngleDegrees) * std::numbers::pi_v<float> / 180.0f;
}

namespace
{
// VictoryConditions' local answers, the local player's side, and the music, for the local player's scripts.
class LocalMatch final : public scripting::LocalMatchHost
{
public:
	LocalMatch(GameClient &client, std::map<std::string, std::int64_t> &recorded) : m_client(client), m_recorded(recorded) {}

	bool AlliedVictory() const override
	{
		const auto *outcome = Outcome();
		const auto player = m_client.LocalPlayer();
		return outcome != nullptr && player && outcome->HasWon(*player);
	}
	bool AlliedDefeat() const override
	{
		const auto *outcome = Outcome();
		if (outcome == nullptr)
			return false;
		// VictoryConditions::isLocalAlliedDefeat: an observer (a local player not among the match's: cachePlayerPtrs), once
		// it is over.
		const auto player = m_client.LocalPlayer();
		return player && outcome->Of(*player) != nullptr ? outcome->HasLost(*player) : outcome->singleAllianceRemaining;
	}
	bool LocalDefeat() const override
	{
		const auto *outcome = Outcome();
		const auto player = m_client.LocalPlayer();
		return outcome != nullptr && player && outcome->Eliminated(*player);
	}
	std::string SideOf(const std::string &name) const override
	{
		session::SessionView *view = m_client.View();
		if (view == nullptr)
			return {};
		if (name == "<Local Player>")
		{
			const auto player = m_client.LocalPlayer();
			return player ? view->PlayerSide(*player) : std::string{};
		}
		const auto *roster = view->World().FindResource<engine::gameplay::TeamRoster>();
		const auto player = roster != nullptr ? roster->FindPlayer(name) : std::nullopt;
		return player ? view->PlayerSide(*player) : std::string{};
	}
	bool MusicCompleted(const std::string &track, std::int64_t times) const override
	{
		session::SessionView *view = m_client.View();
		const auto *audio = view != nullptr ? view->World().FindResource<presentation::AudioState>() : nullptr;
		return audio != nullptr && audio->musicName == track && static_cast<std::int64_t>(audio->musicCompletions) >= times;
	}
	std::int64_t CountObjects(const std::string &name, const std::string &types, bool ignoreDead) const override
	{
		session::SessionView *view = m_client.View();
		const auto player = PlayerOf(name);
		return view != nullptr && player ? view->CountPlayerObjects(*player, types, ignoreDead) : 0;
	}
	std::int64_t RecordedCount(const std::string &name, const std::string &types) const override
	{
		const auto found = m_recorded.find(name + '\n' + types);
		return found == m_recorded.end() ? 0 : found->second;
	}
	void RecordCount(const std::string &name, const std::string &types, std::int64_t count) override { m_recorded[name + '\n' + types] = count; }

private:
	// A player by name, or the local player for "<Local Player>".
	std::optional<std::uint32_t> PlayerOf(const std::string &name) const
	{
		if (name == "<Local Player>")
			return m_client.LocalPlayer();
		session::SessionView *view = m_client.View();
		const auto *roster = view != nullptr ? view->World().FindResource<engine::gameplay::TeamRoster>() : nullptr;
		return roster != nullptr ? roster->FindPlayer(name) : std::nullopt;
	}
	std::map<std::string, std::int64_t> &m_recorded; // PLAYER_LOST_OBJECT_TYPE's counts seen, on this machine (LocalScripts)

	const engine::gameplay::MatchOutcome *Outcome() const
	{
		session::SessionView *view = m_client.View();
		return view != nullptr ? view->World().FindResource<engine::gameplay::MatchOutcome>() : nullptr;
	}

	GameClient &m_client;
};
}

void GameClient::SetPlayerNames(std::vector<std::pair<std::string, std::u16string>> names, std::u16string defeatedText)
{
	m_state->playerNames = std::move(names);
	if (!defeatedText.empty())
		m_state->defeatedText = std::move(defeatedText);
}

void GameClient::UseMatchScripts(std::vector<engine::level::ScriptList> lists)
{
	State &state = *m_state;
	if (lists.empty())
		return;
	auto local = std::make_unique<LocalScripts>();
	for (engine::level::ScriptList &scripts : lists)
		local->scenario.participants.emplace_back().scripts = std::move(scripts);
	local->host = std::make_unique<LocalMatch>(*this, local->recorded);
	scripting::AddCoreVocabulary(local->vocabulary);
	scripting::UseLegacyCallReading(local->vocabulary, generalszh::content::KindOfNames);
	scripting::AddPresentationVocabulary(local->vocabulary, local->commands);
	scripting::AddMatchVocabulary(local->vocabulary, local->host.get());
	local->runtime.emplace(local->scenario, local->vocabulary, engine::scripting::ScriptHooks{},
		engine::scripting::ScriptRuntimeOptions{static_cast<std::uint32_t>(LogicTicksPerSecond), 1});
	state.localScripts = std::move(local);
}

// CommandXlat's MSG_META_PLACE_BEACON: in a network match (not a replay), with the local player still playing and fewer
// than MaxBeaconsPerPlayer of its beacons up, Command_PlaceBeacon waits for its spot.
void GameClient::PlaceBeaconKey()
{
	State &state = *m_state;
	if (!state.simulation || !state.multiplayerMatch || state.replayMatch)
		return;
	auto &world = state.Game().World();
	const auto *local = world.FindResource<presentation::LocalPlayer>();
	const auto *rules = world.FindResource<gameplay::BeaconRules>();
	if (local == nullptr || !local->valid || rules == nullptr)
		return;
	if (const auto *outcome = world.FindResource<engine::gameplay::MatchOutcome>())
		if (const auto *standing = outcome->Of(local->player); standing != nullptr && standing->defeated)
			return;
	const std::string_view beacon = rules->Of(local->player);
	std::int32_t count = 0;
	ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Owner>> query(world);
	query.ForEachChunk([&](auto chunk) {
		const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
		const auto owners = chunk.template Get<engine::gameplay::Owner>();
		for (std::size_t row = 0; row < definitions.size(); ++row)
			count += owners[row].player == local->player && !beacon.empty() && state.Game().Definition(definitions[row].index).name == beacon ? 1 : 0;
	});
	if (count < rules->maxPerPlayer)
		BeginTargeting({}, "Command_PlaceBeacon");
}

const content::DrawGroupInfoContent &GameClient::GroupNumberLook() const { return m_state->drawGroupInfo; }

void GameClient::TeamKey(presentation::TeamMeta meta, std::int32_t group)
{
	State &state = *m_state;
	if (!state.simulation)
		return;
	const presentation::TeamMetaOutcome outcome = presentation::ApplyTeamMeta(state.Game().World(), meta, group, state.Game().CurrentTick());
	// A replay takes no orders of the one watching it.
	if (outcome.command && !state.replayMatch)
		state.simulation->Submit(*outcome.command);
	if (outcome.select && !state.replayMatch)
		state.simulation->Submit(*outcome.select);
	if (outcome.lookAt)
		state.camera.LookAt({(*outcome.lookAt)[0], (*outcome.lookAt)[1], (*outcome.lookAt)[2]});
}

// CommandXlat's MSG_META_REMOVE_BEACON: in a network match (not a replay), MSG_REMOVE_BEACON over the selection.
void GameClient::RemoveBeaconKey()
{
	State &state = *m_state;
	if (!state.simulation || !state.multiplayerMatch || state.replayMatch)
		return;
	const auto entities = state.Game().World().Side<presentation::Selected>().Entities();
	state.simulation->Submit(commands::RemoveBeacon{std::vector<ecs::Entity>(entities.begin(), entities.end())});
}

void GameClient::CueVoice(const presentation::UnitVoiceCue &cue)
{
	if (m_state->simulation)
		if (auto *cues = m_state->Game().World().FindResource<presentation::UnitVoiceCues>())
			cues->pending.push_back(cue);
}

void GameClient::Submit(const commands::GameCommand &command)
{
	if (m_state->simulation)
		m_state->simulation->Submit(command);
}

void GameClient::PlayInterfaceSound(std::string_view event)
{
	State &state = *m_state;
	if (auto *commands = state.simulation ? state.Game().World().FindResource<presentation::AudioCommands>() : nullptr)
		commands->pending.push_back({presentation::AudioCommand::Kind::Interface, std::string(event)});
}

void GameClient::PlayInterfaceVoice(std::string_view event)
{
	State &state = *m_state;
	if (auto *commands = state.simulation ? state.Game().World().FindResource<presentation::AudioCommands>() : nullptr)
		commands->pending.push_back({presentation::AudioCommand::Kind::Interface, std::string(event), true});
}

std::uint64_t GameClient::PlayHostSound(std::string_view event)
{
	State &state = *m_state;
	if (!state.player || event.empty())
		return 0;
	const auto *sound = state.audio.Find(event);
	if (sound == nullptr)
		return 0;
	std::erase_if(state.hostSounds, [&state](engine::audio::SoundHandle handle) { return !state.player->Playing(handle); });
	const engine::audio::SoundHandle handle = state.player->Play(*sound);
	if (handle != 0)
		state.hostSounds.push_back(handle);
	return handle;
}

void GameClient::StopHostSound(std::uint64_t sound, bool fade)
{
	State &state = *m_state;
	if (!state.player || sound == 0)
		return;
	if (fade)
		state.player->FadeOut(sound, presentation::FadeMixerFrames(state.audio.settings, state.mixer.SampleRate()));
	else
		state.player->Stop(sound, false);
	std::erase(state.hostSounds, static_cast<engine::audio::SoundHandle>(sound));
}

void GameClient::UpdateHostSounds()
{
	State &state = *m_state;
	if (state.player)
		state.player->Update();
}

void GameClient::FadeOutMusicNow()
{
	State &state = *m_state;
	auto *sound = state.simulation ? state.Game().World().FindResource<presentation::AudioState>() : nullptr;
	if (sound == nullptr || !state.player || sound->music == 0)
		return;
	state.player->FadeOut(sound->music, presentation::FadeMixerFrames(state.audio.settings, state.mixer.SampleRate()));
	sound->music = 0;
	sound->musicName.clear();
}

void GameClient::PlayMenuMusic(std::string_view track)
{
	State &state = *m_state;
	auto *commands = state.simulation ? state.Game().World().FindResource<presentation::AudioCommands>() : nullptr;
	const auto *sound = state.simulation ? state.Game().World().FindResource<presentation::AudioState>() : nullptr;
	if (commands == nullptr || sound == nullptr)
		return;
	state.musicBeforeMenu = sound->musicName;
	commands->pending.push_back({presentation::AudioCommand::Kind::MusicTrack, std::string(track), true});
}

void GameClient::RestoreMusic()
{
	State &state = *m_state;
	auto *commands = state.simulation ? state.Game().World().FindResource<presentation::AudioCommands>() : nullptr;
	if (commands == nullptr || state.musicBeforeMenu.empty())
		return;
	commands->pending.push_back({presentation::AudioCommand::Kind::MusicTrack, state.musicBeforeMenu, true});
	state.musicBeforeMenu.clear();
}

void GameClient::SetRetaliation(bool enabled) noexcept { m_state->retaliation = enabled; }

void GameClient::SetDetail(std::int32_t level, const presentation::CustomDetail &custom)
{
	State &state = *m_state;
	state.detailLevel = level;
	state.customDetail = custom;
	if (state.simulation)
		state.ApplyDetail();
}

const presentation::DetailSettings *GameClient::Detail() const
{
	State &state = *m_state;
	return state.simulation ? state.Game().World().FindResource<presentation::DetailSettings>() : nullptr;
}

const presentation::CloudLayer *GameClient::Clouds() const
{
	State &state = *m_state;
	return state.simulation ? state.Game().World().FindResource<presentation::CloudLayer>() : nullptr;
}

std::vector<presentation::TrackView> GameClient::Tracks() const
{
	State &state = *m_state;
	std::vector<presentation::TrackView> views;
	if (!state.simulation)
		return views;
	auto &world = state.Game().World();
	const auto *settings = world.FindResource<presentation::TrackSettings>();
	const auto *catalog = world.FindResource<presentation::LookCatalog>();
	if (settings == nullptr || catalog == nullptr)
		return views;
	const std::uint32_t maxEdges = std::clamp<std::uint32_t>(settings->maxEdges, 1u, static_cast<std::uint32_t>(presentation::MaxTrackEdges));
	world.Side<presentation::TrackMarks>().ForEach([&](ecs::Entity, presentation::TrackMarks &track) {
		if (track.count < 2 || track.texture >= catalog->trackTextures.size())
			return;
		presentation::TrackView view{catalog->trackTextures[track.texture], {}, {}, {}, {}, maxEdges, settings->maxOpaqueEdges};
		view.left.reserve(track.count);
		view.right.reserve(track.count);
		view.v.reserve(track.count);
		view.alpha.reserve(track.count);
		std::uint32_t index = track.bottom;
		for (std::uint32_t i = 0; i < track.count; ++i, ++index)
		{
			if (index >= maxEdges)
				index = 0;
			view.left.push_back(track.left[index]);
			view.right.push_back(track.right[index]);
			view.v.push_back(track.v[index]);
			view.alpha.push_back(track.alpha[index]);
		}
		views.push_back(std::move(view));
	});
	return views;
}

void GameClient::SetScrollFactor(int percent)
{
	// OptionPreferences::getScrollFactor (EA's): clamped to 0..100, over 100.
	m_state->scrollFactor = static_cast<float>(std::clamp(percent, 0, 100)) / 100.0f;
}

void GameClient::SetUserVolumes(int music, int sound2D, int sound3D, int speech)
{
	State &state = *m_state;
	auto *commands = state.simulation ? state.Game().World().FindResource<presentation::AudioCommands>() : nullptr;
	if (commands == nullptr)
		return;
	using Kind = presentation::AudioCommand::Kind;
	commands->pending.push_back({Kind::UserMusic, {}, false, static_cast<float>(music) / 100.0f});
	commands->pending.push_back({Kind::UserSound, {}, false, static_cast<float>(sound2D) / 100.0f});
	commands->pending.push_back({Kind::UserSound3D, {}, false, static_cast<float>(sound3D) / 100.0f});
	commands->pending.push_back({Kind::UserSpeech, {}, false, static_cast<float>(speech) / 100.0f});
}

std::array<int, 5> GameClient::DefaultVolumes() const
{
	const auto &settings = m_state->audio.settings;
	const auto percent = [](float share) { return static_cast<int>(share * 100.0f + 0.5f); };
	return {percent(settings.defaultMusicVolume), percent(settings.defaultSoundVolume), percent(settings.default3DSoundVolume),
		percent(settings.defaultSpeechVolume), percent(settings.relative2DVolume)};
}

std::string GameClient::AudioSummary() const
{
	State &state = *m_state;
	const auto *sound = state.simulation ? state.Game().World().FindResource<presentation::AudioState>() : nullptr;
	if (sound == nullptr || !state.player)
		return "no audio";
	std::size_t loops = 0;
	const auto &table = state.Game().World().Side<presentation::SoundLoops>();
	for (std::size_t index = 0; index < table.Size(); ++index)
	{
		const auto &object = table.Value(index);
		loops += (object.ambient != 0) + (object.move != 0) + (object.burning != 0) + (object.crashing != 0);
	}
	return "music '" + sound->musicName + "', " + std::to_string(state.player->PlayingCount()) + " sounds playing, " + std::to_string(loops) +
		" object loops, " + std::to_string(state.soundFiles->CachedSounds()) + " sounds decoded";
}

const engine::effects::ParticleWorld *GameClient::Particles() const noexcept { return m_state->particles.get(); }
void GameClient::SetFieldParticleCount(std::size_t count) noexcept
{
	if (m_state->particles)
		m_state->particles->SetFieldParticleCount(count);
}
float GameClient::ParticleAlpha() const noexcept { return m_state->effectAlpha; }

std::vector<presentation::ShownLight> GameClient::Lights() const
{
	State &state = *m_state;
	if (const auto *lights = state.simulation ? state.Game().World().FindResource<presentation::DynamicLights>() : nullptr)
		return lights->shown;
	return {};
}

const presentation::RadiusCursor *GameClient::CursorDecal() const
{
	State &state = *m_state;
	return state.simulation ? state.Game().World().FindResource<presentation::RadiusCursor>() : nullptr;
}

const presentation::Tracers *GameClient::TracerEffects() const
{
	State &state = *m_state;
	return state.simulation ? state.Game().World().FindResource<presentation::Tracers>() : nullptr;
}

void GameClient::UseMapObjects(const engine::config::Document *mapIni, content::ContentLoader *loader)
{
	m_state->mapIni = mapIni;
	m_state->mapLoader = loader;
}

void GameClient::SetWeather(const presentation::WeatherSetting &weather)
{
	State &state = *m_state;
	state.weather = weather;
	if (state.simulation)
		if (auto *snow = state.Game().World().FindResource<presentation::SnowField>())
			presentation::ApplyWeatherSetting(*snow, weather);
}

const presentation::SnowField *GameClient::Snow() const
{
	State &state = *m_state;
	return state.simulation ? state.Game().World().FindResource<presentation::SnowField>() : nullptr;
}

bool GameClient::WeatherShown() const { return m_state->settings.weatherShown; }

presentation::ViewFilter *GameClient::ScreenFilter()
{
	State &state = *m_state;
	return state.simulation ? state.Game().World().FindResource<presentation::ViewFilter>() : nullptr;
}

const presentation::BridgeViews *GameClient::Bridges() const
{
	State &state = *m_state;
	return state.simulation ? state.Game().World().FindResource<presentation::BridgeViews>() : nullptr;
}

const presentation::RoadGeometry *GameClient::Roads() const
{
	State &state = *m_state;
	return state.simulation ? state.Game().World().FindResource<presentation::RoadGeometry>() : nullptr;
}

const presentation::BridgeArt *GameClient::BridgeModels() const
{
	State &state = *m_state;
	return state.simulation ? state.Game().World().FindResource<presentation::BridgeArt>() : nullptr;
}

const presentation::RadiusDecalViews *GameClient::RadiusDecals() const
{
	State &state = *m_state;
	return state.simulation ? state.Game().World().FindResource<presentation::RadiusDecalViews>() : nullptr;
}

std::vector<std::pair<std::string, presentation::ShadowDecalPlacement>> GameClient::ShadowDecals() const
{
	State &state = *m_state;
	std::vector<std::pair<std::string, presentation::ShadowDecalPlacement>> decals;
	if (!state.simulation)
		return decals;
	const auto *catalog = state.Game().World().FindResource<presentation::LookCatalog>();
	if (catalog == nullptr)
		return decals;
	for (const presentation::ObjectInstance &instance : state.instances)
	{
		if (instance.shadowDecal >= catalog->shadowDecals.size())
			continue;
		const presentation::ShadowDecalLook &look = catalog->shadowDecals[instance.shadowDecal];
		// (A template with no size takes its model's box: not known here yet, so none is laid; see the ledger.)
		if (look.sizeX == 0.0f || look.sizeY == 0.0f)
			continue;
		const auto &w = instance.world;
		decals.emplace_back(look.texture, presentation::PlaceShadowDecal(look, {w[3], w[7]}, {w[0], w[4]}, {w[1], w[5]}, {0.0f, 0.0f}));
	}
	return decals;
}

const presentation::ScorchMarks *GameClient::Scorches() const
{
	State &state = *m_state;
	return state.simulation ? state.Game().World().FindResource<presentation::ScorchMarks>() : nullptr;
}

std::vector<presentation::BeamSegment> GameClient::Lasers() const
{
	State &state = *m_state;
	std::vector<presentation::BeamSegment> beams;
	if (const auto *frame = state.simulation ? state.Game().World().FindResource<presentation::LaserFrame>() : nullptr)
		for (const auto &beam : frame->beams)
			beams.push_back({beam.start, beam.end, beam.width, beam.color, beam.texture, beam.uvScale, beam.uvOffset});
	// W3DRopeDraw's lines, blended.
	if (const auto *ropes = state.simulation ? state.Game().World().FindResource<presentation::RopeViews>() : nullptr)
		for (std::size_t row = 0; row < ropes->Size(); ++row)
			beams.push_back({ropes->starts[row], ropes->ends[row], ropes->widths[row], ropes->colors[row], {}, 1.0f, 0.0f, false});
	// W3DWaypointBuffer's lines in waypoint mode: added, never hidden by what is drawn.
	if (const auto *paths = state.simulation ? state.Game().World().FindResource<presentation::WaypointPaths>() : nullptr)
		for (const auto &segment : paths->segments)
			beams.push_back({segment.start, segment.end, segment.width, segment.color, segment.texture, segment.uvScale, segment.uvOffset, true, false});
	return beams;
}

std::string_view GameClient::AudioOutputName() const noexcept
{
	return m_state->output ? m_state->output->Name() : std::string_view("none");
}

void GameClient::PinCamera(float x, float y) noexcept { m_state->pinnedLook = std::pair{x, y}; }

// View::getScreenCornerWorldPointsAtZ: where the view's corners (upper left, upper right, lower right, lower left)
// meet the plane at `z`; none when a corner's ray does not reach it.
std::optional<std::array<std::array<float, 2>, 4>> GameClient::ViewCornersAtZ(float z) const
{
	const auto &view = m_state->camera.View();
	const Engine::Math::Vector3 right = view.transform.Basis_X(), up = view.transform.Basis_Y(), back = view.transform.Basis_Z();
	const float tanX = std::tan(view.horizontalFieldOfView * 0.5f), tanY = tanX / std::max(view.aspectRatio, 0.01f);
	std::array<std::array<float, 2>, 4> corners{};
	const std::array<std::array<float, 2>, 4> screen{{{-1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, -1.0f}, {-1.0f, -1.0f}}};
	for (std::size_t index = 0; index < 4; ++index)
	{
		const float sx = screen[index][0] * tanX, sy = screen[index][1] * tanY;
		const float dx = -back.x + right.x * sx + up.x * sy, dy = -back.y + right.y * sx + up.y * sy, dz = -back.z + right.z * sx + up.z * sy;
		if (dz == 0.0f)
			return std::nullopt;
		const float t = (z - view.eye.z) / dz;
		if (t <= 0.0f)
			return std::nullopt;
		corners[index] = {view.eye.x + dx * t, view.eye.y + dy * t};
	}
	return corners;
}

// The meta-events (CommandMap.ini, MetaEventTranslator) the game client answers: CommandXlat's camera keys, reset and
// tracking, the last radar event and STOP (MSG_DO_STOP over the selection), and LookAtTranslator's view bookmarks.
bool GameClient::MetaEvent(std::string_view meta)
{
	State &state = *m_state;
	if (!state.simulation)
		return false;
	if (presentation::ApplyCameraKeyMeta(state.cameraKeys, meta))
		return true;
	// CommandTranslator's MSG_META_ALL_CHEER (Ctrl+C): only in a network game (isInMultiplayerGame), AllCheerSound and
	// MSG_DO_CHEER for the selection; otherwise the key goes on unanswered.
	if (meta == "ALL_CHEER")
	{
		if (!state.multiplayerMatch || state.replayMatch)
			return false;
		state.simulation->Submit(presentation::AllCheer(state.Game().World()));
		return true;
	}
	if (meta == "CAMERA_RESET")
	{
		// InGameUI::resetCamera: W3DView::resetCamera at the view's own position, over 1 ms, no easing.
		state.camera.ResetCamera(state.camera.Position(), 1, 0.0f, 0.0f);
		return true;
	}
	if (meta == "TOGGLE_CAMERA_TRACKING_DRAWABLE")
	{
		state.trackingDrawable = true; // setCameraTrackingDrawable(true): it stays on until nothing is selected
		return true;
	}
	if (const auto bookmark = presentation::ParseBookmarkMeta(meta))
	{
		presentation::ViewLocation &slot = state.bookmarks.slots[static_cast<std::size_t>(bookmark->slot - 1)];
		if (bookmark->save)
		{
			// View::getLocation: the position, angle, pitch and zoom; GUI:BookmarkXSet with the slot.
			const auto &at = state.camera.Position();
			slot = {true, {at.x, at.y, at.z}, state.camera.Angle(), state.camera.Pitch(), state.camera.Zoom()};
			ShowMessage(presentation::BookmarkSetMessage(state.labels ? state.labels("GUI:BookmarkXSet") : u"GUI:BookmarkXSet", bookmark->slot));
		}
		else if (slot.valid)
		{
			// View::setLocation: position, angle, pitch and zoom set back, redrawn.
			state.camera.SetPosition({slot.position[0], slot.position[1], slot.position[2]});
			state.camera.SetAngle(slot.angle);
			state.camera.SetPitch(slot.pitch);
			state.camera.SetZoom(slot.zoom);
			state.camera.ForceRedraw();
		}
		return true;
	}
	if (meta == "VIEW_LAST_RADAR_EVENT")
	{
		// Radar::getLastEventLoc, then TheTacticalView->lookAt (no radar needed).
		if (const auto *radar = state.Game().World().FindResource<presentation::RadarEvents>(); radar != nullptr && radar->last)
		{
			const auto &world = radar->events[*radar->last].world;
			state.camera.LookAt({world[0], world[1], world[2]});
		}
		return true;
	}
	const auto *local = state.Game().World().FindResource<presentation::LocalPlayer>();
	const auto positionOf = [&](ecs::Entity entity) -> std::optional<Engine::Math::Vector3> {
		const auto *transform = state.Game().World().IsAlive(entity) ? state.Game().World().Get<engine::gameplay::Transform>(entity) : nullptr;
		if (transform == nullptr)
			return std::nullopt;
		return Engine::Math::Vector3{Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y),
			Engine::Math::ToFloat(transform->position.z)};
	};
	if (meta == "VIEW_COMMAND_CENTER")
	{
		// viewCommandCenter: TheTacticalView->lookAt its command center, else its costliest structure; nothing without one.
		if (local != nullptr && local->valid)
			if (const auto place = state.Game().CommandCenterToView(local->player))
				if (const auto at = positionOf(*place))
					state.camera.LookAt(*at);
		return true;
	}
	if (meta == "SELECT_HERO")
	{
		// iNeedAHero; inside something, its container; the selection becomes it alone and the view looks at it.
		if (local != nullptr && local->valid)
			if (auto hero = state.Game().HeroToSelect(local->player))
			{
				if (const auto *passenger = state.Game().World().Get<engine::gameplay::Passenger>(*hero))
					hero = passenger->transport;
				if (const auto at = positionOf(*hero))
				{
					SelectOnly({*hero});
					state.camera.LookAt(*at);
					// MSG_CREATE_SELECTED_GROUP: its select voice.
					if (auto *cues = state.Game().World().FindResource<presentation::UnitVoiceCues>())
						cues->pending.push_back(presentation::UnitVoiceCue{presentation::VoiceOrder::CreateGroup});
				}
			}
		return true;
	}
	// CommandXlat's selection keys over the drawable list (each object newest first: ObjectId highest first).
	std::optional<presentation::StepKey> step;
	if (meta == "SELECT_NEXT_UNIT")
		step = presentation::StepKey::NextUnit;
	else if (meta == "SELECT_PREV_UNIT")
		step = presentation::StepKey::PrevUnit;
	else if (meta == "SELECT_NEXT_WORKER")
		step = presentation::StepKey::NextWorker;
	else if (meta == "SELECT_PREV_WORKER")
		step = presentation::StepKey::PrevWorker;
	const bool selectAll = meta == "SELECT_ALL", selectAircraft = meta == "SELECT_ALL_AIRCRAFT", matching = meta == "SELECT_MATCHING_UNITS";
	if (step || selectAll || selectAircraft || matching)
	{
		if (local == nullptr || !local->valid)
			return true;
		ecs::World &world = state.Game().World();
		const auto &view = world.Resource<presentation::InteractionView>();
		auto &selected = world.Side<presentation::Selected>();
		const std::uint64_t unselectable = std::uint64_t{1} << content::ObjectStatusBit("UNSELECTABLE");
		const std::uint64_t carBomb = std::uint64_t{1} << content::ObjectStatusBit("IS_CARBOMB");
		std::vector<std::pair<std::uint32_t, presentation::KeyDrawable>> drawn;
		ecs::Query<ecs::Read<engine::gameplay::ObjectId>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Owner>,
			ecs::Read<engine::gameplay::Transform>>
			query(world);
		query.ForEachChunk([&](auto chunk) {
			const auto entities = chunk.Entities();
			const auto ids = chunk.template Get<engine::gameplay::ObjectId>();
			const auto refs = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto owners = chunk.template Get<engine::gameplay::Owner>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			for (std::size_t row = 0; row < entities.size(); ++row)
			{
				const ecs::Entity entity = entities[row];
				const content::ObjectDefinition &definition = state.Game().Definition(refs[row].index);
				presentation::KeyDrawable d;
				d.entity = entity;
				// ThingTemplate::isEquivalentTo: the same template, or of the same reskin family.
				d.equivalence = definition.reskinnedFrom.empty() ? refs[row].index
																 : state.Game().DefinitionIndex(definition.reskinnedFrom).value_or(refs[row].index);
				d.local = owners[row].player == local->player;
				d.offMap = world.Get<engine::gameplay::OffMap>(entity) != nullptr;
				d.contained = d.offMap || world.Get<engine::gameplay::Passenger>(entity) != nullptr;
				const auto *health = world.Get<engine::gameplay::Health>(entity);
				d.dead = (health != nullptr && engine::gameplay::IsDead(*health)) || world.Get<engine::gameplay::Dying>(entity) != nullptr;
				const auto *off = world.Get<engine::gameplay::Disabled>(entity);
				d.mobile = !definition.Is("IMMOBILE") && (off == nullptr || off->mask == 0);
				d.selected = selected.Get(entity) != nullptr;
				const auto *status = world.Get<engine::gameplay::StatusFlags>(entity);
				const auto *slave = world.Get<engine::gameplay::Slaved>(entity);
				// Object::isSelectable: ALWAYS_SELECTABLE; else SELECTABLE, not UNSELECTABLE (an enslaved drone, a structure
				// being sold), alive.
				d.selectable = definition.Is("ALWAYS_SELECTABLE") ||
					(definition.Is("SELECTABLE") && (status == nullptr || (status->bits & unselectable) == 0) && (slave == nullptr || slave->enslaved == 0) &&
						world.Get<engine::gameplay::Sale>(entity) == nullptr && !d.dead);
				d.carBomb = status != nullptr && (status->bits & carBomb) != 0;
				d.structure = definition.Is("STRUCTURE");
				d.aircraft = definition.Is("AIRCRAFT");
				d.dozer = definition.Is("DOZER");
				d.harvester = definition.Is("HARVESTER");
				d.ignoresSelectAll = definition.Is("IGNORES_SELECT_ALL");
				d.noSelect = definition.Is("NO_SELECT");
				d.position = {Engine::Math::ToFloat(transforms[row].position.x), Engine::Math::ToFloat(transforms[row].position.y),
					Engine::Math::ToFloat(transforms[row].position.z)};
				float sx = 0.0f, sy = 0.0f;
				d.onScreen = view.valid && view.Project(d.position[0], d.position[1], d.position[2], sx, sy) && sx >= 0.0f && sx <= view.width &&
					sy >= 0.0f && sy <= view.height;
				drawn.push_back({ids[row].value, d});
			}
		});
		std::ranges::sort(drawn, [](const auto &a, const auto &b) { return a.first > b.first; });
		std::vector<presentation::KeyDrawable> list;
		list.reserve(drawn.size());
		for (auto &[id, d] : drawn)
			list.push_back(std::move(d));
		const auto entities = selected.Entities();
		const ecs::Entity first = entities.empty() ? ecs::Entity{} : entities.back();
		const int maxSelect = state.inGameUi.maxSelectionSize;
		const presentation::KeySelection outcome = step ? presentation::StepSelectKey(list, *step, entities.size(), first)
			: matching									 ? presentation::SelectMatchingKey(list, maxSelect, first)
														 : presentation::SelectAllKey(list, selectAircraft, maxSelect, first);
		if (outcome.deselectAll)
			selected.Clear();
		for (const ecs::Entity entity : outcome.select)
			if (world.IsAlive(entity) && selected.Get(entity) == nullptr)
				selected.Emplace(entity);
		if (outcome.lookAt)
			state.camera.LookAt({(*outcome.lookAt)[0], (*outcome.lookAt)[1], (*outcome.lookAt)[2]});
		for (const presentation::KeyMessage &message : outcome.messages)
		{
			std::u16string text = state.labels ? state.labels(message.label) : std::u16string(message.label.begin(), message.label.end());
			if (message.number >= 0)
				text = presentation::BookmarkSetMessage(std::move(text), message.number); // UnicodeString::format's %d
			ShowMessage(text);
		}
		if (outcome.createGroup && !outcome.select.empty())
			if (auto *cues = world.FindResource<presentation::UnitVoiceCues>())
				cues->pending.push_back(presentation::UnitVoiceCue{presentation::VoiceOrder::CreateGroup});
		// SELECT_NEXT_WORKER from nothing selected: the worker's own VoiceSelect at it, as well.
		if (outcome.unitVoice && !outcome.select.empty())
			if (auto *sounds = world.FindResource<presentation::SoundRequests>())
				if (const auto *reference = world.Get<engine::gameplay::DefinitionRef>(outcome.select.front()))
					if (const auto voice = state.Game().Definition(reference->index).Sound("VoiceSelect"); !voice.empty())
					{
						presentation::SoundRequest request{std::string(voice)};
						request.at = outcome.lookAt.value_or(std::array<float, 3>{});
						request.owner = local->player;
						sounds->pending.push_back(std::move(request));
					}
		return true;
	}
	if (meta == "STOP")
	{
		// MSG_DO_STOP: the selected group stops.
		const auto selected = Selection();
		state.simulation->Submit(commands::Stop{selected});
		return true;
	}
	if (meta == "SCATTER")
	{
		// MSG_DO_SCATTER: the selected group scatters.
		state.simulation->Submit(commands::Scatter{Selection()});
		return true;
	}
	if (meta == "CREATE_FORMATION")
	{
		// CommandTranslator's MSG_META_CREATE_FORMATION -> MSG_CREATE_FORMATION: the selected group makes (or breaks) a
		// formation (AIGroup::groupCreateFormation).
		const auto selected = Selection();
		state.simulation->Submit(commands::CreateFormation{selected});
		return true;
	}
	return false;
}

// View::userLookAt: the player moves the camera there.
void GameClient::UserLookAt(float x, float y)
{
	State &state = *m_state;
	state.camera.BeginUserAction();
	state.camera.LookAt({x, y, state.terrainHeight ? state.terrainHeight(x, y) : 0.0f});
}

void GameClient::ApplyView(Graphics::CameraState &camera) const
{
	const auto &view = m_state->camera.View();
	// W3DView::getCameraTransform: the look, turned by the camera shakers' roll about x, y and z; a slaved camera is its
	// bone's transform instead.
	Engine::Math::AffineTransform3 transform = view.transform;
	if (m_state->slaveView)
		transform.elements = *m_state->slaveView;
	else if (const auto *shakers = m_state->simulation ? m_state->Game().World().FindResource<presentation::CameraShakers>() : nullptr;
			 shakers != nullptr && presentation::CameraShaking(*shakers))
	{
		transform = Compose(transform, Engine::Math::AffineTransform3::Rotation_X(shakers->angles[0]));
		transform = Compose(transform, Engine::Math::AffineTransform3::Rotation_Y(shakers->angles[1]));
		transform = Compose(transform, Engine::Math::AffineTransform3::Rotation_Z(shakers->angles[2]));
	}
	camera.Set_Transform(Graphics::Import_Affine_Transform(transform));
	// Aspect before the view plane, as the original's setWidth.
	camera.Set_Aspect_Ratio(view.aspectRatio);
	camera.Set_View_Plane(view.horizontalFieldOfView, -1.0f);
	camera.Set_Clip_Planes(view.nearClip, view.farClip);
}

std::array<float, 3> GameClient::Eye() const
{
	const auto &eye = m_state->camera.View().eye;
	return {eye.x, eye.y, eye.z};
}

std::array<float, 3> GameClient::LookAtPosition() const
{
	const auto &at = m_state->camera.Position();
	return {at.x, at.y, at.z};
}

std::array<float, 3> GameClient::PreviousLookAtPosition() const
{
	const auto at = m_state->camera.PreviousLookAt();
	return {at.x, at.y, at.z};
}

std::span<const ObjectInstance> GameClient::Objects() const noexcept { return m_state->instances; }

presentation::ModelLibrary *GameClient::Models() const noexcept
{
	return m_state->simulation ? m_state->Game().World().FindResource<presentation::ModelLibrary>() : nullptr;
}

void GameClient::WaitForModels()
{
	if (m_state->simulation)
		presentation::WaitForInstanceModels(m_state->Game().World(), m_state->instances);
}

const ClientSettings &GameClient::Settings() const noexcept { return m_state->settings; }

std::optional<SlavedCamera> GameClient::CameraSlave() const
{
	const State &state = *m_state;
	if (!state.cameraSlaved || !state.simulation)
		return std::nullopt;
	const ecs::Entity unit = state.Game().Named(state.slaveUnit);
	for (const presentation::PresentedObject &object : state.presented)
		if (object.entity == unit)
			return SlavedCamera{object.look, object.animationSeconds, object.animationStart, object.world, state.slaveBone};
	return std::nullopt;
}

void GameClient::SlaveView(const std::array<float, 12> &bone)
{
	State &state = *m_state;
	state.slaveView = bone;
	// View::setPosition2D: the camera's pivot follows the bone.
	const auto &position = state.camera.Position();
	const std::array<float, 2> at = presentation::SlavePosition(bone);
	state.camera.SetPosition({at[0], at[1], position.z});
}

float GameClient::InfantryLightScale(std::uint32_t timeOfDay) const noexcept
{
	const auto &scales = m_state->content.gameData.infantryLightScale;
	const std::array<float, 4> values{Engine::Math::ToFloat(scales[0]), Engine::Math::ToFloat(scales[1]), Engine::Math::ToFloat(scales[2]),
		Engine::Math::ToFloat(scales[3])};
	return presentation::InfantryLightScale(m_state->settings.infantryLightOverride, values, timeOfDay);
}

const presentation::CinematicText *GameClient::CinematicText() const
{
	const auto *cinematic = m_state->simulation ? m_state->Game().World().FindResource<presentation::CinematicText>() : nullptr;
	return cinematic != nullptr && presentation::CinematicTextShown(*cinematic) ? cinematic : nullptr;
}

const presentation::PopupMessage *GameClient::Popup() const
{
	const auto *popup = m_state->simulation ? m_state->Game().World().FindResource<presentation::PopupMessage>() : nullptr;
	return popup != nullptr && popup->shown ? popup : nullptr;
}

bool GameClient::ClosePopup()
{
	auto *popup = m_state->simulation ? m_state->Game().World().FindResource<presentation::PopupMessage>() : nullptr;
	return popup != nullptr && presentation::ClosePopupMessage(*popup);
}

const content::PlayerTemplates &GameClient::PlayerTemplates() const noexcept { return m_state->content.playerTemplates; }
const content::MultiplayerSettings &GameClient::MultiplayerSettings() const noexcept { return m_state->content.multiplayer; }
std::string GameClient::UnportedSummary() const { return m_state->simulation ? m_state->Game().UnportedSummary() : std::string{}; }
std::size_t GameClient::EntityCount() const { return m_state->simulation ? m_state->Game().EntityCount() : 0; }
std::size_t GameClient::WorkerCount() const { return m_state->simulation ? m_state->Game().WorkerCount() : 0; }
}
