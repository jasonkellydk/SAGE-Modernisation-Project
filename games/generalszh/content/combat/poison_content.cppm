export module games.generalszh.content.combat.poison_content;
import std;

export import engine.gameplay.common.poison.components.poison;
export import games.generalszh.content.objects.object_definition;
export import engine.time.simulation_time;
import games.generalszh.content.combat.combat_catalog;
import engine.config.binding.values;

// PoisonedBehavior: poison (POISON damage) stays in the object, hurting it
// every PoisonDamageInterval for PoisonDuration (ms, whole frames rounded up)
// with UNRESISTABLE damage that shows POISON's effects.
export namespace generalszh::content
{
inline std::optional<engine::gameplay::Poison> ReadObjectPoison(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "PoisonedBehavior")
			continue;
		const auto ticks = [&](std::string_view key, std::int64_t fallback) {
			const auto *node = module.block->Find(key);
			const std::int64_t ms = node != nullptr ? engine::config::values::ParseInt(node->Value()).value_or(fallback) : fallback;
			return ms <= 0 ? std::uint64_t{0} : static_cast<std::uint64_t>((ms * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
		};
		engine::gameplay::Poison poison;
		poison.interval = std::max<std::uint64_t>(ticks("PoisonDamageInterval", 0), 1);
		poison.duration = ticks("PoisonDuration", 0);
		poison.catchType = DamageTypeIndex("POISON").value_or(0);
		poison.dealType = DamageTypeIndex("UNRESISTABLE").value_or(0);
		poison.fxType = poison.catchType;
		return poison;
	}
	return std::nullopt;
}
}
