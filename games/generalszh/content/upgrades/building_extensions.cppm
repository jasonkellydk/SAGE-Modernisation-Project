export module games.generalszh.content.upgrades.building_extensions;
import std;

export import games.generalszh.content.objects.object_definition;
export import engine.time.simulation_time;
export import Engine.Core.Math.Fixed;
import engine.config.binding.values;

// Parts a building extends when upgraded: RadarUpdate's dish (RadarExtendTime,
// a real duration: ms * 30 / 1000 frames, cut to whole frames as the original
// adds it to the frame number) and PowerPlantUpdate's control rods
// (RodsExtendTime, whole frames rounded up); and OverchargeBehavior's overcharge (HealthPercentToDrainPerSecond in
// percent points, NotAllowedWhenHealthBelowPercent as a share: parsePercentToReal).
export namespace generalszh::content
{
struct BuildingExtensions
{
	std::optional<std::uint64_t> radarTicks;
	std::optional<std::uint64_t> rodsTicks;
	struct Overcharge
	{
		Engine::Math::Fixed drainPercent;
		Engine::Math::Fixed floor;
	};
	std::optional<Overcharge> overcharge;
};

inline BuildingExtensions ReadBuildingExtensions(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	BuildingExtensions result;
	const auto milliseconds = [](const engine::config::Node *node) {
		return node != nullptr ? engine::config::values::ParseInt(node->Value()).value_or(0) : std::int64_t{0};
	};
	const auto perSecond = static_cast<std::int64_t>(step.TicksPerSecond());
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr)
			continue;
		if (module.type == "RadarUpdate")
			result.radarTicks = static_cast<std::uint64_t>(std::max<std::int64_t>(milliseconds(module.block->Find("RadarExtendTime")) * perSecond / 1000, 0));
		else if (module.type == "OverchargeBehavior")
		{
			const auto percent = [&](const char *key) {
				const engine::config::Node *node = module.block->Find(key);
				std::string_view text = node != nullptr ? node->Value() : std::string_view{};
				if (!text.empty() && text.back() == '%')
					text.remove_suffix(1);
				return engine::config::values::ParseFixed(text).value_or(Engine::Math::Fixed{});
			};
			result.overcharge = BuildingExtensions::Overcharge{percent("HealthPercentToDrainPerSecond"),
				percent("NotAllowedWhenHealthBelowPercent") / Engine::Math::Fixed::FromInt(100)};
		}
		else if (module.type == "PowerPlantUpdate")
		{
			const std::int64_t ms = milliseconds(module.block->Find("RodsExtendTime"));
			result.rodsTicks = ms <= 0 ? 0u : static_cast<std::uint64_t>((ms * perSecond + 999) / 1000);
		}
	}
	return result;
}
}
