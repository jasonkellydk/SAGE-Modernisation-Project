export module games.generalszh.presentation.composition.schedules;
import std;

export import engine.ecs.system.system;
import games.generalszh.presentation.objects.algorithms.presentation_schedule;
import games.generalszh.presentation.audio.algorithms.audio_schedule;
import games.generalszh.presentation.composition.objects;
import games.generalszh.presentation.composition.hud;
import games.generalszh.presentation.composition.interaction;

// The presentation's two schedules over the simulation's world, every domain's systems in them: once a tick (the
// drawables' updates, the interface's) and once a frame (the drawing's preparation, sound, the interaction).
export namespace generalszh::presentation::composition
{
// In the order they were always registered (it is their commit order).
inline void RegisterPresentationSystems(ecs::SystemRegistry &tick, ecs::SystemRegistry &frame)
{
	RegisterPresentationTick(tick);
	RegisterPresentationFrame(frame);
	RegisterProjectileStreams(frame);
	RegisterFirestorms(frame);
	RegisterBoneFx(tick, frame);
	RegisterSoundFrame(frame);
	RegisterHudTickSystems(tick);
	RegisterInteractionFrameSystems(frame);
	RegisterObjectFrameSystems(frame);
}
}
