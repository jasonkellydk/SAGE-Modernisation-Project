export module games.generalszh.content.combat.damage_fx_content;
import std;

export import engine.config.document.document;
export import engine.time.simulation_time;
export import Engine.Core.Math.Fixed;
import engine.config.binding.values;
import games.generalszh.content.combat.combat_catalog;
export import games.generalszh.content.objects.object_definition;

// DamageFX.ini: what a hit shows and sounds like, per damage type and the
// attacker's veterancy (REGULAR, VETERAN, ELITE, HEROIC): the major FX list
// for a hit of at least AmountForMajorFX, else the minor one, and how long
// (ThrottleTime, in milliseconds up to whole ticks) the same damage type
// waits before showing again on the same object. "DEFAULT" sets every
// damage type; the plain fields set every level, the Veterancy ones one.
// A named FX list of "None" is none. The object's ArmorSet names its DamageFX.
export namespace generalszh::content
{
struct DamageFxEntry
{
	Engine::Math::Fixed majorAmount; // AmountForMajorFX (0: every hit is major)
	std::string major;
	std::string minor;
	std::uint64_t throttleTicks{0};
};

inline constexpr std::array<std::string_view, 4> VeterancyLevelNames{"REGULAR", "VETERAN", "ELITE", "HEROIC"};

struct DamageFxTable
{
	std::array<std::array<DamageFxEntry, 4>, DamageTypeNames.size()> byType{};

	const DamageFxEntry *At(std::uint32_t damageType, std::uint32_t level) const noexcept
	{
		return damageType < byType.size() && level < 4 ? &byType[damageType][level] : nullptr;
	}

	// DamageFX::getDamageFXList: none for no damage; major at or above the amount, else minor.
	const std::string *FxFor(std::uint32_t damageType, std::uint32_t level, Engine::Math::Fixed amount) const noexcept
	{
		const DamageFxEntry *entry = At(damageType, level);
		if (entry == nullptr || amount == Engine::Math::Fixed{})
			return nullptr;
		const std::string &fx = amount >= entry->majorAmount ? entry->major : entry->minor;
		return fx.empty() ? nullptr : &fx;
	}
};

using DamageFxTables = std::map<std::string, DamageFxTable, std::less<>>;

namespace damage_fx_detail
{
inline bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y)); });
}

inline std::optional<std::uint32_t> Level(std::string_view name)
{
	for (std::uint32_t level = 0; level < VeterancyLevelNames.size(); ++level)
		if (Same(name, VeterancyLevelNames[level]))
			return level;
	return std::nullopt;
}
}

// An object's ArmorSets as DamageFX sees them: each set's armor and DamageFX (a later set with the same conditions
// replaces an earlier one, as the defaults' sets give way to the object's own; the set with no conditions first).
struct ArmorSetDamageFx
{
	std::string armor;
	std::string damageFx;
	bool unconditioned{false};
	std::string conditions;
};

std::vector<ArmorSetDamageFx> ReadArmorSetDamageFx(const ObjectDefinition &object)
{
	using namespace damage_fx_detail;
	std::vector<ArmorSetDamageFx> sets;
	for (const engine::config::Node *set : object.armorSets)
	{
		if (set == nullptr)
			continue;
		ArmorSetDamageFx entry;
		if (const auto *armor = set->Find("Armor"))
			entry.armor = std::string(armor->Value());
		if (const auto *fx = set->Find("DamageFX"); fx != nullptr && !Same(fx->Value(), "None"))
			entry.damageFx = std::string(fx->Value());
		const auto *conditions = set->Find("Conditions");
		entry.unconditioned = conditions == nullptr || conditions->values.empty() || Same(conditions->Value(), "None");
		entry.conditions = entry.unconditioned ? std::string{} : std::string(conditions->text);
		const auto same = std::find_if(sets.begin(), sets.end(), [&](const ArmorSetDamageFx &other) { return other.conditions == entry.conditions; });
		if (same != sets.end())
			*same = std::move(entry);
		else
			sets.push_back(std::move(entry));
	}
	std::stable_partition(sets.begin(), sets.end(), [](const ArmorSetDamageFx &set) { return set.unconditioned; });
	return sets;
}

DamageFxTables BindDamageFx(const engine::config::Document &document, const engine::time::FixedStep &step)
{
	using namespace damage_fx_detail;
	DamageFxTables tables;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "DamageFX" || root.values.empty())
			continue;
		DamageFxTable &table = tables[std::string(root.Value())];
		table = {};
		for (const engine::config::Node &field : root.children)
		{
			const bool veterancy = field.key.starts_with("Veterancy");
			const std::string_view key = veterancy ? field.key.substr(9) : field.key;
			std::size_t at = 0;
			std::uint32_t firstLevel = 0, lastLevel = 3;
			if (veterancy)
			{
				const auto level = Level(field.Value(at++));
				if (!level)
					continue;
				firstLevel = lastLevel = *level;
			}
			const std::string_view typeName = field.Value(at++);
			std::uint32_t firstType = 0, lastType = static_cast<std::uint32_t>(DamageTypeNames.size() - 1);
			if (!Same(typeName, "Default"))
			{
				const auto type = DamageTypeIndex(typeName);
				if (!type)
					continue;
				firstType = lastType = *type;
			}
			const std::string_view value = field.Value(at);
			const auto apply = [&](auto &&set) {
				for (std::uint32_t type = firstType; type <= lastType; ++type)
					for (std::uint32_t level = firstLevel; level <= lastLevel; ++level)
						set(table.byType[type][level]);
			};
			if (key == "AmountForMajorFX")
			{
				if (const auto amount = engine::config::values::ParseFixed(value))
					apply([&](DamageFxEntry &entry) { entry.majorAmount = *amount; });
			}
			else if (key == "MajorFX" || key == "MinorFX")
			{
				const std::string fx = Same(value, "None") ? std::string{} : std::string(value);
				const bool major = key == "MajorFX";
				apply([&](DamageFxEntry &entry) { (major ? entry.major : entry.minor) = fx; });
			}
			else if (key == "ThrottleTime")
			{
				// INI::parseDurationUnsignedInt: milliseconds up to whole ticks.
				if (const auto ms = engine::config::values::ParseFixed(value); ms && *ms >= Engine::Math::Fixed{})
				{
					const auto numerator = static_cast<std::uint64_t>(ms->Raw()) * step.TicksPerSecond();
					const std::uint64_t denominator = std::uint64_t{1000} << Engine::Math::Fixed::FractionBits;
					const std::uint64_t ticks = (numerator + denominator - 1) / denominator;
					apply([&](DamageFxEntry &entry) { entry.throttleTicks = ticks; });
				}
			}
		}
	}
	return tables;
}
}
