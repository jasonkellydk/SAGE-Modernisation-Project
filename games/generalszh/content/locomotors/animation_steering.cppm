export module games.generalszh.content.locomotors.animation_steering;
import std;

export import games.generalszh.content.objects.object_definition;
export import engine.time.simulation_time;
import engine.config.binding.values;

// AnimationSteeringUpdate: a vehicle that shows its turns (a bike leaning into
// them), changing its turn look at most once every MinTransitionTime (ms, in
// whole ticks rounded up).
export namespace generalszh::content
{
inline std::optional<std::uint64_t> ReadAnimationSteering(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
		if (module.block != nullptr && module.type == "AnimationSteeringUpdate")
		{
			std::int64_t milliseconds = 0;
			if (const auto *node = module.block->Find("MinTransitionTime"))
				milliseconds = engine::config::values::ParseInt(node->Value()).value_or(0);
			return milliseconds <= 0 ? 0u : static_cast<std::uint64_t>((milliseconds * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
		}
	return std::nullopt;
}
}
