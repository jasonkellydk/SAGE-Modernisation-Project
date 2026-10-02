export module games.generalszh.content.combat.emp_content;
import std;

export import games.generalszh.content.objects.object_definition;
export import engine.time.simulation_time;
export import Engine.Core.Math.Fixed;
import engine.config.binding.values;

// An EMP pulse's EMPUpdate (EMPUpdateModuleData, defaults as there): DisabledDuration, Lifetime and StartFadeTime
// (durations, in ticks), EffectRadius (200), DoesNotAffect (ALLIES spares allies), DoesNotAffectMyOwnBuildings; and
// for presentation StartScale, TargetScaleMin / Max, StartColor, EndColor, DisableFXParticleSystem,
// SparksPerCubicFoot (0.001).
export namespace generalszh::content
{
struct EmpPulseContent
{
	std::uint64_t disabledTicks{0};
	std::uint64_t lifeTicks{0};
	std::uint64_t fadeTicks{0};
	Engine::Math::Fixed radius{Engine::Math::Fixed::FromInt(200)};
	bool sparesAllies{false};
	bool sparesOwnBuildings{false};
	std::string sparks;
	Engine::Math::Fixed startScale{Engine::Math::Fixed::One()};
	Engine::Math::Fixed targetScaleMin{Engine::Math::Fixed::One()};
	Engine::Math::Fixed targetScaleMax{Engine::Math::Fixed::One()};
	std::array<std::uint8_t, 3> startColor{255, 255, 255};
	std::array<std::uint8_t, 3> endColor{0, 0, 0};
	std::string sparksPerCubicFoot{"0.001"}; // presentation's (a float there, as the original's): its text
};

inline std::optional<EmpPulseContent> ReadEmpPulse(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "EMPUpdate")
			continue;
		const engine::config::Node &block = *module.block;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		EmpPulseContent pulse;
		const auto ticks = [&](std::string_view key) -> std::uint64_t {
			const auto *node = block.Find(key);
			return node != nullptr ? engine::config::ReadDurationTicks(*node, bind).value_or(0) : 0;
		};
		pulse.disabledTicks = ticks("DisabledDuration");
		pulse.lifeTicks = ticks("Lifetime");
		pulse.fadeTicks = ticks("StartFadeTime");
		if (const auto *node = block.Find("EffectRadius"))
			pulse.radius = engine::config::values::ParseFixed(node->Value()).value_or(pulse.radius);
		if (const auto *node = block.Find("DoesNotAffect"))
			for (const std::string_view flag : node->values)
				pulse.sparesAllies = pulse.sparesAllies || flag == "ALLIES";
		if (const auto *node = block.Find("DoesNotAffectMyOwnBuildings"))
			pulse.sparesOwnBuildings = engine::config::values::ParseBool(node->Value()).value_or(false);
		if (const auto *node = block.Find("DisableFXParticleSystem"); node != nullptr && !node->Value().empty())
			pulse.sparks = std::string(node->Value());
		const auto scale = [&](std::string_view key, Engine::Math::Fixed &into) {
			if (const auto *node = block.Find(key))
				into = engine::config::values::ParseFixed(node->Value()).value_or(into);
		};
		scale("StartScale", pulse.startScale);
		scale("TargetScaleMin", pulse.targetScaleMin);
		scale("TargetScaleMax", pulse.targetScaleMax);
		const auto color = [&](std::string_view key, std::array<std::uint8_t, 3> &into) {
			if (const auto *node = block.Find(key))
				if (const auto rgb = engine::config::ReadRgb(*node, bind))
					into = {rgb->r, rgb->g, rgb->b};
		};
		color("StartColor", pulse.startColor);
		color("EndColor", pulse.endColor);
		if (const auto *node = block.Find("SparksPerCubicFoot"); node != nullptr && !node->Value().empty())
			pulse.sparksPerCubicFoot = std::string(node->Value());
		return pulse;
	}
	return std::nullopt;
}
}
