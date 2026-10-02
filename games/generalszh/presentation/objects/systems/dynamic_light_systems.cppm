export module games.generalszh.presentation.objects.systems.dynamic_light_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.off_map;
export import games.generalszh.presentation.objects.components.object_presentation;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.dynamic_lights;
import games.generalszh.presentation.objects.algorithms.model_state_changes;

// The frame's dynamic lights, after the FX have played: the light pulses age
// by the frame's real time (in frames of 1/30 s, as the original's per-render-
// frame update) and those still lit are shown; each police car shows its light
// bar where it is drawn, coloured by its light-bar clip's frame.
export namespace generalszh::presentation
{
struct DynamicLightSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Exclude<engine::gameplay::OffMap>>;
	using SideTables = ecs::SideTables<ecs::Read<TickPose>, ecs::Read<ShownLook>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<LookCatalog>, ecs::Read<LookClips>, ecs::Write<LightPulses>,
		ecs::Write<DynamicLights>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const LookClips &clips = context.Read<LookClips>();
		auto &pulses = context.Write<LightPulses>().live;
		auto &shown = context.Write<DynamicLights>().shown;
		shown.clear();
		// W3DDynamicLight::On_Frame_Update runs once a rendered frame (the scene's update list), not once a logic frame:
		// a pulse ages by real time (the original's 30 frames a second), on while the game is paused or slowed.
		const float frames = frame.realSeconds * 30.0f;
		std::erase_if(pulses, [&](LightPulse &pulse) {
			pulse.age += frames;
			const auto light = ShowPulse(pulse);
			if (light)
				shown.push_back(*light);
			return !light;
		});
		const auto &poses = context.SideRead<SideTables, TickPose>();
		const auto &looks = context.SideRead<SideTables, ShownLook>();
		query.ForEachChunk([&](auto chunk) {
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < definitions.size(); ++row)
			{
				const DefinitionLooks *definition = catalog.Of(definitions[row].index);
				if (definition == nullptr || !definition->policeLights)
					continue;
				const TickPose *pose = poses.Get(entities[row]);
				const ShownLook *look = looks.Get(entities[row]);
				if (pose == nullptr || look == nullptr || look->shown >= definition->states.states.size())
					continue;
				const double at = ShownClipFrame(*look, clips.At(look->look), definition->states.states[look->shown].animationMode, frame.clock);
				if (at < 0.0)
					continue;
				std::array<float, 3> position{};
				for (std::size_t axis = 0; axis < 3; ++axis)
					position[axis] = pose->previous[axis] + (pose->current[axis] - pose->previous[axis]) * frame.alpha;
				shown.push_back(PoliceLight(position, static_cast<float>(at)));
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::DynamicLightSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.dynamic_lights";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
