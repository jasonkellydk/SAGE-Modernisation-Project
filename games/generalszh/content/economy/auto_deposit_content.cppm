export module games.generalszh.content.economy.auto_deposit_content;
import std;

export import games.generalszh.content.objects.object_definition;
export import engine.time.simulation_time;
import engine.config.binding.values;

// An AutoDepositUpdate (AutoDepositUpdateModuleData, defaults as there): DepositTiming (a duration, in ticks),
// DepositAmount, InitialCaptureBonus, ActualMoney (Yes) and its UpgradedBoost pairs ("UpgradeType:<name> Boost:<n>").
export namespace generalszh::content
{
struct AutoDepositContent
{
	struct Boost
	{
		std::string upgrade;
		std::int64_t amount{0};
	};
	std::uint64_t periodTicks{0};
	std::int64_t amount{0};
	std::int64_t captureBonus{0};
	bool actualMoney{true};
	std::vector<Boost> boosts;
};

inline std::optional<AutoDepositContent> ReadAutoDeposit(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "AutoDepositUpdate")
			continue;
		const engine::config::Node &block = *module.block;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		AutoDepositContent deposit;
		const auto integer = [&](std::string_view key) -> std::int64_t {
			const auto *node = block.Find(key);
			return node != nullptr ? engine::config::values::ParseInt(node->Value()).value_or(0) : 0;
		};
		if (const auto *node = block.Find("DepositTiming"))
			deposit.periodTicks = engine::config::ReadDurationTicks(*node, bind).value_or(0);
		deposit.amount = integer("DepositAmount");
		deposit.captureBonus = integer("InitialCaptureBonus");
		if (const auto *node = block.Find("ActualMoney"))
			deposit.actualMoney = engine::config::values::ParseBool(node->Value()).value_or(true);
		// parseUpgradePair: its tokens split on whitespace and colons.
		for (const engine::config::Node &child : block.children)
		{
			if (child.key != "UpgradedBoost")
				continue;
			std::string text(child.text);
			std::ranges::replace(text, ':', ' ');
			std::istringstream words(text);
			std::string label, upgrade, boostLabel, amount;
			words >> label >> upgrade >> boostLabel >> amount;
			if (label == "UpgradeType" && boostLabel == "Boost")
				deposit.boosts.push_back({upgrade, engine::config::values::ParseInt(amount).value_or(0)});
		}
		return deposit;
	}
	return std::nullopt;
}
}
