export module games.generalszh.presentation.composition.audio;
import std;

export import engine.ecs.core.world;
export import games.generalszh.presentation.audio.resources.audio_resources;
import games.generalszh.presentation.audio.components.sound_loops;

// What the player hears: the presentation's composition of which sound resources it keeps in the simulation's world
// (the content, the player and the mixer; what plays and the listener) and what goes with an object that goes.
export namespace generalszh::presentation::composition
{
inline void EmplaceAudioResources(ecs::World &world, const AudioHandle &audio)
{
	world.EmplaceResource<AudioHandle>(audio);
	world.EmplaceResource<AudioState>();
	world.EmplaceResource<AudioCommands>();
	world.EmplaceResource<ListenerPose>();
}

// A thing that goes stops its sounds: its ambient, movement, burning, crashing and turret loops, its weapon's fire
// loop, an uplink's sounds (killEverything).
inline void BindSoundReleases(ecs::World &world, engine::audio::SoundPlayer *sounds)
{
	world.Side<SoundLoops>().OnRemove([sounds](ecs::Entity, SoundLoops &loops) {
		for (const auto handle : {loops.ambient, loops.move, loops.burning, loops.crashing, loops.turret})
			if (handle != 0 && sounds != nullptr)
				sounds->Stop(handle);
	});
	world.Side<FireSoundLoop>().OnRemove([sounds](ecs::Entity, FireSoundLoop &loop) {
		if (loop.handle != 0 && sounds != nullptr)
			sounds->Stop(loop.handle);
	});
	world.Side<UplinkSounds>().OnRemove([sounds](ecs::Entity, UplinkSounds &loops) {
		for (const auto handle : loops.handles)
			if (handle != 0 && sounds != nullptr)
				sounds->Stop(handle);
	});
}
}
