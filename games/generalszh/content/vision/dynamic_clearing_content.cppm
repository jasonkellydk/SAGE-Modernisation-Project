export module games.generalszh.content.vision.dynamic_clearing_content;
import std;

export import engine.gameplay.rts.vision.definitions.dynamic_clearing;
export import games.generalszh.content.objects.object_definition;

// DynamicShroudClearingRangeUpdateModuleData: ChangeInterval, GrowInterval, ShrinkDelay, ShrinkTime, GrowDelay and
// GrowTime (parseDurationUnsignedInt: milliseconds to ticks) and FinalVision. Its GridDecalTemplate is the
// presentation's (the grid decals drawn while it grows).
export namespace generalszh::content
{
inline std::optional<engine::gameplay::DynamicClearingDefinition> ReadDynamicClearing(const ObjectDefinition &object, engine::time::FixedStep step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "DynamicShroudClearingRangeUpdate")
			continue;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		engine::gameplay::DynamicClearingDefinition how;
		for (const engine::config::Node &field : module.block->children)
		{
			const std::string_view key = field.key;
			const auto ticks = [&] { return static_cast<std::uint32_t>(engine::config::ReadDurationTicks(field, bind).value_or(0)); };
			if (key == "ChangeInterval")
				how.changeInterval = ticks();
			else if (key == "GrowInterval")
				how.growInterval = ticks();
			else if (key == "ShrinkDelay")
				how.shrinkDelay = ticks();
			else if (key == "ShrinkTime")
				how.shrinkTime = ticks();
			else if (key == "GrowDelay")
				how.growDelay = ticks();
			else if (key == "GrowTime")
				how.growTime = ticks();
			else if (key == "FinalVision")
				how.finalVision = engine::config::ReadFixed(field, bind).value_or(Engine::Math::Fixed{});
		}
		return how;
	}
	return std::nullopt;
}
}
