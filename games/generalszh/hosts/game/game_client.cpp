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

import engine.camera.model.rts_camera;
import engine.time.simulation_time;
import games.generalszh.session.session_view;
import games.generalszh.hud.superweapon_timers;
import games.generalszh.presentation.hud.systems.eva_system;
import games.generalszh.presentation.hud.systems.production_presentation_system;
import games.generalszh.presentation.hud.systems.overcharge_notice_system;
import games.generalszh.presentation.hud.systems.rally_notice_system;
import games.generalszh.presentation.hud.systems.radar_event_system;
import games.generalszh.content.eva.eva_content;
import games.generalszh.scripting.core_vocabulary;
import games.generalszh.scripting.presentation_vocabulary;
import games.generalszh.scripting.match_vocabulary;
import games.generalszh.presentation.hud.systems.in_game_message_system;
import games.generalszh.presentation.hud.systems.military_caption_system;
import games.generalszh.presentation.hud.systems.screen_fade_system;
import games.generalszh.presentation.objects.systems.script_flash_systems;
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
import games.generalszh.presentation.interaction.systems.interaction_systems;
import games.generalszh.presentation.interaction.systems.build_placement_system;
import games.generalszh.presentation.objects.systems.placement_ghost_system;
import games.generalszh.presentation.objects.systems.disable_presentation_systems;
import games.generalszh.presentation.interaction.algorithms.selection_setup;
import games.generalszh.content.stealth.stealth_content;
import games.generalszh.content.fire.fire_content;
import games.generalszh.content.death.death_content;
import games.generalszh.content.topple.topple_content;
import games.generalszh.presentation.rendering.model_bones;
import engine.jobs.job_system;
import games.generalszh.content.loading.game_content;
import games.generalszh.content.objects.model_draw;
import games.generalszh.content.locomotors.locomotor_catalog;
import games.generalszh.presentation.animation.tread_roll;
import games.generalszh.content.objects.model_states;
import games.generalszh.content.objects.model_conditions;
import Engine.Core.Math.FixedPresentation;
import Engine.Core.Math.Vector3;
import Graphics.Scene.AffineTransform;

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
};

struct GameClient::State
{
	TerrainHeight terrainHeight;
	std::u16string addCashText{u"$%d"}; // GUI:AddCash
	std::u16string loseCashText{u"-$%d"}; // GUI:LoseCash
	std::optional<std::pair<float, float>> pinnedLook;
	content::GameContent content;
	// Single player is a match of one through an in-process relay.
	std::unique_ptr<session::ClientMatch> simulation;
	std::unique_ptr<LocalScripts> localScripts;
	std::uint64_t generation{0};
	engine::camera::RtsCamera camera;
	float speed{1.0f};
	float baseSpeed{1.0f}; // the player's own speed (a script's SET_FPS_LIMIT lasts only its match)
	ClientSettings settings;
	std::string detailLevel{"High"}; // the options' StaticGameLOD
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
	bool scriptTrace{false}; // matches write each script action run to stderr
	std::string musicBeforeMenu; // what played before a menu's music


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
	presentation::MotionSampleSystem motionSample;
	presentation::PoseSampleSystem poseSample;
	presentation::DetectorPingSystem detectorPings;
	presentation::HitFxSystem hitFx;
	presentation::MissileIgnitionSystem missileIgnitions;
	presentation::HarvestPresentationSystem harvestPresentation;
	presentation::CratePresentationSystem cratePresentation;
	presentation::CashPresentationSystem cashPresentation;
	presentation::PromotionPresentationSystem promotionPresentation;
	presentation::ChassisSystem chassis;
	presentation::TreadRollSystem treadRoll;
	presentation::WheelRollSystem wheelRoll;
	presentation::MotionEmitterSystem motionEmitters;
	presentation::ObjectPresentationSystem objectPresentation;
	presentation::EffectAttachmentSystem effectAttachments;
	presentation::DamageEffectSystem damageEffects;
	presentation::FireFxPlacementSystem fireFxPlacement;
	presentation::ExhaustSampleSystem exhaustSample;
	presentation::ExhaustSystem exhausts;
	presentation::LaserSystem lasers;
	presentation::FxPlaybackSystem fxPlayback;
	presentation::DynamicLightSystem dynamicLights;
	presentation::TrackLayingSystem trackLaying;
	presentation::TrackFadeSystem trackFade;
	presentation::TreeBreezeSystem treeBreeze;
	presentation::TreeBendSystem treeBend;
	presentation::DebrisAnimationSystem debrisAnimation;
	presentation::TreeContactSystem treeContacts;
	presentation::SoundLoopSystem soundLoops;
	presentation::AudioMixSystem audioMix;
	presentation::PointerInteractionSystem pointerInteraction;
	presentation::InGameMessageSystem inGameMessages;
	presentation::MilitaryCaptionSystem militaryCaption;
	presentation::ScreenFadeSystem screenFade;
	presentation::ScriptFlashSystem scriptFlash;
	presentation::EvaSystem eva;
	presentation::ProductionPresentationSystem productionPresentation;
	presentation::OverchargeNoticeSystem overchargeNotices;
	presentation::RallyNoticeSystem rallyNotices;
	presentation::DisabledSoundSystem disabledSounds;
	presentation::EmpSparkSystem empSparks;
	presentation::AutoDepositPresentationSystem autoDepositTexts;
	presentation::AbilityFeedbackSystem abilityFeedback;
	presentation::TintStatusSystem tintStatus;
	presentation::RiderTintSystem riderTint;
	presentation::ObjectIconSystem objectIcons;
	presentation::UplinkStatusSystem uplinkStatus;
	presentation::AttachedParticleClearSystem particleClears;
	presentation::UplinkEffectSystem uplinkEffects;
	presentation::UplinkSoundSystem uplinkSounds;
	presentation::FireLoopSystem fireLoops;
	presentation::MountedDrawSystem mountedDraw;
	presentation::RadarEventSystem radarEvents;
	std::function<std::u16string(std::string_view)> labels; // the game's strings (GameText::fetch)
	content::EvaCatalog evaCatalog; // Eva.ini
	content::InGameUiContent inGameUi;
	content::MouseContent mouse;
	content::LanguageFonts language;
	Engine::Math::FixedVector2 playableExtent{}; // the active boundary the camera keeps within
	hud::SuperweaponFlash superweaponFlash; // the ready countdowns' flash (InGameUI's, kept across frames)
	std::vector<std::pair<std::string, std::u16string>> playerNames;
	std::u16string defeatedText{u"%ls has been defeated."};
	presentation::BuildPlacementSystem buildPlacement;
	presentation::PlacementGhostSystem placementGhosts;
	// The player's pointer since the last frame (presses and releases gather until the interaction reads them).
	PointerState pointer;
	float viewportWidth{800}, viewportHeight{600};
	std::uint64_t presentationTicks{0};
	std::uint64_t presentationFrames{0};

	void BindPresentation()
	{
		ecs::World &world = Game().World();
		world.EmplaceResource<presentation::PresentationFrame>();
		presentation::KnowMotionLooks(world.EmplaceResource<presentation::MotionLooks>(), Game());
		presentation::LookCatalog &catalog = world.EmplaceResource<presentation::LookCatalog>();
		catalog.playerColors = playerColors;
		catalog.night = night && content.gameData.forceModelsToFollowTimeOfDay;
		presentation::KnowLooks(catalog, Game());
		world.EmplaceResource<presentation::LookClips>();
		world.EmplaceResource<presentation::ObjectInstances>();
		world.EmplaceResource<presentation::MountedInstances>();
		world.EmplaceResource<presentation::PresentedObjects>();
		world.EmplaceResource<presentation::ParticleWorldHandle>(presentation::ParticleWorldHandle{particles.get(), &effects});
		// Where models' bones sit, from the renderer's loaded models (effects ride on them).
		world.EmplaceResource<presentation::BonePoses>(presentation::BonePoses{[this](std::string_view model, std::string_view bone) {
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
			[this](std::string_view model, std::string_view bone, std::string_view ancestor) { return bones.Descends(model, bone, ancestor); }});
		world.EmplaceResource<presentation::FxRequests>();
		world.EmplaceResource<presentation::FloatingTexts>();
		world.EmplaceResource<presentation::WorldAnimations>();
		world.EmplaceResource<presentation::Breeze>();
		world.EmplaceResource<presentation::TreeBreeze>();
		world.EmplaceResource<presentation::FloatingTextSettings>().addCash = addCashText;
		world.Resource<presentation::FloatingTextSettings>().loseCash = loseCashText;
		world.EmplaceResource<presentation::WeaponExhausts>();
		world.EmplaceResource<presentation::WeaponFireLoops>();
		world.EmplaceResource<presentation::WeaponLasers>();
		world.EmplaceResource<presentation::LaserRequests>();
		world.EmplaceResource<presentation::ActiveLasers>();
		world.EmplaceResource<presentation::LaserFrame>();
		world.EmplaceResource<presentation::SoundRequests>();
		world.EmplaceResource<presentation::ShakeRequests>();
		world.EmplaceResource<presentation::LightPulses>();
		world.EmplaceResource<presentation::DynamicLights>();
		world.EmplaceResource<presentation::ScorchMarks>();
		world.EmplaceResource<presentation::TrackSettings>();
		ApplyTrackSettings();
		world.EmplaceResource<presentation::PresentationRandom>();
		world.EmplaceResource<presentation::EffectStats>();
		world.EmplaceResource<presentation::AudioHandle>(presentation::AudioHandle{&audio, player.get(), &mixer});
		world.EmplaceResource<presentation::AudioState>();
		world.EmplaceResource<presentation::AudioCommands>();
		world.EmplaceResource<presentation::EvaState>().catalog = evaCatalog;
		world.EmplaceResource<presentation::RadarEvents>();
		{
			// Object::getRadarPriority / Radar::isPriorityVisible by definition; the damage that never warns; the words and sounds.
			auto &radar = world.EmplaceResource<presentation::RadarFeedback>();
			for (std::size_t index = 0; index < Game().DefinitionCount(); ++index)
			{
				const content::ObjectDefinition &kind = Game().Definition(static_cast<std::uint32_t>(index));
				bool shown = kind.radarPriority != "NOT_ON_RADAR";
				if (kind.radarPriority.empty() || kind.radarPriority == "INVALID")
					shown = kind.Is("CAPTURABLE") ||
						std::any_of(kind.modules.begin(), kind.modules.end(), [](const content::ModuleEntry &module) { return module.type == "GarrisonContain"; });
				radar.onRadar.push_back(shown);
			}
			radar.penaltyDamage = content::DamageTypeIndex("PENALTY").value_or(0xFFFFFFFFu);
			radar.healingDamage = content::DamageTypeIndex("HEALING").value_or(0xFFFFFFFFu);
			if (labels)
			{
				radar.underAttack = labels("RADAR:UnderAttack");
				radar.unitUnderAttack = labels("RADAR:UnitUnderAttack");
				radar.harvesterUnderAttack = labels("RADAR:HarvesterUnderAttack");
				radar.structureUnderAttack = labels("RADAR:StructureUnderAttack");
				radar.infiltration = labels("RADAR:Infiltration");
				// BattlePlanUpdate's message labels, by definition.
				for (std::size_t index = 0; index < Game().DefinitionCount(); ++index)
				{
					std::array<std::u16string, 3> messages;
					for (const content::ModuleEntry &module : Game().Definition(static_cast<std::uint32_t>(index)).modules)
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
				const auto found = content.miscAudio.find(field);
				return found != content.miscAudio.end() ? found->second : std::string{};
			};
			radar.harvesterSound = misc("RadarNotifyHarvesterUnderAttackSound");
			radar.structureSound = misc("RadarNotifyStructureUnderAttackSound");
			radar.infiltrationSound = misc("RadarNotifyInfiltrationSound");
		}
		world.EmplaceResource<presentation::ListenerPose>();
		// The player's interaction: pointer, view, selection and orders (the local seat's player).
		world.EmplaceResource<presentation::PointerInput>();
		world.EmplaceResource<presentation::InteractionView>();
		world.EmplaceResource<presentation::InteractionState>();
		{
			auto &settings = world.EmplaceResource<presentation::MouseSettings>();
			settings.dragTolerance = static_cast<float>(mouse.dragTolerance);
			settings.dragTolerance3D = static_cast<float>(mouse.dragTolerance3D);
			settings.dragToleranceMs = mouse.dragToleranceMs;
			for (std::size_t kind = 0; kind < settings.cursorDirections.size(); ++kind)
				settings.cursorDirections[kind] = mouse.cursors[kind].directions;
		}
		world.EmplaceResource<presentation::SelectionBox>();
		world.EmplaceResource<presentation::CursorState>();
		world.EmplaceResource<presentation::GuiTargeting>();
		world.EmplaceResource<presentation::PlayerOrders>();
		world.EmplaceResource<presentation::BuildPlacement>();
		{
			// InGameUI's messages: its colours and delay (MessageDelayMS / 30 / 1000, whole numbers), the players' names.
			auto &messages = world.EmplaceResource<presentation::InGameMessages>();
			messages.color1 = inGameUi.messageColor1;
			messages.color2 = inGameUi.messageColor2;
			messages.timeoutTicks = static_cast<std::uint64_t>(std::max<std::int64_t>(inGameUi.messageDelayMs / 30 / 1000, 0));
			messages.defeatedText = defeatedText;
			if (labels)
			{
				messages.upgradeCompleteText = labels("UPGRADE:UpgradeComplete");
				messages.overchargeExhaustedText = labels("GUI:OverchargeExhausted");
				messages.rallySetText = labels("GUI:RallyPointSet");
				messages.rallyNoPathText = labels("GUI:RallyPointNoPath");
				for (const auto &[name, object] : content.objects)
					if (!object.displayName.empty() && !messages.displayNames.contains(object.displayName))
						messages.displayNames.emplace(object.displayName, labels(object.displayName));
				for (const content::UpgradeContent &upgrade : content.upgrades.upgrades)
					messages.upgradeNames.push_back(upgrade.displayName.empty() ? std::u16string{} : labels(upgrade.displayName));
			}
			if (const auto *roster = world.FindResource<engine::gameplay::TeamRoster>())
				for (const auto &[name, shown] : playerNames)
					if (const auto player = roster->FindPlayer(name))
					{
						if (messages.playerNames.size() <= *player)
							messages.playerNames.resize(*player + 1);
						messages.playerNames[*player] = shown;
					}
		}
		{
			// The military caption's colour (InGameUI.ini) and typing (Language.ini: letters MilitaryCaptionSpeed ticks
			// apart, MilitaryCaptionDelayMS before the first and each line, in whole ticks).
			// InGameUI's named timers: their flash every NamedTimerCountdownFlashDuration frames (whole ticks).
			world.EmplaceResource<presentation::NamedTimers>().flashTicks =
				static_cast<std::uint64_t>(std::max<std::int64_t>(inGameUi.namedTimerFlashFrames.Floor(), 0));
			// ScriptEngine::newMap: every map starts on a fade in from black.
			world.EmplaceResource<presentation::ScreenFade>(presentation::StartFade());
			auto &caption = world.EmplaceResource<presentation::MilitaryCaption>();
			caption.baseColor = inGameUi.militaryCaptionColor;
			caption.speedTicks = static_cast<std::uint64_t>(std::max(language.militaryCaptionSpeed, 0));
			caption.delayTicks = static_cast<std::uint64_t>(std::max(30 * language.militaryCaptionDelayMs / 1000, 0));
		}
		world.EmplaceResource<presentation::PlacementGhosts>().opacity = Engine::Math::ToFloat(Game().Content().gameData.objectPlacementOpacity);
		presentation::KnowSelectables(world.EmplaceResource<presentation::SelectionCatalog>(), Game());
		if (const auto seat = Game().SeatPlayer(Game().LocalSeat()))
			world.EmplaceResource<presentation::LocalPlayer>(presentation::LocalPlayer{*seat, true});
		else
			world.EmplaceResource<presentation::LocalPlayer>();
		// A thing that goes stops its sounds.
		world.Side<presentation::SoundLoops>().OnRemove([sounds = player.get()](ecs::Entity, presentation::SoundLoops &loops) {
			for (const auto handle : {loops.ambient, loops.move, loops.burning, loops.crashing, loops.turret})
				if (handle != 0 && sounds != nullptr)
					sounds->Stop(handle);
		});
		world.EmplaceResource<presentation::TerrainHeightHandle>(presentation::TerrainHeightHandle{[this](float x, float y) {
			return terrainHeight ? terrainHeight(x, y) : 0.0f;
		}});
		world.Side<presentation::DamageEmission>().OnRemove([emitters = particles.get()](ecs::Entity, presentation::DamageEmission &emission) {
			for (const auto &attached : emission.systems)
				emitters->Stop(attached.id);
		});
		// An object that goes takes its riding effects at once.
		world.Side<presentation::ConditionEmission>().OnRemove([emitters = particles.get()](ecs::Entity, presentation::ConditionEmission &emission) {
			for (const auto &attached : emission.systems)
				emitters->Destroy(attached.id);
		});
		world.Side<presentation::FxEmission>().OnRemove([emitters = particles.get()](ecs::Entity, presentation::FxEmission &riding) {
			for (const auto &attached : riding.systems)
				emitters->Destroy(attached.id);
		});
		world.Side<presentation::CrashTrailEmission>().OnRemove([emitters = particles.get()](ecs::Entity, presentation::CrashTrailEmission &trail) {
			for (const auto &attached : trail.systems)
				emitters->Destroy(attached.id);
		});
		// An uplink that goes takes its effects and sounds (killEverything).
		world.Side<presentation::UplinkEffects>().OnRemove([emitters = particles.get()](ecs::Entity, presentation::UplinkEffects &effects) {
			for (const auto system : effects.systems)
				emitters->Destroy(system);
			for (const auto system : effects.orbitSystems)
				if (system != 0)
					emitters->Destroy(system);
		});
		world.Side<presentation::FireSoundLoop>().OnRemove([sounds = player.get()](ecs::Entity, presentation::FireSoundLoop &loop) {
			if (loop.handle != 0 && sounds != nullptr)
				sounds->Stop(loop.handle);
		});
		world.Side<presentation::UplinkSounds>().OnRemove([sounds = player.get()](ecs::Entity, presentation::UplinkSounds &loops) {
			for (const auto handle : loops.handles)
				if (handle != 0 && sounds != nullptr)
					sounds->Stop(handle);
		});
		// A vehicle that goes stops its emitters; what is out lives on.
		world.Side<presentation::MotionEmission>().OnRemove([emitters = particles.get()](ecs::Entity, presentation::MotionEmission &emission) {
			for (std::uint32_t index = 0; index < emission.count && emitters != nullptr; ++index)
				emitters->Stop(emission.systems[index]);
		});
		tickScheduler.reset();
		frameScheduler.reset();
		tickSystems = std::make_unique<ecs::SystemRegistry>();
		frameSystems = std::make_unique<ecs::SystemRegistry>();
		presentation::RegisterPresentationTick(*tickSystems, motionSample, poseSample, detectorPings, exhaustSample, hitFx, missileIgnitions, harvestPresentation, cratePresentation, promotionPresentation, chassis, trackLaying, treeContacts, cashPresentation, disabledSounds, empSparks, autoDepositTexts, abilityFeedback, uplinkStatus, particleClears);
		presentation::RegisterPresentationFrame(*frameSystems, treadRoll, wheelRoll, motionEmitters, objectPresentation, effectAttachments, damageEffects, fireFxPlacement, fxPlayback, exhausts, lasers, dynamicLights, trackFade, treeBreeze, treeBend, debrisAnimation, mountedDraw, tintStatus, objectIcons, uplinkEffects, riderTint);
		presentation::RegisterSoundFrame(*frameSystems, soundLoops, audioMix, uplinkSounds, fireLoops);
		tickSystems->Register(inGameMessages);
		tickSystems->Register(militaryCaption);
		tickSystems->Register(screenFade);
		tickSystems->Register(scriptFlash);
		// Drawable::updateDrawable after the logic's own colour flashes (EMPUpdate).
		tickSystems->OrderBefore<presentation::EmpSparkSystem, presentation::ScriptFlashSystem>();
		tickSystems->OrderBefore<presentation::InGameMessageSystem, presentation::MilitaryCaptionSystem>();
		tickSystems->OrderBefore<presentation::MilitaryCaptionSystem, presentation::EvaSystem>();
		tickSystems->Register(eva);
		tickSystems->OrderBefore<presentation::InGameMessageSystem, presentation::EvaSystem>();
		tickSystems->Register(productionPresentation);
		tickSystems->OrderBefore<presentation::InGameMessageSystem, presentation::ProductionPresentationSystem>();
		tickSystems->OrderBefore<presentation::MilitaryCaptionSystem, presentation::ProductionPresentationSystem>();
		tickSystems->OrderBefore<presentation::CratePresentationSystem, presentation::ProductionPresentationSystem>();
		tickSystems->Register(overchargeNotices);
		tickSystems->OrderBefore<presentation::InGameMessageSystem, presentation::OverchargeNoticeSystem>();
		tickSystems->OrderBefore<presentation::ProductionPresentationSystem, presentation::OverchargeNoticeSystem>();
		tickSystems->Register(rallyNotices);
		tickSystems->OrderBefore<generalszh::presentation::AbilityFeedbackSystem, presentation::RallyNoticeSystem>();
		tickSystems->OrderBefore<generalszh::presentation::DisabledSoundSystem, presentation::RallyNoticeSystem>();
		tickSystems->OrderBefore<presentation::OverchargeNoticeSystem, presentation::RallyNoticeSystem>();
		tickSystems->Register(radarEvents);
		tickSystems->OrderBefore<presentation::OverchargeNoticeSystem, presentation::RadarEventSystem>();
		tickSystems->OrderBefore<presentation::RallyNoticeSystem, presentation::RadarEventSystem>();
		tickSystems->OrderBefore<presentation::ProductionPresentationSystem, presentation::RadarEventSystem>();
		tickSystems->OrderBefore<presentation::RadarEventSystem, presentation::EvaSystem>();
		tickSystems->OrderBefore<presentation::ProductionPresentationSystem, presentation::DisabledSoundSystem>();
		tickSystems->OrderBefore<presentation::EvaSystem, presentation::EmpSparkSystem>();
		frameSystems->Register(pointerInteraction);
		frameSystems->Register(buildPlacement);
		frameSystems->Register(placementGhosts);
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
	} look;

	void LookAround(double gameSeconds)
	{
		if (!simulation)
			return;
		constexpr float scrollMultiplier = 2.0f, scrollAmount = 100.0f * scrollMultiplier, scrollFactor = 0.5f;
		constexpr float horizontalFactor = 1.6f, verticalFactor = 2.0f;
		if (pointer.wheel != 0.0f)
		{
			camera.BeginUserAction();
			camera.ZoomBy(-pointer.wheel * 10.0f);
		}
		if ((pointer.pressed & 2u) != 0 && !look.scrolling)
		{
			look.scrolling = true;
			look.anchorX = pointer.x;
			look.anchorY = pointer.y;
		}
		if ((pointer.down & 2u) == 0)
			look.scrolling = false;
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
			presentation::KnowSelectables(Game().World().Resource<presentation::SelectionCatalog>(), Game());
			presentation::QueueTickSounds(Game().World().Resource<presentation::SoundRequests>(), Game());
			presentation::KnowExhausts(Game().World().Resource<presentation::WeaponExhausts>(), Game());
			presentation::KnowFireLoops(Game().World().Resource<presentation::WeaponFireLoops>(), Game());
			presentation::KnowLasers(Game().World().Resource<presentation::WeaponLasers>(), Game());
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
			settings.matchEnd = ClientSettings::MatchEnd::QuickVictory;
			settings.matchEndTick = localScripts ? localScripts->tick : 0;
			settings.inputDisabled = true;
			break;
		case Kind::SoundDisable: settings.disabledSounds.push_back(command.text); break;
		case Kind::AudioVolumeOverride: settings.soundVolumeOverrides.emplace_back(command.text, command.percent); break;
		case Kind::EvaEnabled: settings.evaEnabled = command.flag; break;
		case Kind::RadarForceEnable: settings.radarForced = true; break;
		case Kind::RadarRevertToNormal: settings.radarForced = false; break;
		case Kind::RadarHidden: settings.radarHidden = command.flag; break;
		case Kind::BorderShroudDisabled: settings.borderShroudDisabled = true; break;
		case Kind::DrawIconUi: settings.drawIconUi = command.flag; break;
		case Kind::TreeSway:
			// ScriptEngine::doSetTreeSway: a new breeze (trees roll their sway again); a period of at least a frame.
			if (auto *breeze = simulation ? Game().World().FindResource<presentation::Breeze>() : nullptr)
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
				Game().World().Side<presentation::Selected>().Clear();
			break;
		case Kind::Letterbox:
			if (settings.letterbox != command.flag)
				++settings.letterboxChanges;
			settings.letterbox = command.flag;
			break;
		case Kind::BlackWhite:
			settings.blackWhite = command.flag;
			settings.blackWhiteFrames = command.percent;
			break;
		case Kind::Fade:
			// ScriptEngine::setFade, first stepped the tick after its script's.
			if (auto *fade = Game().World().FindResource<presentation::ScreenFade>())
			{
				const auto real = [](Engine::Math::Fixed value) { return Engine::Math::ToFloat(value); };
				presentation::SetFade(*fade, static_cast<presentation::FadeKind>(command.percent), real(command.numbers[0]), real(command.numbers[1]),
					command.numbers[2].Round(), command.numbers[3].Round(), command.numbers[4].Round(), presentationTicks + 1);
			}
			break;
		case Kind::RadarEvent:
			// Radar::createEvent (4 seconds), from the logic frame its script ran on.
			if (auto *radar = Game().World().FindResource<presentation::RadarEvents>())
			{
				const auto at = Ground(command.position);
				presentation::CreateRadarEvent(*radar, {at.x, at.y, at.z}, static_cast<presentation::RadarEventType>(command.percent), presentationTicks);
			}
			break;
		case Kind::LogicRate:
		{
			// setLogicTimeScaleFps (0: GameData's FramesPerSecondLimit): the simulation's ticks a second, rendering apart.
			const std::int64_t fps = command.percent != 0 ? command.percent : content.gameData.framesPerSecondLimit;
			if (fps > 0)
				speed = std::clamp(static_cast<float>(fps) / static_cast<float>(LogicTicksPerSecond), 0.05f, 16.0f);
			break;
		}
		case Kind::ObjectSound:
		{
			// Drawable::enableAmbientSoundFromScript on the named unit's drawable.
			ecs::World &world = Game().World();
			const ecs::Entity unit = Game().Named(command.subject);
			if (!world.IsAlive(unit))
				break;
			auto &loops = world.Side<presentation::SoundLoops>();
			if (loops.Get(unit) == nullptr)
			{
				presentation::SoundLoops fresh;
				loops.Insert(unit, &fresh);
			}
			presentation::SoundLoops &mine = *loops.Get(unit);
			mine.scriptOff = command.flag ? 0u : 1u;
			if (command.flag)
				mine.scriptStart = 1;
			else if (mine.ambient != 0 && player)
			{
				player->Stop(mine.ambient);
				mine.ambient = 0;
			}
			break;
		}
		case Kind::NamedTimer:
			// InGameUI::addNamedTimer with TheGameText->fetch of the label.
			if (auto *timers = Game().World().FindResource<presentation::NamedTimers>())
				presentation::AddNamedTimer(*timers, command.subject,
					labels ? labels(command.text) : std::u16string(command.text.begin(), command.text.end()), command.flag);
			break;
		case Kind::NamedTimerHide:
			if (auto *timers = Game().World().FindResource<presentation::NamedTimers>())
				presentation::RemoveNamedTimer(*timers, command.subject);
			break;
		case Kind::NamedTimersShown:
			if (auto *timers = Game().World().FindResource<presentation::NamedTimers>())
				timers->shown = command.flag;
			break;
		case Kind::Flash:
		{
			// doNamedFlash (only for some seconds) / doTeamFlash: LOGICFRAMES_PER_SECOND * seconds / DRAWABLE_FRAMES_PER_FLASH
			// flashes, white or its player's colour (getIndicatorColor).
			ecs::World &world = Game().World();
			std::vector<ecs::Entity> targets;
			if (command.flag)
				targets = Game().TeamMembers(command.subject);
			else if (command.percent > 0)
				targets.push_back(Game().Named(command.subject));
			const auto *catalog = world.FindResource<presentation::LookCatalog>();
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
				auto &flashes = world.Side<presentation::ScriptFlash>();
				presentation::ScriptFlash flash{count, color};
				if (auto *existing = flashes.Get(target))
					*existing = flash;
				else
					flashes.Insert(target, &flash);
			}
			break;
		}
		case Kind::MilitaryCaption:
			// InGameUI::militarySubtitle (TheGameText->fetch of the label), from the logic frame its script ran on.
			if (auto *caption = Game().World().FindResource<presentation::MilitaryCaption>())
				presentation::ShowCaption(*caption, labels ? labels(command.text) : std::u16string(command.text.begin(), command.text.end()), command.percent,
					presentationTicks);
			break;
		// doVictory / doDefeat: the window closes whatever showed, input goes off; doLocalDefeat keeps input.
		case Kind::Victory:
		case Kind::Defeat:
		case Kind::LocalDefeat:
			settings.matchEnd = command.kind == Kind::Victory ? ClientSettings::MatchEnd::Victory
				: command.kind == Kind::Defeat ? ClientSettings::MatchEnd::Defeat : ClientSettings::MatchEnd::LocalDefeat;
			settings.matchEndTick = localScripts ? localScripts->tick : 0;
			settings.inputDisabled = settings.inputDisabled || command.kind != Kind::LocalDefeat;
			break;
		}
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
		using Kind = scripting::CameraScriptCommand::Kind;
		const auto ms = [](std::int64_t value) { return static_cast<float>(value); };
		const auto real = [](Engine::Math::Fixed value) { return Engine::Math::ToFloat(value); };
		switch (command.kind)
		{
		case Kind::Setup:
			// doSetupCamera: moveCameraTo at once, cameraModLookToward, cameraModFinalPitch, cameraModFinalZoom.
			camera.MoveCameraTo(Ground(command.points[0]), 0, 0, true, 0.0f, 0.0f);
			camera.CameraModLookToward(Ground(command.points[1]));
			camera.CameraModFinalPitch(real(command.values[1]), 0.0f, 0.0f);
			camera.CameraModFinalZoom(real(command.values[0]), 0.0f, 0.0f);
			return;
		case Kind::Zoom:
			camera.ZoomCamera(real(command.values[0]), static_cast<int>(command.milliseconds), ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
			return;
		case Kind::Pitch:
			camera.PitchCamera(real(command.values[0]), static_cast<int>(command.milliseconds), ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
			return;
		case Kind::Rotate:
			camera.RotateCamera(real(command.values[0]), static_cast<int>(command.milliseconds), ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
			return;
		case Kind::Reset:
			camera.ResetCamera(Ground(command.points[0]), static_cast<int>(command.milliseconds), ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
			return;
		case Kind::FreezeTime: camera.CameraModFreezeTime(); return;
		case Kind::FreezeAngle: camera.CameraModFreezeAngle(); return;
		case Kind::FinalZoom: camera.CameraModFinalZoom(real(command.values[0]), real(command.values[1]), real(command.values[2])); return;
		case Kind::FinalPitch: camera.CameraModFinalPitch(real(command.values[0]), real(command.values[1]), real(command.values[2])); return;
		case Kind::FinalSpeed: camera.CameraModFinalTimeMultiplier(static_cast<int>(command.count)); return;
		case Kind::RollingAverage: camera.CameraModRollingAverage(static_cast<int>(command.count)); return;
		case Kind::Follow:
		case Kind::Tether:
		{
			// setCameraLock, snapToCameraLock when asked, setSnapMode(LOCK_FOLLOW, 0) or (LOCK_TETHER, play).
			const ecs::Entity unit = Game().Named(command.unit);
			if (!Game().World().IsAlive(unit))
				return;
			camera.SetCameraLock(LockOn(unit));
			if (command.flag)
				camera.SnapToCameraLock();
			camera.SetSnapMode(command.kind == Kind::Follow ? engine::camera::CameraLockMode::Follow : engine::camera::CameraLockMode::Tether,
				command.kind == Kind::Follow ? 0.0f : real(command.values[0]));
			return;
		}
		case Kind::StopFollow: camera.SetCameraLock({}); return;
		case Kind::SetDefault:
		{
			// doCameraSetDefault (PRESERVE_RETAIL_SCRIPTED_CAMERA): pitch = ViewDefaultPitchRadians - pitch; the angle unused.
			constexpr float DefaultPitch = 37.5f * std::numbers::pi_v<float> / 180.0f;
			camera.SetDefaultView(DefaultPitch - real(command.values[0]), real(command.values[1]), real(command.values[2]));
			return;
		}
		case Kind::LookTowardObject:
		{
			const ecs::Entity unit = Game().Named(command.unit);
			if (!Game().World().IsAlive(unit))
				return;
			auto lock = LockOn(unit);
			camera.RotateCameraTowardTarget([lock]() -> std::optional<Engine::Math::Vector3> {
				if (const auto state = lock())
					return state->position;
				return std::nullopt;
			}, static_cast<int>(command.milliseconds), static_cast<int>(command.holdMilliseconds), ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
			return;
		}
		case Kind::LookTowardWaypoint:
			camera.RotateCameraTowardPosition(Ground(command.points[0]), static_cast<int>(command.milliseconds), ms(command.easeInMilliseconds),
				ms(command.easeOutMilliseconds), command.flag);
			return;
		case Kind::Shake:
		{
			// View::shake at the camera's position: the type's GameData intensity, a random direction.
			ecs::World &world = Game().World();
			if (auto *shakes = world.FindResource<presentation::ShakeRequests>())
			{
				const auto &position = camera.Position();
				shakes->pending.push_back({static_cast<presentation::ShakeType>(command.count), {position.x, position.y, position.z}});
			}
			return;
		}
		default:
			break;
		}
		if (command.points.empty())
			return;
		switch (command.kind)
		{
		case Kind::MoveTo:
			camera.MoveCameraTo(Ground(command.points.front()), static_cast<int>(command.milliseconds),
				static_cast<int>(command.shutterMilliseconds), true, ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
			break;
		case Kind::MoveAlongPath:
		{
			std::vector<Engine::Math::Vector3> path;
			for (const auto &point : command.points)
				path.push_back(Ground(point));
			camera.MoveCameraAlongWaypointPath(path, static_cast<int>(command.milliseconds), static_cast<int>(command.shutterMilliseconds),
				true, ms(command.easeInMilliseconds), ms(command.easeOutMilliseconds));
			break;
		}
		case Kind::LookToward:
			camera.CameraModLookToward(Ground(command.points.front()));
			break;
		case Kind::FinalLookToward:
			camera.CameraModFinalLookToward(Ground(command.points.front()));
			break;
		}
	}
	// TerrainTracksRenderObjClassSystem::setDetail and init: GameData's MakeTrackMarks and MaxTerrainTracks, and the
	// detail level's GameLOD track limits.
	void ApplyTrackSettings()
	{
		auto *settings = Game().World().FindResource<presentation::TrackSettings>();
		if (settings == nullptr)
			return;
		settings->make = content.gameData.makeTrackMarks;
		settings->maxTracks = content.gameData.maxTerrainTracks;
		for (const auto &lod : content.staticLods)
			if (lod.name == detailLevel)
			{
				settings->maxEdges = lod.maxTankTrackEdges;
				settings->maxOpaqueEdges = lod.maxTankTrackOpaqueEdges;
				settings->fadeMilliseconds = lod.maxTankTrackFadeDelay;
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
	state.player->SetSampleLimits({state.audio.settings.sampleCount2D, state.audio.settings.sampleCount3D});
	state.output = engine::audio::OpenPreferredOutput(state.mixer);

	state.inGameUi = content::BindInGameUi(loader.Load({"Data/INI/Default/InGameUI", "Data/INI/InGameUI"}));
	state.mouse = content::BindMouse(loader.Load({"Data/INI/Mouse"}));
	state.language = content::ReadLanguageFonts(loader.Load({"Data/English/Language"}));
	state.evaCatalog = content::BindEva(loader.Load({"Data/INI/Default/Eva", "Data/INI/Eva"}), engine::time::FixedStep{30});
	state.effects = presentation::LoadEffectsContent(loader);
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
	std::int32_t rankPoints)
{
	State &state = *m_state;
	if (state.simulation)
	{
		// What the last match played and showed goes with it.
		state.player->StopAll();
		state.particles = std::make_unique<engine::effects::ParticleWorld>(
			[&effects = state.effects](std::string_view name) { return effects.particles.Find(name); },
			[&state](float x, float y) { return state.terrainHeight ? state.terrainHeight(x, y) : 0.0f; });
		state.simulation.reset();
		state.localScripts.reset();
		state.settings.matchEnd = ClientSettings::MatchEnd::None;
		state.settings.inputDisabled = false;
		// Radar::reset (newMap): no longer forced on (a hidden radar stays hidden: the retail code keeps it).
		state.settings.radarForced = false;
		state.settings.localDefeatSeen = false;
		state.instances.clear();
		state.playerColors.clear();
		state.pinnedLook.reset();
	}
	state.speed = state.baseSpeed;
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
	options.localSeat = network ? network->seat : 0;
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
	options.cameraMovementFinished = [&camera = state.camera] { return camera.IsCameraMovementFinished(); };
	// GameEngine::isTimeFrozen: a camera move with CAMERA_MOD_FREEZE_TIME holds the game while it lasts; nothing freezes
	// a network game.
	options.timeFreezes = !network.has_value();
	if (!network)
	{
		options.cameraFreezesTime = [&camera = state.camera] { return camera.IsTimeFrozen() && !camera.IsCameraMovementFinished(); };
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
			(!network || properties.Get<std::string>("playerName").value_or("") == localPlayer))
			state.eyes = index;
	}
	if (state.eyes == presentation::PresentationFrame::NoViewer)
		state.eyes = firstPlayer;
	for (const auto &participant : level.scenario.participants)
	{
		// Its own colour, else its faction's (PlayerTemplate.ini), else the neutral white.
		const auto argb = content::SideColor(participant.properties.Get<std::int64_t>("playerColor"), state.content.factionColors,
			participant.properties.Get<std::string>("playerFaction").value_or(""));
		state.playerColors.push_back({static_cast<float>((argb >> 16) & 0xFF) / 255.0f, static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
			static_cast<float>(argb & 0xFF) / 255.0f, 1.0f});
	}
	state.simulation = network            ? session::MakeNetworkMatch(level, state.content, std::move(options), *network)
		: checkpoint.empty() ? session::MakeClientMatch(level, state.content, std::move(options))
							 : session::ResumeClientMatch(level, state.content, std::move(options), checkpoint);
	if (!state.simulation)
		return false;
	state.playableExtent = state.Game().PlayableExtent();
	state.BindPresentation();
	state.TakeSnapshot();
	state.TakeSnapshot();
	return true;
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
	const ClientSettings &settings = state.settings;
	writer.Text(settings.musicTrack);
	writer.I64(settings.musicVolume);
	writer.I64(settings.soundVolume);
	writer.I64(settings.speechVolume);
	writer.U32(static_cast<std::uint32_t>(settings.disabledSounds.size()));
	for (const std::string &sound : settings.disabledSounds)
		writer.Text(sound);
	writer.U32(static_cast<std::uint32_t>(settings.soundVolumeOverrides.size()));
	for (const auto &[sound, volume] : settings.soundVolumeOverrides)
	{
		writer.Text(sound);
		writer.I64(volume);
	}
	for (const bool flag : {settings.evaEnabled, settings.radarForced, settings.radarHidden, settings.borderShroudDisabled, settings.drawIconUi,
			 settings.occlusion, settings.particleCap, settings.specialPowerDisplayDisabled, settings.inputDisabled, settings.localDefeatSeen})
		writer.Flag(flag);
}

bool GameClient::LoadClientState(engine::core::serialization::ByteReader &reader)
{
	State &state = *m_state;
	const auto real = [&] { return std::bit_cast<float>(reader.U32().value_or(0)); };
	const float x = real(), y = real(), z = real();
	const float angle = real(), pitch = real(), zoom = real();
	ClientSettings settings = state.settings;
	settings.musicTrack = reader.Text().value_or("");
	settings.musicVolume = reader.I64().value_or(100);
	settings.soundVolume = reader.I64().value_or(100);
	settings.speechVolume = reader.I64().value_or(100);
	settings.disabledSounds.clear();
	for (std::uint32_t count = reader.U32().value_or(0); count > 0 && !reader.Failed(); --count)
		settings.disabledSounds.push_back(reader.Text().value_or(""));
	settings.soundVolumeOverrides.clear();
	for (std::uint32_t count = reader.U32().value_or(0); count > 0 && !reader.Failed(); --count)
	{
		std::string sound = reader.Text().value_or("");
		settings.soundVolumeOverrides.emplace_back(std::move(sound), reader.I64().value_or(100));
	}
	for (bool *flag : {&settings.evaEnabled, &settings.radarForced, &settings.radarHidden, &settings.borderShroudDisabled, &settings.drawIconUi,
			 &settings.occlusion, &settings.particleCap, &settings.specialPowerDisplayDisabled, &settings.inputDisabled, &settings.localDefeatSeen})
		*flag = reader.Flag().value_or(*flag);
	if (reader.Failed())
		return false;
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

std::optional<std::array<std::uint8_t, 2>> GameClient::MouseCursor() const
{
	if (!m_state->simulation)
		return std::nullopt;
	const auto *cursor = m_state->Game().World().FindResource<presentation::CursorState>();
	if (cursor == nullptr)
		return std::nullopt;
	return std::array<std::uint8_t, 2>{static_cast<std::uint8_t>(cursor->cursor), cursor->direction};
}
double GameClient::TicksPerSecond() const noexcept { return LogicTicksPerSecond * m_state->speed; }

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
	if (state.pinnedLook)
	{
		const auto [x, y] = *state.pinnedLook;
		state.camera.LookAt({x, y, state.terrainHeight ? state.terrainHeight(x, y) : 0.0f});
	}

	// Animations run on game time too.
	const double gameSeconds = static_cast<double>(std::clamp(deltaSeconds, 0.0f, 0.25f) * state.speed);
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
		frame.clock = state.clock;
		frame.alpha = std::clamp(alpha, 0.0f, 1.0f);
		frame.frame = static_cast<std::uint32_t>(state.presentationFrames + 1);
		frame.tick = state.Game().CurrentTick();
		frame.drawIconUi = state.settings.drawIconUi;
		if (const auto *fade = state.Game().World().FindResource<presentation::ScreenFade>())
			frame.scriptFade = fade->kind != presentation::FadeKind::None;
		if (const auto &local = state.Game().World().Resource<presentation::LocalPlayer>(); local.valid)
			frame.viewer = local.player;
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
		// The interaction sees the pointer as it is and the camera as it looks this frame.
		{
			auto &interaction = state.Game().World().Resource<presentation::InteractionView>();
			const Engine::Math::Vector3 up = view.transform.Basis_Y(), back = view.transform.Basis_Z();
			interaction = {{view.eye.x, view.eye.y, view.eye.z}, {right.x, right.y, right.z}, {up.x, up.y, up.z}, {-back.x, -back.y, -back.z},
				std::tan(view.horizontalFieldOfView * 0.5f), std::tan(view.horizontalFieldOfView * 0.5f) / std::max(view.aspectRatio, 0.01f),
				state.viewportWidth, state.viewportHeight, true};
			const PointerState &pointer = state.pointer;
			state.Game().World().Resource<presentation::PointerInput>() = {pointer.x, pointer.y, pointer.down, pointer.pressed, pointer.released,
				pointer.shift, pointer.ctrl, pointer.alt, pointer.timeMs, pointer.overInterface, state.look.moving, state.look.offsetX, state.look.offsetY};
			state.pointer.pressed = state.pointer.released = 0;
			state.pointer.wheel = 0;
		}
		// A structure being placed: whether it may go where the ghost is (BuildAssistant::isLocationLegalToBuild).
		if (auto &placement = state.Game().World().Resource<presentation::BuildPlacement>(); placement.active && placement.onGround)
		{
			const float turns = placement.facing / (2.0f * std::numbers::pi_v<float>);
			placement.legal = state.Game().CanBuildAt(placement.builder, placement.structure,
				{Engine::Math::Fixed::FromRaw(std::llround(static_cast<double>(placement.at[0]) * 65536.0)), Engine::Math::Fixed::FromRaw(std::llround(static_cast<double>(placement.at[1]) * 65536.0))},
				Engine::Math::TurnAngle{static_cast<std::uint32_t>(static_cast<std::int64_t>(std::llround(static_cast<double>(turns) * 4294967296.0)))});
		}
		// A command waiting for an object: whether its power may be fired at the one under the pointer (as the interaction
		// last picked it; canDoSpecialPowerAtObject).
		if (auto &targeting = state.Game().World().Resource<presentation::GuiTargeting>(); targeting.active && !targeting.shortcutType.empty())
		{
			const auto *local = state.Game().World().FindResource<presentation::LocalPlayer>();
			const auto source = local != nullptr && local->valid ? state.Game().ShortcutPowerSource(local->player, targeting.shortcutType) : std::nullopt;
			targeting.source = source.value_or(ecs::Entity{});
		}
		if (auto &targeting = state.Game().World().Resource<presentation::GuiTargeting>(); targeting.active)
			targeting.validFor = targeting.hovered != ecs::Entity{} && state.Game().CanTargetWithPower(targeting.source, targeting.power, targeting.hovered)
				? targeting.hovered : ecs::Entity{};
		state.frameScheduler->Execute(engine::time::SimulationTime{++state.presentationFrames, engine::time::FixedStep{30}});
		// The player's orders go to the match (they apply on the tick the relay gives them).
		auto &orders = state.Game().World().Resource<presentation::PlayerOrders>().pending;
		for (const auto &order : orders)
			state.simulation->Submit(order);
		orders.clear();
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
	InGameOverlay overlay;
	if (!state.simulation || !state.frameScheduler)
		return overlay;
	ecs::World &world = state.Game().World();
	const auto &box = world.Resource<presentation::SelectionBox>();
	overlay.boxActive = box.active;
	if (const auto *messages = world.FindResource<presentation::InGameMessages>())
	{
		overlay.messageAt = {static_cast<float>(state.inGameUi.messagePosition[0]), static_cast<float>(state.inGameUi.messagePosition[1])};
		for (std::size_t index = messages->slots.size(); index-- > 0;)
			if (const auto &message = messages->slots[index]; message.shown)
				overlay.messages.push_back({message.text, {message.color[0] / 255.0f, message.color[1] / 255.0f, message.color[2] / 255.0f, message.color[3] / 255.0f}});
	}
	if (const auto *timers = world.FindResource<presentation::NamedTimers>())
	{
		const auto rgba = [](const std::array<std::uint8_t, 4> &c) { return std::array<float, 4>{c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f}; };
		for (const auto &line : presentation::NamedTimerLines(*timers, [&state](std::string_view name) { return state.Game().ScriptCounter(name); },
				 state.Game().CurrentTick()))
			overlay.namedTimers.push_back({line.text, line.ready, rgba(line.flashColor ? state.inGameUi.namedTimerFlashColor : state.inGameUi.namedTimerNormalColor)});
		overlay.namedTimerAt = {Engine::Math::ToFloat(state.inGameUi.namedTimerPosition[0]), Engine::Math::ToFloat(state.inGameUi.namedTimerPosition[1])};
	}
	if (const auto *fade = world.FindResource<presentation::ScreenFade>())
	{
		overlay.fade = static_cast<std::uint8_t>(fade->kind);
		overlay.fadeValue = fade->value;
	}
	if (const auto *caption = world.FindResource<presentation::MilitaryCaption>(); caption != nullptr && caption->shown)
	{
		overlay.caption.shown = true;
		overlay.caption.lines = caption->lines;
		overlay.caption.color = {caption->color[0] / 255.0f, caption->color[1] / 255.0f, caption->color[2] / 255.0f, caption->color[3] / 255.0f};
		overlay.caption.block = caption->blockDrawn;
		overlay.caption.at = {static_cast<float>(state.inGameUi.militaryCaptionPosition[0]), static_cast<float>(state.inGameUi.militaryCaptionPosition[1])};
	}
	overlay.box = {std::min(box.x0, box.x1), std::min(box.y0, box.y1), std::max(box.x0, box.x1), std::max(box.y0, box.y1)};
	const auto &view = world.Resource<presentation::InteractionView>();
	const auto &catalog = world.Resource<presentation::SelectionCatalog>();
	const auto &data = state.Game().Content().gameData;
	// Drawable::drawHealthBar for the selected (computeHealthRegion: the health box position projected, its width
	// over the zoom, 3 high, starting 0.45 of its width left of centre; none at no health or for FORCEATTACKABLE).
	const float zoom = std::max(state.camera.Zoom(), 0.01f);
	for (const ecs::Entity entity : world.Side<presentation::Selected>().Entities())
	{
		const auto *transform = world.Get<engine::gameplay::Transform>(entity);
		const auto *definition = world.Get<engine::gameplay::DefinitionRef>(entity);
		const auto *body = world.Get<engine::gameplay::Health>(entity);
		if (transform == nullptr || definition == nullptr || body == nullptr || body->maximum <= Engine::Math::Fixed{} || body->current <= Engine::Math::Fixed{})
			continue;
		const presentation::SelectionLook *look = catalog.Of(definition->index);
		if (look == nullptr || look->healthBoxWidth <= 0.0f || (look->kinds & presentation::select_kind::ForceAttackable) != 0)
			continue;
		float sx = 0, sy = 0;
		if (!view.Project(Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y),
				Engine::Math::ToFloat(transform->position.z) + look->top + 10.0f, sx, sy))
			continue;
		const float width = look->healthBoxWidth / zoom;
		SelectedMarker marker;
		marker.x = std::floor(sx - width * 0.45f);
		marker.y = std::floor(sy - 1.5f);
		marker.width = std::floor(width);
		marker.health = std::clamp(Engine::Math::ToFloat(body->current) / Engine::Math::ToFloat(body->maximum), 0.0f, 1.0f);
		marker.reallyDamaged = body->current < body->maximum * data.unitReallyDamaged;
		marker.damaged = !marker.reallyDamaged && body->current < body->maximum * data.unitDamaged;
		if (const auto *off = world.Get<engine::gameplay::Disabled>(entity))
			marker.disabled = (off->mask & ~engine::gameplay::disabled_type::Held) != 0;
		overlay.selected.push_back(marker);
	}
	// Drawable::drawIconUI for everything seen, while icon UI is on and no script fade runs: its animated icons as
	// ObjectIconSystem left them (their Animation2D's image this many logic frames after it was made) against its health
	// region, then its veterancy (alive, not IGNORED_IN_GUI, with a health box).
	if (state.settings.drawIconUi && overlay.fade == 0)
	{
		const double clock = world.Resource<presentation::PresentationFrame>().clock;
		const auto *looks = world.FindResource<presentation::LookCatalog>();
		const auto &iconTable = world.Side<presentation::ObjectIcons>();
		world.Resource<engine::gameplay::VisibleObjects>().ForEach([&](const engine::gameplay::VisibleObject &object) {
			const presentation::SelectionLook *look = catalog.Of(object.definition);
			if (look == nullptr || look->healthBoxWidth <= 0.0f)
				return;
			const auto &at = object.transform.position;
			float sx = 0, sy = 0;
			if (!view.Project(Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z) + look->top + 10.0f, sx, sy))
				return;
			const int screenX = static_cast<int>(sx), screenY = static_cast<int>(sy);
			if (const presentation::ObjectIcons *icons = iconTable.Get(object.entity); icons != nullptr && icons->drawn != 0)
			{
				const presentation::DefinitionLooks *kind = looks != nullptr ? looks->Of(object.definition) : nullptr;
				const float scale = presentation::EnthusiasticScale(kind != nullptr && (kind->structure || kind->hugeVehicle), kind != nullptr && kind->vehicle);
				for (std::size_t index = 0; index < presentation::ObjectIconCount; ++index)
				{
					const auto icon = static_cast<presentation::ObjectIcon>(index);
					if (!icons->Drawn(icon))
						continue;
					const auto found = state.content.animations2d.find(presentation::ObjectIconAnimations[index]);
					if (found == state.content.animations2d.end() || found->second.images.empty())
						continue;
					const auto frames = static_cast<std::uint64_t>(std::max(clock - icons->since[index], 0.0) * 30.0);
					OverlayImage image{found->second.images[found->second.ImageAt(frames)], sx, sy, 1.0f, 1.0f};
					image.placement = OverlayImage::Placement::Icon;
					image.icon = icon;
					image.region = presentation::HealthRegion(screenX, screenY, look->healthBoxWidth, zoom);
					image.iconScale = scale;
					overlay.images.push_back(std::move(image));
				}
			}
			const auto *experience = world.Get<engine::gameplay::Experience>(object.entity);
			const auto *body = world.Get<engine::gameplay::Health>(object.entity);
			const presentation::DefinitionLooks *kind = looks != nullptr ? looks->Of(object.definition) : nullptr;
			if (experience == nullptr || (body != nullptr && body->current <= Engine::Math::Fixed{}) || world.Has<engine::gameplay::Dying>(object.entity) ||
				(kind != nullptr && kind->ignoredInGui))
				return;
			if (const std::string_view veteran = presentation::VeterancyImage(experience->level); !veteran.empty())
			{
				OverlayImage image{std::string(veteran), static_cast<float>(screenX), static_cast<float>(screenY), 1.0f, 1.0f};
				image.placement = OverlayImage::Placement::Veterancy;
				image.healthBoxWidth = look->healthBoxWidth;
				image.zoom = zoom;
				overlay.images.push_back(std::move(image));
			}
		});
	}
	// InGameUI::updateAndDrawWorldAnimations: each at its risen point, on the image its Animation2D shows this many
	// logic frames in, its own size at 1.3 over the zoom, at its fading alpha.
	if (const auto *animations = world.FindResource<presentation::WorldAnimations>())
	{
		const double clock = world.Resource<presentation::PresentationFrame>().clock;
		for (const presentation::WorldAnimation &animation : animations->shown)
		{
			const auto found = state.content.animations2d.find(animation.animation);
			if (found == state.content.animations2d.end() || found->second.images.empty())
				continue;
			const auto at = animation.PositionAt(clock);
			float sx = 0, sy = 0;
			if (!view.Project(at[0], at[1], at[2], sx, sy))
				continue;
			const auto frames = static_cast<std::uint64_t>(std::max(clock - animation.start, 0.0) * 30.0);
			overlay.images.push_back({found->second.images[found->second.ImageAt(frames)], sx, sy, 1.3f / zoom, animation.AlphaAt(clock)});
		}
	}
	// InGameUI::postDraw's superweapon countdowns: in their owner's colour, a ready one flashing (unless a script hid them).
	overlay.superweaponAt = {Engine::Math::ToFloat(state.inGameUi.superweaponPosition[0]), Engine::Math::ToFloat(state.inGameUi.superweaponPosition[1])};
	if (!state.settings.specialPowerDisplayDisabled)
	{
		const auto *looks = world.FindResource<presentation::LookCatalog>();
		const std::uint64_t tick = state.Game().CurrentTick();
		const auto &flash = state.inGameUi.superweaponFlashColor;
		for (const hud::SuperweaponEntry &entry : hud::ReadSuperweaponTimers(state.Game(), tick))
		{
			OverlaySuperweapon line{entry.shown, entry.power, hud::CountdownText(entry.readySeconds), {1, 1, 1, 1}, entry.ready};
			if (looks != nullptr)
				line.color = looks->ColorOf(entry.player);
			line.color[3] = 1.0f;
			if (entry.shown && entry.ready && state.superweaponFlash.FlashColor(tick, state.inGameUi.superweaponFlashFrames))
				line.color = {flash[0] / 255.0f, flash[1] / 255.0f, flash[2] / 255.0f, flash[3] / 255.0f};
			overlay.superweapons.push_back(std::move(line));
		}
	}
	// InGameUI::drawFloatingText: at its start, risen a pixel a tick, in its colour at its alpha.
	const auto &settings = world.Resource<presentation::FloatingTextSettings>();
	for (const presentation::FloatingText &text : world.Resource<presentation::FloatingTexts>().shown)
	{
		float sx = 0, sy = 0;
		if (!view.Project(text.at[0], text.at[1], text.at[2], sx, sy))
			continue;
		overlay.texts.push_back({text.text, sx, sy - static_cast<float>(text.ticks) * settings.riseRate,
			{text.color[0], text.color[1], text.color[2], static_cast<float>(std::clamp(text.alpha, 0, 255)) / 255.0f}});
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
		targeting.powerType = state.content.powers.templates[*power].type;
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

void GameClient::BeginPlacement(ecs::Entity builder, std::string_view structure)
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
	// ThingTemplate PlacementViewAngle (degrees).
	placement.facing = Engine::Math::ToFloat(what->placementViewAngleDegrees) * std::numbers::pi_v<float> / 180.0f;
}

namespace
{
// VictoryConditions' local answers, the local player's side, and the music, for the local player's scripts.
class LocalMatch final : public scripting::LocalMatchHost
{
public:
	explicit LocalMatch(GameClient &client) : m_client(client) {}

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
		const auto player = m_client.LocalPlayer();
		return player ? outcome->HasLost(*player) : outcome->singleAllianceRemaining; // an observer: once it is over
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
	std::map<std::string, std::int64_t> m_recorded; // PLAYER_LOST_OBJECT_TYPE's counts seen, on this machine

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
	local->host = std::make_unique<LocalMatch>(*this);
	scripting::AddCoreVocabulary(local->vocabulary);
	scripting::AddPresentationVocabulary(local->vocabulary, local->commands);
	scripting::AddMatchVocabulary(local->vocabulary, local->host.get());
	local->runtime.emplace(local->scenario, local->vocabulary, engine::scripting::ScriptHooks{},
		engine::scripting::ScriptRuntimeOptions{static_cast<std::uint32_t>(LogicTicksPerSecond), 1});
	state.localScripts = std::move(local);
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

void GameClient::SetDetailLevel(std::string_view level)
{
	State &state = *m_state;
	state.detailLevel = std::string(level);
	if (state.simulation)
		state.ApplyTrackSettings();
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
		presentation::TrackView view{catalog->trackTextures[track.texture], {}, maxEdges, settings->maxOpaqueEdges};
		std::uint32_t index = track.bottom;
		for (std::uint32_t i = 0; i < track.count; ++i, ++index)
		{
			if (index >= maxEdges)
				index = 0;
			view.edges.push_back(track.edges[index]);
		}
		views.push_back(std::move(view));
	});
	return views;
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
float GameClient::ParticleAlpha() const noexcept { return m_state->effectAlpha; }

std::vector<presentation::ShownLight> GameClient::Lights() const
{
	State &state = *m_state;
	if (const auto *lights = state.simulation ? state.Game().World().FindResource<presentation::DynamicLights>() : nullptr)
		return lights->shown;
	return {};
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
	camera.Set_Transform(Graphics::Import_Affine_Transform(view.transform));
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

std::span<const ObjectInstance> GameClient::Objects() const noexcept { return m_state->instances; }

void GameClient::KnowClips(const std::function<std::optional<std::pair<float, float>>(std::uint32_t look)> &clipOf)
{
	State &state = *m_state;
	if (!state.simulation)
		return;
	ecs::World &world = state.Game().World();
	const auto *catalog = world.FindResource<presentation::LookCatalog>();
	auto *clips = world.FindResource<presentation::LookClips>();
	if (catalog == nullptr || clips == nullptr)
		return;
	clips->byLook.resize(catalog->looks.size());
	for (std::uint32_t look = 0; look < clips->byLook.size(); ++look)
		if (!clips->byLook[look].Known())
			if (const auto clip = clipOf(look))
				clips->byLook[look] = {clip->first, clip->second};
}

ObjectModel GameClient::ModelFor(std::uint32_t look) const
{
	State &state = *m_state;
	const auto *catalog = state.simulation ? state.Game().World().FindResource<presentation::LookCatalog>() : nullptr;
	if (catalog == nullptr || look >= catalog->looks.size())
		return {};
	const presentation::LookEntry info = catalog->looks[look];
	// A model shown instead of a definition's (a debris piece), still or playing one of its animations.
	if (info.model != 0)
		return {catalog->lookModels[look], look < catalog->lookAnimations.size() ? catalog->lookAnimations[look] : std::string{},
			static_cast<ObjectAnimationMode>(info.mode), false};
	const presentation::DefinitionLooks &definitionLooks = catalog->byDefinition[info.definition];
	if (info.draw != 0)
	{
		// Another draw module (a rider): its state's model, animation and parts, turning nothing.
		const content::ModelStates &extra = definitionLooks.extraDraws[info.draw - 1].states;
		const content::ModelState &picked = extra.states[info.state];
		const bool loops = picked.animationMode == content::ModelAnimationMode::Loop || picked.animationMode == content::ModelAnimationMode::LoopPingPong ||
			picked.animationMode == content::ModelAnimationMode::LoopBackwards;
		return {picked.model, picked.animations.empty() ? std::string{} : picked.animations.front(), static_cast<ObjectAnimationMode>(picked.animationMode),
			picked.idleAnimation && !loops, picked.hiddenSubObjects, picked.shownSubObjects, picked.muzzleFlashes};
	}
	const content::ModelStates &states = definitionLooks.states;
	const auto *motion = state.Game().World().FindResource<presentation::MotionLooks>();
	const presentation::MotionLook *wheels = motion != nullptr ? motion->Of(info.definition) : nullptr;
	std::vector<std::string> tires = wheels != nullptr ? wheels->wheelBones : std::vector<std::string>{};
	std::vector<std::string> steered = wheels != nullptr ? wheels->steeredBones : std::vector<std::string>{};
	const std::vector<std::uint8_t> corners = wheels != nullptr ? wheels->wheelCorners : std::vector<std::uint8_t>{};
	const std::string cab = wheels != nullptr ? wheels->cabBone : std::string{};
	const std::string trailer = wheels != nullptr ? wheels->trailerBone : std::string{};
	if (!states.Empty())
	{
		const content::ModelState &picked = states.states[info.state];
		const bool loops = picked.animationMode == content::ModelAnimationMode::Loop || picked.animationMode == content::ModelAnimationMode::LoopPingPong ||
			picked.animationMode == content::ModelAnimationMode::LoopBackwards;
		const std::string animation = picked.animations.empty() ? std::string{} : picked.animations[std::min<std::size_t>(info.variant, picked.animations.size() - 1)];
		ObjectModel model{picked.model, animation,
			static_cast<ObjectAnimationMode>(picked.animationMode), false, picked.hiddenSubObjects,
			picked.shownSubObjects, picked.muzzleFlashes, std::move(tires), std::move(steered), cab, trailer, picked.turretBone, picked.turretPitchBone,
			static_cast<float>(static_cast<std::int32_t>(picked.turretArtAngle.units)) * 6.283185307179586f / 4294967296.0f,
			static_cast<float>(static_cast<std::int32_t>(picked.turretArtPitch.units)) * 6.283185307179586f / 4294967296.0f, picked.recoilBone,
			picked.altTurretBone, picked.altTurretPitchBone,
			static_cast<float>(static_cast<std::int32_t>(picked.altTurretArtAngle.units)) * 6.283185307179586f / 4294967296.0f,
			static_cast<float>(static_cast<std::int32_t>(picked.altTurretArtPitch.units)) * 6.283185307179586f / 4294967296.0f};
		model.tireCorners = corners;
		// Its part overrides (SubObjectsUpgrade), in order over its state's own.
		if (info.parts != 0 && info.parts < catalog->partOverrides.size())
			for (const auto &[part, show] : catalog->partOverrides[info.parts])
			{
				const auto same = [&](const std::string &name) {
					return std::ranges::equal(name, part, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
				};
				std::erase_if(model.hidden, same);
				std::erase_if(model.shown, same);
				(show ? model.shown : model.hidden).push_back(part);
			}
		model.projectileSlots = states.projectileFeedbackSlots;
		model.projectileBones = picked.slotLaunchBones;
		model.projectileHideShow = picked.slotHideShowBones;
		return model;
	}
	content::RestingModel resting = content::DefaultModel(state.Game().Definition(info.definition));
	// The content's AnimationMode names map one to one onto the scene's.
	ObjectModel model{std::move(resting.model), std::move(resting.animation), static_cast<ObjectAnimationMode>(resting.animationMode),
		resting.idleAnimation, {}, {}, {}, std::move(tires), std::move(steered), cab, trailer};
	model.tireCorners = corners;
	// W3DTreeBuffer::updateTexture: a map tree is drawn with its TextureName, from Art/Terrain, else Art/Textures (the
	// asset source reads a path as given, then looks the file name up under the textures).
	if (definitionLooks.bufferTree && !definitionLooks.treeMotion.texture.empty())
		model.texture = "Art/Terrain/" + definitionLooks.treeMotion.texture;
	return model;
}

const ClientSettings &GameClient::Settings() const noexcept { return m_state->settings; }

const content::PlayerTemplates &GameClient::PlayerTemplates() const noexcept { return m_state->content.playerTemplates; }
std::string GameClient::UnportedSummary() const { return m_state->simulation ? m_state->Game().UnportedSummary() : std::string{}; }
std::size_t GameClient::EntityCount() const { return m_state->simulation ? m_state->Game().EntityCount() : 0; }
std::size_t GameClient::WorkerCount() const { return m_state->simulation ? m_state->Game().WorkerCount() : 0; }
}
