export module games.generalszh.presentation.objects.systems.tracer_systems;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.effects.tracers;
export import engine.gameplay.common.identity.components.definition_ref;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.systems.effect_attachment_systems;

// The frame's tracers, after the FX have played: each moves on and ages by the frame's game time (W3DTracerDraw's
// per-frame move and fade, on frame time); the expired go.
export namespace generalszh::presentation
{
struct TracerSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Write<Tracers>>;

	void Execute(ecs::SystemContext &context) const { context.Write<Tracers>().Advance(context.Read<PresentationFrame>().seconds); }
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::TracerSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.tracers";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<generalszh::presentation::FxPlaybackSystem>;
};
}
