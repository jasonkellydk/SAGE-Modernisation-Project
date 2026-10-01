export module games.generalszh.presentation.objects.systems.cloud_drift_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.objects.algorithms.cloud_drift;
export import games.generalszh.presentation.objects.resources.presentation_resources;
import engine.gameplay.common.spatial.components.transform;

// Once a frame, on the frame's time: the cloud shadows slide on (DriftClouds), while they show.
export namespace generalszh::presentation
{
struct CloudDriftSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Write<CloudLayer>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		CloudLayer &clouds = context.Write<CloudLayer>();
		if (clouds.enabled)
			DriftClouds(clouds, context.Read<PresentationFrame>().seconds);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::CloudDriftSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.cloud_drift";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
