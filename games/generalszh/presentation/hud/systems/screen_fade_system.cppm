export module games.generalszh.presentation.hud.systems.screen_fade_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.hud.algorithms.screen_fade_steps;
export import engine.gameplay.common.identity.components.owner;

// Once a tick: the screen fade steps (ScriptEngine::update's updateFades).
export namespace generalszh::presentation
{
struct ScreenFadeSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using Resources = ecs::Resources<ecs::Write<ScreenFade>>;

	void Execute(ecs::SystemContext &context) const { StepFadeOnTick(context.Write<ScreenFade>(), context.Tick()); }
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::ScreenFadeSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.screen_fade";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
