export module games.generalszh.presentation.audio.systems.audio_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.rts.aircraft.components.jet;
export import engine.gameplay.rts.containment.components.transport;
export import games.generalszh.presentation.audio.components.sound_loops;
export import games.generalszh.presentation.audio.resources.audio_resources;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.object_frame;
export import games.generalszh.presentation.objects.systems.effect_attachment_systems;
export import games.generalszh.presentation.objects.components.uplink_effects;
export import engine.gameplay.rts.combat.components.firing_tracker;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.spatial.components.transform;
export import games.generalszh.gameplay.railroad.components.railcar;
export import engine.gameplay.rts.construction.components.under_construction;
export import games.generalszh.gameplay.battleplans.components.battle_plan;
export import games.generalszh.gameplay.powers.components.launcher_door;
import Engine.Core.Math.FixedPresentation;

// What the player hears, as presentation systems each frame (one pass each:
// the sound player is their side effect):
// - object loops: each object on screen sounds where it is now (its ambient
//   loop; its move-start sound as it sets off and its movement loop while it
//   moves; its burning loop while aflame; a crash's loop while the wreck
//   falls); objects no longer presented fall silent;
// - a Particle Cannon uplink's loops (ParticleUplinkCannonUpdate's addAudioEvent / removeAudioEvent as UplinkEffects
//   follows them): each start plays its loop afresh (an earlier one of it stopped), each stop ends it; its powering
//   up, unpack and firing loops on the uplink, its ground annihilation loop over the beam's spot;
// - a locomotive's RunningSound: playing on it while it speeds up or runs (RailroadBehavior's update: added setting off
//   and whenever it is not playing then; removed braking for a station, disembark or ping-pong point); stopped
//   otherwise, and when it is gone;
// - the mix: the scripts' commands (music, levels, speech, disabled sounds),
//   one-shot world sounds where things happened, the microphone following
//   the camera, speech in turn and the music repeating.
export namespace generalszh::presentation
{
struct SoundLoopSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<SoundLoops>, ecs::Read<MotionEmission>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Jet>, ecs::Read<engine::gameplay::UnderConstruction>,
		ecs::Read<engine::gameplay::Transport>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<PresentedObjects>, ecs::Read<LookCatalog>, ecs::Read<AudioState>,
		ecs::Write<AudioHandle>, ecs::Read<MotionLooks>, ecs::Read<engine::gameplay::Relationships>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		AudioHandle &audio = context.Write<AudioHandle>();
		if (audio.player == nullptr || audio.content == nullptr)
			return;
		engine::audio::SoundPlayer &player = *audio.player;
		const AudioContent &content = *audio.content;
		const AudioState &state = context.Read<AudioState>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const std::uint32_t serial = context.Read<PresentationFrame>().frame;
		auto &loopsTable = context.Side<SideTables, SoundLoops>();
		const auto &emissions = context.SideRead<SideTables, MotionEmission>();
		const MotionLooks &motionLooks = context.Read<MotionLooks>();
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		auto &commands = context.Commands();
		context.Read<PresentedObjects>().ForEach([&](const PresentedObject &object) {
			const DefinitionLooks *looks = catalog.Of(object.definition);
			if (looks == nullptr)
				return;
			const bool aflame = object.appearance.Test(catalog.bits.aflame);
			// DozerAIUpdate::startBuildingSound / finishBuildingSound: the structure's UnderConstruction loop on it while a
			// builder at its dock works on it, until it stands. (The original started it as the builder got there and
			// stopped it only on completion, so a builder called away left it playing for good: that retail leak is fixed,
			// it stops once nobody works on it.)
			const bool built = object.appearance.Test(catalog.bits.beingConstructed) && lookup.Get<engine::gameplay::UnderConstruction>(object.entity) != nullptr;
			// Going down (dying, not yet down): its crash loop.
			const bool falling = object.appearance.Test(catalog.bits.dying) && !object.appearance.Test(catalog.bits.specialDamaged);
			const bool sounds = !looks->ambientSound.empty() || !looks->ambientDamaged.empty() || !looks->ambientReallyDamaged.empty() ||
				!looks->ambientRubble.empty() || !looks->onDamaged.empty() || !looks->onReallyDamaged.empty() || !looks->moveLoop.empty() || !looks->moveStart.empty() || !looks->moveLoopDamaged.empty() || !looks->moveStartDamaged.empty() ||
				(aflame && !looks->burningSound.empty()) || (built && !looks->constructionLoop.empty()) || (falling && !looks->crashSound.empty()) || (object.turning && !looks->turretLoop.empty()) ||
				!looks->stealthOn.empty() || !looks->stealthOff.empty() || !looks->afterburnerSound.empty() || !looks->lowFuelVoice.empty() ||
				(motionLooks.Of(object.definition) != nullptr && !motionLooks.Of(object.definition)->powerslideSound.empty());
			SoundLoops *loops = loopsTable.Get(object.entity);
			if (loops == nullptr)
			{
				if (sounds)
					commands.Add<SoundLoops>(object.entity, SoundLoops{}); // from next frame
				return;
			}
			loops->seenFrame = serial;
			// AudioManager::shouldPlayLocally: its one-shot sounds for whoever may hear them (its player's).
			const auto *ownerOf = lookup.Get<engine::gameplay::Owner>(object.entity);
			const std::uint32_t owner = ownerOf != nullptr ? ownerOf->player : SoundRequest::NoOwner;
			const auto audible = [&](const engine::audio::SoundEventDefinition &sound) { return Audible(sound, owner, viewer, &relationships); };
			const engine::audio::Vec3 at{object.position[0], object.position[1], object.position[2]};
			// Its damage state picks its ambient sound (Drawable::startAmbientSound); entering damaged or really damaged
			// plays that state's sound once (ActiveBody::attemptDamage).
			const std::uint32_t damage = object.appearance.Test(catalog.bits.rubble) ? 3u : object.appearance.Test(catalog.bits.reallyDamaged) ? 2u
				: object.appearance.Test(catalog.bits.damaged) ? 1u : 0u;
			if (loops->damage != damage)
			{
				if (loops->damage != 0xFFFFFFFFu && damage > loops->damage && damage <= 2u)
					if (const auto *once = AllowedSound(content, state, damage == 1u ? looks->onDamaged : looks->onReallyDamaged); once != nullptr && audible(*once))
						PlaySound(player, state, *once, at);
				if (loops->ambient != 0)
					player.Stop(loops->ambient);
				loops->ambient = 0;
				loops->damage = damage;
			}
			const std::string &ambient = damage == 3u ? looks->ambientRubble
				: damage == 2u && !looks->ambientReallyDamaged.empty() ? looks->ambientReallyDamaged
				: damage == 1u && !looks->ambientDamaged.empty() ? looks->ambientDamaged : looks->ambientSound;
			// enableAmbientSoundFromScript: off, it stops and stays off; on again, it starts (a one-shot one plays once).
			FollowLoop(player, content, state, loops->ambient, ambient, loops->scriptOff == 0, at);
			if (std::exchange(loops->scriptStart, 0u) != 0 && loops->scriptOff == 0)
				if (const auto *once = AllowedSound(content, state, ambient); once != nullptr && !once->Loops())
					PlaySound(player, state, *once, at);
			// StealthUpdate::update: stealthing plays its stealth-on sound, and so does dropping out of stealth; first
			// detected, its stealth-off sound; no longer detected, its stealth-on sound when it is the local player's.
			const std::uint32_t stealth = (object.appearance.Test(catalog.bits.stealthed) ? 1u : 0u) | (object.appearance.Test(catalog.bits.detected) ? 2u : 0u);
			if (loops->stealth != 0xFFFFFFFFu && loops->stealth != stealth)
			{
				const std::uint32_t was = loops->stealth;
				const auto play = [&](const std::string &name) {
					if (const auto *once = AllowedSound(content, state, name); once != nullptr && audible(*once))
						PlaySound(player, state, *once, at);
				};
				if ((was & 1u) != (stealth & 1u))
					play(looks->stealthOn);
				if ((was & 2u) == 0u && (stealth & 2u) != 0u)
					play(looks->stealthOff);
				else if ((was & 2u) != 0u && (stealth & 2u) == 0u)
				{
					const auto *owner = lookup.Get<engine::gameplay::Owner>(object.entity);
					if (owner != nullptr && owner->player == viewer)
						play(looks->stealthOn);
				}
			}
			loops->stealth = stealth;
			// AIInternalMoveToState::startMoveSound: as it sets off, worse than damaged its damaged sounds; its start sound
			// once, or (having none) its move loop for the whole move.
			if (object.moving && loops->wasMoving == 0)
			{
				const bool worse = damage >= 2u;
				const std::string &start = worse ? looks->moveStartDamaged : looks->moveStart;
				loops->moveLoop = 0;
				if (!start.empty())
				{
					if (const auto *once = AllowedSound(content, state, start); once != nullptr && audible(*once))
						PlaySound(player, state, *once, at);
				}
				else
					loops->moveLoop = worse ? 2u : 1u;
			}
			const std::string_view moveLoop = loops->moveLoop == 2u ? std::string_view(looks->moveLoopDamaged)
				: loops->moveLoop == 1u ? std::string_view(looks->moveLoop) : std::string_view{};
			FollowLoop(player, content, state, loops->move, moveLoop, object.moving, at);
			FollowLoop(player, content, state, loops->burning, looks->burningSound, aflame, at);
			FollowLoop(player, content, state, loops->crashing, looks->crashSound, falling, at);
			FollowLoop(player, content, state, loops->turret, looks->turretLoop, object.turning, at);
			FollowLoop(player, content, state, loops->construction, looks->constructionLoop, built, at);
			// JetAIUpdate::friend_enableAfterburners: lit, its Afterburner sound starts (unless still playing); out, it
			// stops.
			const bool burning = object.appearance.Test(catalog.bits.afterburner);
			if (burning && loops->burnerLit == 0u && (loops->afterburner == 0 || !player.Playing(loops->afterburner)))
			{
				loops->afterburner = 0;
				if (const auto *once = AllowedSound(content, state, looks->afterburnerSound); once != nullptr && audible(*once))
					loops->afterburner = PlaySound(player, state, *once, at);
			}
			else if (!burning && loops->afterburner != 0)
			{
				player.Stop(loops->afterburner);
				loops->afterburner = 0;
			}
			if (loops->afterburner != 0 && player.Playing(loops->afterburner))
				player.Move(loops->afterburner, at);
			loops->burnerLit = burning ? 1u : 0u;
			// W3DTruckDraw::doDrawModule: starting to powerslide starts its TruckPowerslideSound; stopping, it stops.
			const MotionEmission *emission = emissions.Get(object.entity);
			const bool sliding = emission != nullptr && emission->powersliding != 0;
			if (sliding && loops->sliding == 0u)
			{
				if (const MotionLook *motion = motionLooks.Of(object.definition))
					if (const auto *once = AllowedSound(content, state, motion->powerslideSound); once != nullptr && audible(*once))
						loops->powerslide = PlaySound(player, state, *once, at);
			}
			else if (!sliding && loops->sliding != 0u && loops->powerslide != 0)
			{
				player.Stop(loops->powerslide);
				loops->powerslide = 0;
			}
			if (loops->powerslide != 0 && player.Playing(loops->powerslide))
				player.Move(loops->powerslide, at);
			loops->sliding = sliding ? 1u : 0u;
			// JetOrHeliCirclingDeadAirfieldState::onEnter: its low fuel voice as it starts circling.
			const auto *jet = lookup.Get<engine::gameplay::Jet>(object.entity);
			const bool circling = jet != nullptr && jet->state == engine::gameplay::JetState::CirclingDeadAirfield;
			if (circling && loops->circling == 0u)
				if (const auto *once = AllowedSound(content, state, looks->lowFuelVoice); once != nullptr && audible(*once))
					PlaySound(player, state, *once, at);
			loops->circling = circling ? 1u : 0u;
			// OpenContain::doLoadSound / doUnloadSound: once a frame someone gets in (EnterSound) or out (ExitSound), at it.
			if (const auto *contain = lookup.Get<engine::gameplay::Transport>(object.entity))
			{
				const auto hear = [&](std::uint64_t &last, std::uint64_t now, const std::string &sound) {
					if (last != SoundLoops::Unheard && now != last && now != 0)
						if (const auto *once = AllowedSound(content, state, sound); once != nullptr && audible(*once))
							PlaySound(player, state, *once, at);
					last = now;
				};
				hear(loops->entered, contain->enteredTick, looks->enterSound);
				hear(loops->exited, contain->doorOpenedTick, looks->exitSound);
			}
			loops->wasMoving = object.moving ? 1u : 0u;
		});
		for (std::size_t index = 0; index < loopsTable.Size(); ++index)
			if (SoundLoops &loops = loopsTable.Value(index); loops.seenFrame != serial)
			{
				for (engine::audio::SoundHandle *handle : {&loops.ambient, &loops.move, &loops.burning, &loops.crashing, &loops.turret, &loops.afterburner, &loops.powerslide, &loops.construction})
				{
					if (*handle != 0)
						player.Stop(*handle);
					*handle = 0;
				}
				loops.wasMoving = 0;
				loops.stealth = 0xFFFFFFFFu;
				loops.burnerLit = 0;
				loops.sliding = 0;
			}
	}
};

struct AudioMixSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<ListenerPose>, ecs::Write<AudioHandle>, ecs::Write<AudioState>, ecs::Write<AudioCommands>,
		ecs::Write<SoundRequests>, ecs::Read<PresentationFrame>, ecs::Read<engine::gameplay::Relationships>>;
	using SideTables = ecs::SideTables<ecs::Write<SpeakingVoice>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		AudioHandle &audio = context.Write<AudioHandle>();
		AudioState &state = context.Write<AudioState>();
		AudioCommands &commands = context.Write<AudioCommands>();
		SoundRequests &sounds = context.Write<SoundRequests>();
		if (audio.player == nullptr || audio.content == nullptr || audio.mixer == nullptr)
		{
			commands.pending.clear();
			sounds.pending.clear();
			return;
		}
		engine::audio::SoundPlayer &player = *audio.player;
		const AudioContent &content = *audio.content;
		const auto startMusic = [&] {
			if (const auto *track = content.Find(state.musicName))
				state.music = PlaySound(player, state, *track);
		};
		bool levels = !state.levelsApplied;
		for (const AudioCommand &command : commands.pending)
			switch (command.kind)
			{
			case AudioCommand::Kind::MusicTrack:
				// The track replaces the current one and repeats until another is set (faded out when asked).
				if (state.musicName == command.text && player.Playing(state.music))
					break;
				// MilesAudioManager::stopAudioEvent(AHSV_StopTheMusicFade): the old track fades over TimeToFadeAudio
				// (processFadingList); AHSV_StopTheMusic: it is released at once.
				if (command.flag)
					player.FadeOut(state.music, FadeMixerFrames(content.settings, audio.mixer->SampleRate()));
				else
					player.Stop(state.music, false);
				state.musicName = command.text;
				state.music = 0;
				state.musicCompletions = 0;
				startMusic();
				break;
			case AudioCommand::Kind::MusicVolume: state.scriptMusic = command.share, levels = true; break;
			case AudioCommand::Kind::SoundVolume: state.scriptSound = command.share, levels = true; break;
			case AudioCommand::Kind::SpeechVolume: state.scriptSpeech = command.share, levels = true; break;
			case AudioCommand::Kind::SpeechPlay:
				if (AllowedSound(content, state, command.text) != nullptr)
					state.speech.push_back(command.text);
				break;
			case AudioCommand::Kind::UserMusic: state.userMusic = command.share, levels = true; break;
			case AudioCommand::Kind::UserSound: state.userSound = command.share, levels = true; break;
			case AudioCommand::Kind::UserSound3D: state.userSound3D = command.share, levels = true; break;
			case AudioCommand::Kind::UserSpeech: state.userSpeech = command.share, levels = true; break;
			case AudioCommand::Kind::Interface:
				if (command.flag)
				{
					// removeAudioEvent(lastPreviewSound), then the new one.
					player.Stop(state.interfaceVoice, false);
					state.interfaceVoice = 0;
					if (const auto *sound = command.text.empty() ? nullptr : AllowedSound(content, state, command.text))
						state.interfaceVoice = PlaySound(player, state, *sound);
				}
				else if (const auto *sound = AllowedSound(content, state, command.text))
					PlaySound(player, state, *sound);
				break;
			case AudioCommand::Kind::Eva:
				++state.evaServed;
				if (const auto *line = AllowedSound(content, state, command.text))
					state.eva = PlaySound(player, state, *line);
				break;
			case AudioCommand::Kind::SoundStop: player.StopEvent(command.text); break;
			case AudioCommand::Kind::SoundStopMuted: player.StopMuted(); break;
			case AudioCommand::Kind::FlatSoundsPaused:
				// MilesAudioManager::pauseAudio(AudioAffect_Sound): each playing 2D sound held where it is, and every play
				// request not yet carried out dropped; resumeAudio lets them go on.
				player.PauseFlat(command.flag);
				if (command.flag)
					sounds.pending.clear();
				break;
			case AudioCommand::Kind::SoundDisable:
			case AudioCommand::Kind::SoundEnable:
			case AudioCommand::Kind::VolumeOverride:
			{
				// setAudioEventVolumeOverride: a volume for the event (a disable: none), what plays now taking it too
				// and later plays starting at it; -1 (an enable, a restore): its override gone; no event: every one gone.
				const float share = command.kind == AudioCommand::Kind::SoundDisable ? 0.0f
					: command.kind == AudioCommand::Kind::SoundEnable ? -1.0f : command.share;
				if (command.text.empty())
				{
					state.volumeOverrides.clear();
					break;
				}
				auto &overrides = state.volumeOverrides;
				const auto found = std::lower_bound(overrides.begin(), overrides.end(), command.text,
					[](const auto &entry, const std::string &name) { return entry.first < name; });
				const bool present = found != overrides.end() && found->first == command.text;
				if (share < 0.0f)
				{
					if (present)
						overrides.erase(found);
					break;
				}
				player.SetVolume(command.text, share);
				if (present)
					found->second = share;
				else
					overrides.insert(found, {command.text, share});
				break;
			}
			}
		commands.pending.clear();
		if (levels)
		{
			ApplyLevels(*audio.mixer, content.settings, state);
			state.levelsApplied = true;
		}
		// One-shot world sounds where things happened, for whoever may hear them.
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		const auto *relationships = &context.Read<engine::gameplay::Relationships>();
		auto &speaking = context.Side<SideTables, SpeakingVoice>();
		for (const SoundRequest &sound : sounds.pending)
			if (const auto *event = AllowedSound(content, state, sound.sound); event != nullptr && Audible(*event, sound.owner, viewer, relationships))
			{
				// SoundManager::canPlayNow's violatesVoice: a voice for an object already saying one (isObjectPlayingVoice) is
				// dropped unless it interrupts.
				const bool voice = (event->type & engine::audio::sound_type::Voice) != 0 && sound.object.IsValid();
				SpeakingVoice *talking = voice ? speaking.Get(sound.object) : nullptr;
				if (talking != nullptr && player.Playing(talking->handle) && (event->control & engine::audio::sound_control::Interrupt) == 0)
					continue;
				const std::optional<engine::audio::Vec3> where =
					sound.positioned ? std::optional(engine::audio::Vec3{sound.at[0], sound.at[1], sound.at[2]}) : std::nullopt;
				const engine::audio::SoundHandle handle = sound.volume ? player.Play(*event, where, *sound.volume) : PlaySound(player, state, *event, where);
				if (voice)
				{
					if (talking == nullptr)
						talking = speaking.Emplace(sound.object);
					talking->handle = handle;
				}
			}
		sounds.pending.clear();
		if (const ListenerPose &pose = context.Read<ListenerPose>(); pose.placed)
		{
			const engine::audio::Listener microphone = MicrophoneFor(content.settings, pose);
			audio.mixer->SetListener(microphone);
			// set3DVolumeAdjustment: the world sounds' levels follow the zoom.
			if (const float zoom = ZoomVolume(content.settings, pose, microphone.position); zoom != state.zoomVolume)
			{
				state.zoomVolume = zoom;
				ApplyLevels(*audio.mixer, content.settings, state);
			}
			// MilesAudioManager::update: the device listener, then processPlayingList's cull of world sounds too quiet
			// at the microphone (MinSampleVolume).
			player.SetListenerPosition(microphone.position);
			const SoundLevels levels = LevelsFor(content.settings, state);
			player.CullQuiet(levels.positional, levels.flat);
		}
		state.evaSpeaking = player.Playing(state.eva);
		// Speech in turn; the music repeats.
		if (!player.Playing(state.speaking) && !state.speech.empty())
		{
			if (const auto *line = AllowedSound(content, state, state.speech.front()))
				state.speaking = PlaySound(player, state, *line);
			state.speech.erase(state.speech.begin());
		}
		if (!state.musicName.empty() && !player.Playing(state.music))
		{
			if (state.music != 0)
				++state.musicCompletions; // played through: again
			startMusic();
		}
		player.Update();
	}
};

// FiringTracker's sounds, each frame: a weapon firing with a FireSoundLoopTime keeps its fire sound looping on the
// shooter; a shot finding it not playing (never started, or ended) starts it afresh; it stops once the tracker's loop
// time is up (removeAudioEvent). Going FAST, the shooter says its VoiceRapidFire (speedUp). A shooter no longer there
// takes its loop with it.
struct FireLoopSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::FiringTracker>, ecs::Read<engine::gameplay::Armament>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Read<engine::gameplay::Transform>>;
	using SideTables = ecs::SideTables<ecs::Write<FireSoundLoop>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<AudioState>, ecs::Read<WeaponFireLoops>, ecs::Write<AudioHandle>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		AudioHandle &audio = context.Write<AudioHandle>();
		if (audio.player == nullptr || audio.content == nullptr)
			return;
		engine::audio::SoundPlayer &player = *audio.player;
		const AudioContent &content = *audio.content;
		const AudioState &state = context.Read<AudioState>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const WeaponFireLoops &loops = context.Read<WeaponFireLoops>();
		auto &table = context.Side<SideTables, FireSoundLoop>();
		query.ForEachChunk([&](auto chunk) {
			const auto trackers = chunk.template Get<engine::gameplay::FiringTracker>();
			const auto armaments = chunk.template Get<engine::gameplay::Armament>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < trackers.size(); ++row)
			{
				const engine::gameplay::FiringTracker &tracker = trackers[row];
				FireSoundLoop *heard = table.Get(entities[row]);
				if (heard == nullptr)
				{
					if (tracker.loopUntil != 0 || tracker.level != 0)
						context.Commands().Add<FireSoundLoop>(entities[row], FireSoundLoop{0, 0, tracker.level, 0}); // from next frame
					continue;
				}
				const auto &at = transforms[row].position;
				const engine::audio::Vec3 where{Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z)};
				if (heard->level == 1 && tracker.level == 2)
					if (const DefinitionLooks *looks = catalog.Of(definitions[row].index); looks != nullptr)
						if (const auto *voice = AllowedSound(content, state, looks->rapidFireVoice))
							PlaySound(player, state, *voice, where);
				heard->level = tracker.level;
				if (tracker.loopUntil == 0)
				{
					if (heard->handle != 0)
						player.Stop(heard->handle);
					heard->handle = 0;
					continue;
				}
				const bool playing = heard->handle != 0 && player.Playing(heard->handle);
				if (!playing && armaments[row].firedTick != heard->firedTick)
				{
					heard->handle = 0;
					if (const auto *sound = AllowedSound(content, state, loops.Of(tracker.loopWeapon)))
						heard->handle = PlaySound(player, state, *sound, where);
				}
				else if (playing)
					player.Move(heard->handle, where);
				heard->firedTick = armaments[row].firedTick;
			}
		});
	}
};

struct TrainSoundSystem
{
	using Query = ecs::Query<ecs::Read<generalszh::gameplay::Railcar>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<generalszh::gameplay::Railcar>>;
	using SideTables = ecs::SideTables<ecs::Write<TrainSoundLoop>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<AudioState>, ecs::Write<AudioHandle>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		AudioHandle &audio = context.Write<AudioHandle>();
		auto &loops = context.Side<SideTables, TrainSoundLoop>();
		if (audio.player == nullptr || audio.content == nullptr)
			return;
		engine::audio::SoundPlayer &player = *audio.player;
		const AudioContent &content = *audio.content;
		const AudioState &state = context.Read<AudioState>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const auto lookup = context.Lookup<Lookup>();
		// Gone trains fall silent.
		for (std::size_t index = 0; index < loops.Size();)
		{
			const ecs::Entity entity = loops.Entities()[index];
			if (!lookup.IsAlive(entity))
			{
				if (loops.Value(index).handle != 0)
					player.Stop(loops.Value(index).handle);
				loops.Erase(entity);
				continue;
			}
			++index;
		}
		query.ForEachChunk([&](auto chunk) {
			const auto cars = chunk.template Get<generalszh::gameplay::Railcar>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < cars.size(); ++row)
			{
				const generalszh::gameplay::Railcar &car = cars[row];
				if (car.locomotive == 0)
					continue;
				const DefinitionLooks *looks = catalog.Of(definitions[row].index);
				const bool wanted = looks != nullptr && !looks->trainRunningSound.empty() && car.gone == 0 &&
					car.state == generalszh::gameplay::ConductorState::Accelerate;
				TrainSoundLoop *loop = loops.Get(entities[row]);
				if (loop == nullptr)
				{
					if (!wanted)
						continue;
					loop = loops.Emplace(entities[row]);
				}
				const auto &at = transforms[row].position;
				const engine::audio::Vec3 where{Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z)};
				const bool playing = loop->handle != 0 && player.Playing(loop->handle);
				if (wanted && !playing)
				{
					loop->handle = 0;
					if (const auto *sound = AllowedSound(content, state, looks->trainRunningSound))
						loop->handle = PlaySound(player, state, *sound, where);
				}
				else if (!wanted && loop->handle != 0)
				{
					player.Stop(loop->handle);
					loop->handle = 0;
				}
				else if (playing)
					player.Move(loop->handle, where);
			}
		});
	}
};

// BattlePlanUpdate::setStatus's sounds on the Strategy Center (attached to it, stopped as the status they belong to ends):
// unpacking a plan, its unpack sound; Search and Destroy active, its idle loop; packing a plan, its pack sound (only when
// the plan has an unpack sound, as the original tests it).
inline std::string_view PlanSound(const DefinitionLooks &looks, generalszh::gameplay::PlanTransition status, generalszh::gameplay::PlanStatus plan)
{
	using generalszh::gameplay::PlanTransition;
	using generalszh::gameplay::PlanStatus;
	if (plan == PlanStatus::None)
		return {};
	const auto index = static_cast<std::size_t>(plan) - 1;
	if (index >= looks.planUnpackSounds.size())
		return {};
	switch (status)
	{
	case PlanTransition::Unpacking: return looks.planUnpackSounds[index];
	case PlanTransition::Active: return plan == PlanStatus::SearchAndDestroy ? std::string_view(looks.planIdleLoop) : std::string_view{};
	case PlanTransition::Packing: return looks.planUnpackSounds[index].empty() ? std::string_view{} : std::string_view(looks.planPackSounds[index]);
	default: return {};
	}
}

struct BattlePlanSoundSystem
{
	using Query = ecs::Query<ecs::Read<generalszh::gameplay::BattlePlan>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<BattlePlanSound>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<AudioState>, ecs::Write<AudioHandle>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		AudioHandle &audio = context.Write<AudioHandle>();
		if (audio.player == nullptr || audio.content == nullptr)
			return;
		engine::audio::SoundPlayer &player = *audio.player;
		const AudioContent &content = *audio.content;
		const AudioState &state = context.Read<AudioState>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &sounds = context.Side<SideTables, BattlePlanSound>();
		query.ForEachChunk([&](auto chunk) {
			const auto plans = chunk.template Get<generalszh::gameplay::BattlePlan>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < plans.size(); ++row)
			{
				const DefinitionLooks *looks = catalog.Of(definitions[row].index);
				if (looks == nullptr)
					continue;
				const auto &plan = plans[row];
				const auto key = static_cast<std::uint32_t>(plan.status) * 4u + static_cast<std::uint32_t>(plan.current) + 1u;
				const auto &at = transforms[row].position;
				const engine::audio::Vec3 where{Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z)};
				BattlePlanSound *sound = sounds.Get(entities[row]);
				if (sound == nullptr)
				{
					if (PlanSound(*looks, plan.status, plan.current).empty())
						continue;
					sound = sounds.Emplace(entities[row]);
				}
				if (sound->key == key)
				{
					if (sound->handle != 0 && player.Playing(sound->handle))
						player.Move(sound->handle, where);
					continue;
				}
				if (sound->handle != 0)
					player.Stop(sound->handle);
				sound->handle = 0;
				sound->key = key;
				if (const auto *event = AllowedSound(content, state, PlanSound(*looks, plan.status, plan.current)))
					sound->handle = PlaySound(player, state, *event, where);
			}
		});
	}
};

// MissileLauncherBuildingUpdate::switchToState: entering DOOR_OPEN its DoorOpenIdleAudio starts on the building (unless still
// playing), leaving it the audio stops.
struct DoorIdleSoundSystem
{
	using Query = ecs::Query<ecs::Read<generalszh::gameplay::LauncherDoor>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<DoorIdleSound>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<AudioState>, ecs::Write<AudioHandle>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		AudioHandle &audio = context.Write<AudioHandle>();
		if (audio.player == nullptr || audio.content == nullptr)
			return;
		engine::audio::SoundPlayer &player = *audio.player;
		const AudioContent &content = *audio.content;
		const AudioState &state = context.Read<AudioState>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &sounds = context.Side<SideTables, DoorIdleSound>();
		query.ForEachChunk([&](auto chunk) {
			const auto doors = chunk.template Get<generalszh::gameplay::LauncherDoor>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < doors.size(); ++row)
			{
				const DefinitionLooks *looks = catalog.Of(definitions[row].index);
				if (looks == nullptr || looks->doorOpenIdleAudio.empty())
					continue;
				const bool open = doors[row].state == generalszh::gameplay::LauncherDoorState::Open;
				DoorIdleSound *sound = sounds.Get(entities[row]);
				if (sound == nullptr)
				{
					if (!open)
						continue;
					sound = sounds.Emplace(entities[row]);
				}
				const auto &at = transforms[row].position;
				const engine::audio::Vec3 where{Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z)};
				if (open && sound->open == 0 && (sound->handle == 0 || !player.Playing(sound->handle)))
				{
					sound->handle = 0;
					if (const auto *event = AllowedSound(content, state, looks->doorOpenIdleAudio))
						sound->handle = PlaySound(player, state, *event, where);
				}
				else if (!open && sound->handle != 0)
				{
					player.Stop(sound->handle);
					sound->handle = 0;
				}
				else if (sound->handle != 0 && player.Playing(sound->handle))
					player.Move(sound->handle, where);
				sound->open = open ? 1u : 0u;
			}
		});
	}
};

// SpecialAbilityUpdate's PrepSoundLoop (startPreparation: addAudioEvent on the unit; endPreparation / onExit:
// removeAudioEvent): each start its cue asks for plays the ability's loop afresh on the unit (the one before stopped),
// following it; no longer wanted, or the unit gone, it stops.
struct PrepSoundSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>>;
	using SideTables = ecs::SideTables<ecs::Read<PrepSoundCue>, ecs::Write<PrepSoundLoop>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<AudioState>, ecs::Write<AudioHandle>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const auto &cues = context.SideRead<SideTables, PrepSoundCue>();
		auto &loops = context.Side<SideTables, PrepSoundLoop>();
		AudioHandle &audio = context.Write<AudioHandle>();
		if ((cues.Size() == 0 && loops.Size() == 0) || audio.player == nullptr || audio.content == nullptr)
			return;
		engine::audio::SoundPlayer &player = *audio.player;
		const AudioContent &content = *audio.content;
		const AudioState &state = context.Read<AudioState>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const auto lookup = context.Lookup<Lookup>();
		// The unit's SpecialAbilityUpdate for the power: its PrepSoundLoop.
		const auto soundOf = [&](const engine::gameplay::DefinitionRef &ref, std::uint32_t power) -> std::string_view {
			if (const DefinitionLooks *looks = catalog.Of(ref.index))
				for (const auto &[which, sound] : looks->prepLoops)
					if (which == power)
						return sound;
			return {};
		};
		for (std::size_t index = 0; index < loops.Size();)
		{
			const ecs::Entity entity = loops.Entities()[index];
			const PrepSoundCue *cue = cues.Get(entity);
			if (!lookup.IsAlive(entity) || cue == nullptr || cue->want == 0)
			{
				if (loops.Value(index).handle != 0)
					player.Stop(loops.Value(index).handle);
				loops.Erase(entity);
				continue;
			}
			++index;
		}
		for (std::size_t index = 0; index < cues.Size(); ++index)
		{
			const ecs::Entity entity = cues.Entities()[index];
			const PrepSoundCue &cue = cues.Value(index);
			const auto *ref = lookup.IsAlive(entity) ? lookup.Get<engine::gameplay::DefinitionRef>(entity) : nullptr;
			const auto *transform = ref != nullptr ? lookup.Get<engine::gameplay::Transform>(entity) : nullptr;
			if (cue.want == 0 || transform == nullptr)
				continue;
			const engine::audio::Vec3 at{Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y),
				Engine::Math::ToFloat(transform->position.z)};
			PrepSoundLoop *loop = loops.Get(entity);
			if (loop == nullptr)
				loop = loops.Emplace(entity);
			else if (loop->heard == cue.started)
			{
				if (loop->handle != 0 && player.Playing(loop->handle))
					player.Move(loop->handle, at);
				continue;
			}
			if (loop->handle != 0)
				player.Stop(loop->handle);
			loop->handle = 0;
			loop->heard = cue.started;
			if (const auto *sound = AllowedSound(content, state, soundOf(*ref, cue.power)))
				loop->handle = PlaySound(player, state, *sound, at);
		}
	}
};

struct UplinkSoundSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>>;
	using SideTables = ecs::SideTables<ecs::Read<UplinkEffects>, ecs::Write<UplinkSounds>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<AudioState>, ecs::Write<AudioHandle>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const auto &effects = context.SideRead<SideTables, UplinkEffects>();
		AudioHandle &audio = context.Write<AudioHandle>();
		if (effects.Size() == 0 || audio.player == nullptr || audio.content == nullptr)
			return;
		engine::audio::SoundPlayer &player = *audio.player;
		const AudioContent &content = *audio.content;
		const AudioState &state = context.Read<AudioState>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &soundsTable = context.Side<SideTables, UplinkSounds>();
		const auto lookup = context.Lookup<Lookup>();
		for (std::size_t index = 0; index < effects.Size(); ++index)
		{
			const ecs::Entity entity = effects.Entities()[index];
			const UplinkEffects &fx = effects.Value(index);
			const auto *definition = lookup.IsAlive(entity) ? lookup.Get<engine::gameplay::DefinitionRef>(entity) : nullptr;
			const auto *transform = lookup.IsAlive(entity) ? lookup.Get<engine::gameplay::Transform>(entity) : nullptr;
			const DefinitionLooks *looks = definition != nullptr ? catalog.Of(definition->index) : nullptr;
			if (looks == nullptr || !looks->uplink || transform == nullptr)
				continue;
			UplinkSounds *sounds = soundsTable.Get(entity);
			if (sounds == nullptr)
			{
				context.Commands().Add<UplinkSounds>(entity, UplinkSounds{}); // from next frame
				continue;
			}
			const engine::audio::Vec3 on{Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y),
				Engine::Math::ToFloat(transform->position.z)};
			for (std::size_t loop = 0; loop < content::UplinkSoundCount; ++loop)
			{
				engine::audio::SoundHandle &handle = sounds->handles[loop];
				const engine::audio::Vec3 at = loop == static_cast<std::size_t>(content::UplinkSound::GroundAnnihilation)
					? engine::audio::Vec3{fx.annihilationAt[0], fx.annihilationAt[1], fx.annihilationAt[2]}
					: on;
				if (fx.want[loop] == 0 || sounds->heard[loop] != fx.started[loop])
				{
					if (handle != 0)
						player.Stop(handle);
					handle = 0;
					if (fx.want[loop] != 0)
						if (const auto *sound = AllowedSound(content, state, looks->uplink->sounds[loop]))
							handle = PlaySound(player, state, *sound, at);
					sounds->heard[loop] = fx.started[loop];
				}
				else if (handle != 0 && player.Playing(handle))
					player.Move(handle, at);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::FireLoopSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.fire_loops";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::TrainSoundSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.train_sounds";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::BattlePlanSoundSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.battle_plan_sounds";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::DoorIdleSoundSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.door_idle_sounds";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::PrepSoundSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.prep_sounds";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::UplinkSoundSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.uplink_sounds";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::SoundLoopSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.sound_loops";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::AudioMixSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.audio_mix";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
