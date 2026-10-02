export module games.generalszh.presentation.audio.algorithms.audio_schedule;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
export import games.generalszh.presentation.audio.systems.audio_systems;
export import games.generalszh.presentation.objects.systems.uplink_systems;
export import games.generalszh.session.session_view;
import Engine.Core.Math.FixedPresentation;
import games.generalszh.presentation.audio.systems.wave_guide_sound_system;
import engine.gameplay.rts.teams.resources.team_roster;
import engine.gameplay.common.physics.resources.landings;
import engine.gameplay.rts.parachute.resources.parachute_openings;
import engine.gameplay.common.identity.components.owner;
import games.generalszh.gameplay.abilities.resources.sticky_bomb_cues;
import games.generalszh.gameplay.bridges.resources.bridge_cues;
import games.generalszh.gameplay.hacking.resources.hack_cues;
import games.generalszh.gameplay.battleplans.resources.battle_plan_cues;
import games.generalszh.gameplay.railroad.resources.rail_network;
import engine.gameplay.rts.delivery.components.delivery;
import engine.gameplay.rts.stealth.resources.detections;

// Sound's part of presentation: its side table, its systems in the frame
// schedule (after the objects are presented and the frame's FX have asked
// for their sounds), and the tick's sounds asked for between ticks.
export namespace generalszh::presentation
{
void RegisterSoundComponents(ecs::World &world)
{
	world.RegisterComponent<SoundLoops>();
	world.RegisterComponent<UplinkSounds>();
	world.RegisterComponent<TrainSoundLoop>();
	world.RegisterComponent<FireSoundLoop>();
	world.RegisterComponent<PrepSoundLoop>();
	world.RegisterComponent<BattlePlanSound>();
	world.RegisterComponent<DoorIdleSound>();
	world.RegisterComponent<WaveGuideSoundLoop>();
}

inline void RegisterSoundFrame(ecs::SystemRegistry &frame)
{
	// Its systems are stateless: one shared instance each.
	static SoundLoopSystem loops;
	static AudioMixSystem mix;
	static UplinkSoundSystem uplinks;
	static FireLoopSystem fireLoops;
	static TrainSoundSystem trains;
	static PrepSoundSystem preps;
	static BattlePlanSoundSystem planSounds;
	static DoorIdleSoundSystem doorSounds;
	// The missile launchers' open-door hiss, with the objects' loops, before the mix.
	frame.Register(doorSounds);
	// The flood waves' looping sound, with the objects' loops, before the mix.
	static WaveGuideSoundSystem waveGuideSounds;
	frame.Register(waveGuideSounds);
	frame.OrderBefore<DoorIdleSoundSystem, WaveGuideSoundSystem>();
	frame.OrderBefore<WaveGuideSoundSystem, AudioMixSystem>();
	frame.OrderBefore<BattlePlanSoundSystem, DoorIdleSoundSystem>();
	frame.OrderBefore<DoorIdleSoundSystem, AudioMixSystem>();
	// The Strategy Centers' plan sounds, with the objects' loops, before the mix.
	frame.Register(planSounds);
	frame.OrderBefore<PrepSoundSystem, BattlePlanSoundSystem>();
	frame.OrderBefore<BattlePlanSoundSystem, AudioMixSystem>();
	// The abilities' preparation loops, with the objects' loops, before the mix.
	frame.Register(preps);
	frame.OrderBefore<SoundLoopSystem, PrepSoundSystem>();
	frame.OrderBefore<UplinkSoundSystem, PrepSoundSystem>();
	frame.OrderBefore<FireLoopSystem, PrepSoundSystem>();
	frame.OrderBefore<TrainSoundSystem, PrepSoundSystem>();
	frame.OrderBefore<PrepSoundSystem, AudioMixSystem>();
	// Locomotives' running loops, with the objects' loops, before the mix.
	frame.Register(trains);
	frame.OrderBefore<SoundLoopSystem, TrainSoundSystem>();
	frame.OrderBefore<UplinkSoundSystem, TrainSoundSystem>();
	frame.OrderBefore<FireLoopSystem, TrainSoundSystem>();
	frame.OrderBefore<TrainSoundSystem, AudioMixSystem>();
	// The weapons' looping fire sounds, with the objects' loops, before the mix.
	frame.Register(fireLoops);
	frame.OrderBefore<SoundLoopSystem, FireLoopSystem>();
	frame.OrderBefore<UplinkSoundSystem, FireLoopSystem>();
	frame.OrderBefore<FireLoopSystem, AudioMixSystem>();
	frame.Register(loops);
	frame.Register(mix);
	// The uplinks' loops once their effects followed this frame's statuses, with the objects' loops, before the mix.
	frame.Register(uplinks);
	frame.OrderBefore<UplinkEffectSystem, UplinkSoundSystem>();
	frame.OrderBefore<SoundLoopSystem, UplinkSoundSystem>();
	frame.OrderBefore<UplinkSoundSystem, AudioMixSystem>();
	frame.OrderBefore<ObjectPresentationSystem, SoundLoopSystem>();
	frame.OrderBefore<SoundLoopSystem, AudioMixSystem>();
	frame.OrderBefore<FxPlaybackSystem, AudioMixSystem>();
	// Vehicles' powerslides and landings are heard the frame they start.
	frame.OrderBefore<MotionEmitterSystem, SoundLoopSystem>();
	frame.OrderBefore<MotionEmitterSystem, AudioMixSystem>();
}

// Shots' fire sounds where they were fired this tick.
void QueueTickSounds(SoundRequests &sounds, session::SessionView &view)
{
	view.Fired().ForEach([&](const engine::gameplay::Shot &shot) {
		// FiringTracker::shotFired: a weapon with a FireSoundLoopTime keeps its sound looping instead (FireLoopSystem).
		if (const auto *weapon = view.WeaponContentOf(shot.weapon); weapon != nullptr && !weapon->fireSound.empty() && weapon->simulation.fireSoundLoopTicks == 0)
			sounds.pending.push_back({weapon->fireSound,
				{Engine::Math::ToFloat(shot.origin.x), Engine::Math::ToFloat(shot.origin.y), Engine::Math::ToFloat(shot.origin.z)}});
	});
	// Riders in and out (OpenContain::onContaining / onRemoving): the container's SoundEnter or SoundExit on it, and
	// a rider leaving its SoundFallingFromPlane on itself.
	const auto at = [](const Engine::Math::FixedVector3 &position) -> std::array<float, 3> {
		return {Engine::Math::ToFloat(position.x), Engine::Math::ToFloat(position.y), Engine::Math::ToFloat(position.z)};
	};
	for (const session::CargoMove &move : view.CargoMoves())
	{
		if (move.hasContainer)
			if (const std::string_view sound = view.Definition(move.containerDefinition).Sound(move.entered ? "SoundEnter" : "SoundExit"); !sound.empty())
				sounds.pending.push_back({std::string(sound), at(move.containerAt)});
		if (!move.entered && move.hasRider)
			if (const std::string_view sound = view.Definition(move.riderDefinition).Sound("SoundFallingFromPlane"); !sound.empty())
				sounds.pending.push_back({std::string(sound), at(move.riderAt)});
	}
	// StealthUpdate::changeVisualDisguise: the disguiser's own DisguiseStarted as it takes a look; losing it,
	// DisguiseRevealedSuccess with a victim, else DisguiseRevealedFailure (its per-unit sounds, on it).
	if (const auto *disguises = view.World().FindResource<engine::gameplay::DisguiseEvents>())
		disguises->ForEach([&](const engine::gameplay::DisguiseEvent &event) {
			const auto definition = view.DefinitionOf(event.entity);
			if (!definition)
				return;
			const std::string_view sound = view.Definition(*definition).Sound(
				event.disguised != 0 ? "DisguiseStarted" : event.success != 0 ? "DisguiseRevealedSuccess" : "DisguiseRevealedFailure");
			if (!sound.empty() && sound != "NoSound")
				sounds.pending.push_back({std::string(sound), at(event.position)});
		});
	// Deaths' own sounds (CrushDie's crush sounds, EjectPilotDie's voice), for their player.
	const auto *roster = view.World().FindResource<engine::gameplay::TeamRoster>();
	for (const auto &event : view.DeathEvents())
		if (event.kind == engine::gameplay::DeathEffectKind::Sound)
			sounds.pending.push_back({std::string(view.DeathEffectName(event.kind, event.id)),
				{Engine::Math::ToFloat(event.position.x), Engine::Math::ToFloat(event.position.y), Engine::Math::ToFloat(event.position.z)},
				roster != nullptr && event.team != engine::gameplay::NoTeam && event.team < roster->TeamCount() ? roster->TeamAt(event.team).owner
																												 : SoundRequest::NoOwner});
	// Trains (RailroadBehavior): whistles, clickety-clacks at the car's speed / 10 (a setVolume of what it was), impacts
	// for their victims' players.
	if (const auto *trains = view.World().FindResource<generalszh::gameplay::RailroadCues>())
		for (const generalszh::gameplay::RailroadCue &cue : trains->list)
		{
			SoundRequest request{cue.sound, at(cue.at), cue.player};
			if (cue.kind != generalszh::gameplay::RailroadCue::Kind::Whistle)
				request.volume = std::max(0.0f, Engine::Math::ToFloat(cue.volume));
			sounds.pending.push_back(std::move(request));
		}
	// Sticky bombs (StickyBombUpdate): a bomb's StickyBombCreated where it was stuck on, its UnitBombPing each second.
	if (const auto *bombs = view.World().FindResource<generalszh::gameplay::StickyBombCues>())
		for (const generalszh::gameplay::StickyBombCue &cue : bombs->list)
		{
			if (cue.kind == generalszh::gameplay::StickyBombCue::Kind::Effect)
				continue;
			const std::string_view sound =
				view.Definition(cue.definition).Sound(cue.kind == generalszh::gameplay::StickyBombCue::Kind::Created ? "StickyBombCreated" : "UnitBombPing");
			if (!sound.empty() && sound != "NoSound")
				sounds.pending.push_back({std::string(sound), at(cue.at), cue.player});
		}
	// Payload carriers (DeliverPayloadAIUpdate::update): a dive's StartDive (UnitSpecificSounds) where it starts.
	if (const auto *runs = view.World().FindResource<engine::gameplay::DeliveryCues>())
		runs->ForEach([&](const engine::gameplay::DeliveryCue &cue) {
			if (cue.kind != engine::gameplay::DeliveryCue::Kind::StartDive)
				return;
			const std::string_view sound = view.Definition(cue.definition).Sound("StartDive");
			if (!sound.empty() && sound != "NoSound")
				sounds.pending.push_back({std::string(sound), at(cue.at)});
		});
	// Bridges (BridgeBehavior::onBodyDamageStateChange): DamagedToSound / RepairedToSound where the bridge stands.
	if (const auto *bridges = view.World().FindResource<generalszh::gameplay::BridgeCues>())
		for (const generalszh::gameplay::BridgeCue &cue : bridges->list)
			if (cue.sound)
				sounds.pending.push_back({cue.name, at(cue.at)});
	// Hackers (HackInternetStateMachine): UnitUnpack, UnitPack and UnitCashPing on them.
	if (const auto *hacks = view.World().FindResource<generalszh::gameplay::HackCues>())
		for (const generalszh::gameplay::HackCue &cue : hacks->list)
		{
			using Kind = generalszh::gameplay::HackCue::Kind;
			const std::string_view sound =
				view.Definition(cue.definition).Sound(cue.kind == Kind::Unpack ? "UnitUnpack" : cue.kind == Kind::Pack ? "UnitPack" : "UnitCashPing");
			if (!sound.empty() && sound != "NoSound")
				sounds.pending.push_back({std::string(sound), at(cue.at)});
		}
	// Strategy Centers (BattlePlanUpdate::setStatus): unpacking, the plan's announcement there.
	if (const auto *plans = view.World().FindResource<generalszh::gameplay::BattlePlanCues>())
		for (const generalszh::gameplay::BattlePlanCue &cue : plans->list)
		{
			using Kind = generalszh::gameplay::BattlePlanCue::Kind;
			if ((cue.kind != Kind::Unpack && cue.kind != Kind::Pack) || cue.plan == generalszh::gameplay::PlanStatus::None)
				continue;
			const content::ObjectDefinition &kind = view.Definition(cue.definition);
			const auto module = std::find_if(kind.modules.begin(), kind.modules.end(),
				[](const content::ModuleEntry &entry) { return entry.block != nullptr && entry.type == "BattlePlanUpdate"; });
			if (module == kind.modules.end())
				continue;
			const auto field = [&](std::string_view key) -> std::string {
				const auto *node = module->block->Find(key);
				return node != nullptr && !node->values.empty() ? std::string(node->Value()) : std::string{};
			};
			constexpr std::array<std::string_view, 3> names{"Bombardment", "HoldTheLine", "SearchAndDestroy"};
			const std::string name(names[static_cast<std::size_t>(cue.plan) - 1]);
			const auto play = [&](const std::string &sound) {
				if (!sound.empty() && sound != "NoSound")
					sounds.pending.push_back({sound, at(cue.at)});
			};
			// Its unpack and pack sounds ride on it, stopped as their status ends (BattlePlanSoundSystem); the announcement
			// is heard whole.
			if (cue.kind == Kind::Unpack)
				play(field(name + "AnnouncementName"));
		}
	// Parachutes opening (ParachuteContain::update): their ParachuteOpenSound, on the rider.
	if (const auto *openings = view.World().FindResource<engine::gameplay::ParachuteOpenings>())
		for (const engine::gameplay::ParachuteOpening &opening : openings->opened)
		{
			const auto chute = view.DefinitionOf(opening.chute);
			if (!chute)
				continue;
			const auto &parachutes = view.Content().parachutes;
			const auto found = parachutes.find(view.Definition(*chute).name);
			if (found == parachutes.end() || found->second.openSound.empty())
				continue;
			const auto *owner = view.World().IsAlive(opening.rider) ? view.World().Get<engine::gameplay::Owner>(opening.rider) : nullptr;
			sounds.pending.push_back({found->second.openSound, at(opening.position), owner != nullptr ? owner->player : SoundRequest::NoOwner});
		}
	// Thrown pieces' bounce sounds each time they land (PhysicsBehavior::doBounceSound), on the piece.
	const auto bounce = [&](const engine::gameplay::Landing &landing) {
		const std::string_view sound = view.DeathEffectName(engine::gameplay::DeathEffectKind::Sound, landing.sound);
		if (sound.empty())
			return;
		const auto *owner = view.World().IsAlive(landing.entity) ? view.World().Get<engine::gameplay::Owner>(landing.entity) : nullptr;
		sounds.pending.push_back({std::string(sound), at(landing.position), owner != nullptr ? owner->player : SoundRequest::NoOwner});
	};
	if (const auto *landings = view.World().FindResource<engine::gameplay::Landings>())
		landings->ForEach(bounce);
	// And flyers', whose locomotor steps their bodies.
	if (const auto *flown = view.World().FindResource<engine::gameplay::LocomotorLandings>())
		flown->ForEach(bounce);
}
}
