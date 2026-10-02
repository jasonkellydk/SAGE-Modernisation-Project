export module games.generalszh.content.railroad.railroad_content;
import std;

export import games.generalszh.content.objects.object_definition;
export import Engine.Core.Math.Fixed;
export import engine.config.binding.schema;

// RailroadBehavior's module data (RailroadGuideAIUpdate.h's field table; the constructor's defaults): whether it
// pulls the train (IsLocomotive), what it pulls (CarriageTemplateName, repeatable, in order), the speed below which what
// walks or boards past it is spared (RunningGarrisonSpeedMax), the speed at which it kills what it hits (KillSpeedMin),
// its top speed a frame (SpeedMax), its speeding up, braking and coasting (multipliers a frame), how long it waits at a
// station (WaitAtStationTime: milliseconds up to whole frames; 150 frames unless given) and its sounds. PathPrefixName and
// CrashFXTemplateName are read and never used, as in the original.
export namespace generalszh::content
{
struct RailroadContent
{
	bool locomotive{false};
	std::vector<std::string> carriages;
	Engine::Math::Fixed runningGarrisonSpeedMax{Engine::Math::Fixed::One()};
	Engine::Math::Fixed killSpeedMin{Engine::Math::Fixed::One()};
	Engine::Math::Fixed speedMax{Engine::Math::Fixed::FromInt(4)};
	Engine::Math::Fixed acceleration{Engine::Math::Fixed::FromRatio(101, 100)};
	Engine::Math::Fixed braking{Engine::Math::Fixed::FromRatio(99, 100)};
	Engine::Math::Fixed friction{Engine::Math::Fixed::FromRatio(97, 100)};
	std::uint32_t waitAtStationTicks{150};
	std::string runningSound;
	std::string clicketyClackSound;
	std::string whistleSound;
	std::string bigMetalBounceSound;
	std::string smallMetalBounceSound;
	std::string meatyBounceSound;
};

inline std::optional<RailroadContent> ReadRailroad(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "RailroadBehavior")
			continue;
		RailroadContent out;
		for (const engine::config::Node &field : module.block->children)
		{
			if (field.values.empty())
				continue;
			const std::string_view key = field.key;
			const auto real = [&](Engine::Math::Fixed &into) { into = engine::config::values::ParseFixed(field.Value()).value_or(into); };
			if (key == "IsLocomotive")
				out.locomotive = engine::config::values::ParseBool(field.Value()).value_or(false);
			else if (key == "CarriageTemplateName")
				out.carriages.emplace_back(field.Value());
			else if (key == "RunningGarrisonSpeedMax")
				real(out.runningGarrisonSpeedMax);
			else if (key == "KillSpeedMin")
				real(out.killSpeedMin);
			else if (key == "SpeedMax")
				real(out.speedMax);
			else if (key == "Acceleration")
				real(out.acceleration);
			else if (key == "Braking")
				real(out.braking);
			else if (key == "Friction")
				real(out.friction);
			else if (key == "WaitAtStationTime")
			{
				// INI::parseDurationUnsignedInt: ceilf(ms * frames a second / 1000).
				const std::int64_t ms = std::max<std::int64_t>(0, engine::config::values::ParseInt(field.Value()).value_or(0));
				out.waitAtStationTicks = static_cast<std::uint32_t>((ms * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
			}
			else if (key == "RunningSound")
				out.runningSound = std::string(field.Value());
			else if (key == "ClicketyClackSound")
				out.clicketyClackSound = std::string(field.Value());
			else if (key == "WhistleSound")
				out.whistleSound = std::string(field.Value());
			else if (key == "BigMetalBounceSound")
				out.bigMetalBounceSound = std::string(field.Value());
			else if (key == "SmallMetalBounceSound")
				out.smallMetalBounceSound = std::string(field.Value());
			else if (key == "MeatyBounceSound")
				out.meatyBounceSound = std::string(field.Value());
		}
		return out;
	}
	return std::nullopt;
}
}
