export module games.generalszh.content.healing.healing_content;
import std;

export import engine.gameplay.common.healing.components.healing;
export import engine.gameplay.common.spatial.components.targetable;
export import games.generalszh.content.objects.object_definition;

// How an object heals, from its AutoHealBehavior module: itself (no radius),
// others in a radius, or all of its player's (AffectsWholePlayer); KindOf
// and ForbiddenKindOf become target classes. Only modules that start active
// count: upgrade-triggered ones (junk repair) wait for upgrades.
export namespace generalszh::content
{
struct ObjectHealing
{
	std::optional<engine::gameplay::SelfHealing> self;
	engine::gameplay::AreaHealing area; // no programs: not a healer
};

namespace healing_detail
{
bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}

// KindOf names to the engine's target classes.
std::uint32_t Classes(const engine::config::Node *node)
{
	namespace target = engine::gameplay::target_class;
	constexpr std::pair<std::string_view, std::uint32_t> kinds[] = {{"INFANTRY", target::Infantry}, {"VEHICLE", target::Vehicle},
		{"AIRCRAFT", target::Aircraft}, {"STRUCTURE", target::Structure}, {"PROJECTILE", target::Projectile}, {"MINE", target::Mine}};
	std::uint32_t classes = 0;
	if (node != nullptr)
		for (const std::string_view token : node->values)
			for (const auto &[name, bit] : kinds)
				if (Same(token, name))
					classes |= bit;
	return classes;
}
}

ObjectHealing ReadObjectHealing(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	using namespace healing_detail;
	ObjectHealing healing;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "AutoHealBehavior")
			continue;
		const engine::config::Node &block = *module.block;
		const auto *starts = block.Find("StartsActive");
		if (starts == nullptr || !engine::config::values::ParseBool(starts->Value()).value_or(false))
			continue;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		const auto ticks = [&](std::string_view key) -> std::uint64_t {
			const auto *node = block.Find(key);
			return node != nullptr ? engine::config::ReadDurationTicks(*node, bind).value_or(0) : 0;
		};
		const auto flag = [&](std::string_view key) {
			const auto *node = block.Find(key);
			return node != nullptr && engine::config::values::ParseBool(node->Value()).value_or(false);
		};
		const auto *amountNode = block.Find("HealingAmount");
		const Engine::Math::Fixed amount = amountNode != nullptr ? Engine::Math::Fixed::FromInt(engine::config::values::ParseInt(amountNode->Value()).value_or(0))
																 : Engine::Math::Fixed{};
		const std::uint64_t delay = std::max<std::uint64_t>(1, ticks("HealingDelay"));
		const auto *radiusNode = block.Find("Radius");
		std::string radiusText = radiusNode != nullptr ? std::string(radiusNode->Value()) : std::string{};
		if (!radiusText.empty() && (radiusText.back() == 'f' || radiusText.back() == 'F'))
			radiusText.pop_back(); // "100.0f"
		const Engine::Math::Fixed radius = engine::config::values::ParseFixed(radiusText).value_or(Engine::Math::Fixed{});
		const bool wholePlayer = flag("AffectsWholePlayer");
		if (radius <= Engine::Math::Fixed{} && !wholePlayer)
		{
			healing.self = engine::gameplay::SelfHealing{amount, delay, ticks("StartHealingDelay"), 0};
			continue;
		}
		engine::gameplay::AreaHealProgram area;
		area.amount = amount;
		area.radius = radius;
		area.delay = delay;
		const std::uint32_t kinds = Classes(block.Find("KindOf"));
		area.classes = kinds != 0 ? kinds : 0xFFFFFFFFu;
		area.forbiddenClasses = Classes(block.Find("ForbiddenKindOf"));
		area.flags = (flag("SkipSelfForHealing") ? engine::gameplay::area_healing::SkipSelf : 0u) |
			(flag("SingleBurst") ? engine::gameplay::area_healing::SingleBurst : 0u) | (wholePlayer ? engine::gameplay::area_healing::WholePlayer : 0u);
		healing.area.Add(area);
	}
	return healing;
}

// PropagandaTowerBehaviorModuleData: Radius (1 by default), DelayBetweenUpdates (a duration; 100 frames by default),
// HealPercentEachSecond / UpgradedHealPercentEachSecond (1% / 2% by default), PulseFX, UpgradeRequired,
// UpgradedPulseFX, AffectsSelf.
struct PropagandaTowerContent
{
	Engine::Math::Fixed radius{Engine::Math::Fixed::One()};
	std::uint64_t delay{100};
	Engine::Math::Fixed heal{Engine::Math::Fixed::FromRatio(1, 100)};
	Engine::Math::Fixed upgradedHeal{Engine::Math::Fixed::FromRatio(2, 100)};
	std::string pulseFX;
	std::string upgradedPulseFX;
	std::string upgrade;
	bool affectsSelf{false};
};

inline std::optional<PropagandaTowerContent> ReadPropagandaTower(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "PropagandaTowerBehavior")
			continue;
		const engine::config::Node &block = *module.block;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		PropagandaTowerContent tower;
		const auto text = [&](std::string_view key) -> std::string {
			const auto *node = block.Find(key);
			return node != nullptr && !node->Value().empty() && !healing_detail::Same(node->Value(), "None") ? std::string(node->Value()) : std::string{};
		};
		if (const auto *node = block.Find("Radius"))
			tower.radius = engine::config::values::ParseFixed(node->Value()).value_or(tower.radius);
		if (const auto *node = block.Find("DelayBetweenUpdates"))
			tower.delay = engine::config::ReadDurationTicks(*node, bind).value_or(tower.delay);
		if (const auto *node = block.Find("HealPercentEachSecond"))
			tower.heal = engine::config::ReadPercent(*node, bind).value_or(tower.heal);
		if (const auto *node = block.Find("UpgradedHealPercentEachSecond"))
			tower.upgradedHeal = engine::config::ReadPercent(*node, bind).value_or(tower.upgradedHeal);
		if (const auto *node = block.Find("AffectsSelf"))
			tower.affectsSelf = engine::config::values::ParseBool(node->Value()).value_or(false);
		tower.pulseFX = text("PulseFX");
		tower.upgradedPulseFX = text("UpgradedPulseFX");
		tower.upgrade = text("UpgradeRequired");
		return tower;
	}
	return std::nullopt;
}
}
