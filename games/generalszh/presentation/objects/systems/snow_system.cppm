export module games.generalszh.presentation.objects.systems.snow_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.effects.snow;
export import engine.gameplay.common.identity.components.definition_ref;
export import games.generalszh.presentation.objects.resources.presentation_resources;

// Once a frame, on the frame's game time: the snow's clock runs on (W3DSnowManager::update, from GameClient::update),
// whether or not the snow shows.
export namespace generalszh::presentation
{
struct SnowSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Write<SnowField>>;

	void Execute(ecs::SystemContext &context) const { AdvanceSnow(context.Write<SnowField>(), context.Read<PresentationFrame>().seconds); }
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::SnowSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.snow";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
