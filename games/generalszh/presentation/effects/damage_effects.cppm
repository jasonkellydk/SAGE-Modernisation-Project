export module games.generalszh.presentation.effects.damage_effects;
import std;

export import games.generalszh.presentation.effects.effects_content;
export import games.generalszh.content.objects.object_definition;
export import engine.effects.particles.simulation.particle_world;
import games.generalszh.content.combat.combat_catalog;

// What an object shows for its body's damage state, as content (the
// original's TransitionDamageFX): per state, the FX lists played and the
// particle systems started on getting worse into it, each at a bone of the
// object's model (one bone, or a random one of a numbered family: Smoke01,
// Smoke02, ...) or at a fixed offset. The damage effect system plays them.
export namespace generalszh::presentation
{
enum class DamageLevel : std::uint8_t
{
	Pristine,
	Damaged,
	ReallyDamaged,
	Rubble,
	Count,
};

struct DamageEffectPlacement
{
	std::string bone; // empty: at `offset`
	bool randomBone{false};
	std::array<float, 3> offset{};
};

struct DamageEffect
{
	enum class Kind : std::uint8_t
	{
		FxList,
		Particles,
	};
	Kind kind{Kind::FxList};
	std::string name;
	DamageEffectPlacement placement;
	// The damage types whose blow may show it (its module's DamageFXTypes / DamageParticleTypes: the object's last damage).
	std::uint64_t types{~std::uint64_t{0}};
};

struct DamageEffects
{
	std::array<std::vector<DamageEffect>, static_cast<std::size_t>(DamageLevel::Count)> byLevel;

	const std::vector<DamageEffect> &At(DamageLevel level) const { return byLevel[static_cast<std::size_t>(level)]; }
	bool Empty() const noexcept
	{
		return std::all_of(byLevel.begin(), byLevel.end(), [](const auto &effects) { return effects.empty(); });
	}
};

namespace damage_effects_detail
{
bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}

bool StartsWith(std::string_view text, std::string_view prefix)
{
	return text.size() >= prefix.size() && Same(text.substr(0, prefix.size()), prefix);
}

// "Bone:Smoke RandomBone:Yes PSys:Name" or "Loc: X:0 Y:0 Z:0 FXList:Name".
std::optional<DamageEffect> Parse(DamageEffect::Kind kind, const engine::config::Node &node)
{
	DamageEffect effect;
	effect.kind = kind;
	const std::string_view nameKey = kind == DamageEffect::Kind::FxList ? "FXList:" : "PSys:";
	const auto value = [](std::string_view token, std::size_t skip) { return token.substr(skip); };
	for (std::size_t index = 0; index < node.values.size(); ++index)
	{
		const std::string_view token = node.values[index];
		const auto next = [&]() { return index + 1 < node.values.size() ? node.values[index + 1] : std::string_view{}; };
		const auto number = [](std::string_view text) {
			float parsed = 0.0f;
			try
			{
				parsed = std::stof(std::string(text));
			}
			catch (...)
			{
			}
			return parsed;
		};
		if (StartsWith(token, "Bone:"))
			effect.placement.bone = std::string(token.size() > 5 ? value(token, 5) : next());
		else if (StartsWith(token, "RandomBone:"))
			effect.placement.randomBone = Same(token.size() > 11 ? value(token, 11) : next(), "Yes");
		else if (StartsWith(token, "X:"))
			effect.placement.offset[0] = number(value(token, 2));
		else if (StartsWith(token, "Y:"))
			effect.placement.offset[1] = number(value(token, 2));
		else if (StartsWith(token, "Z:"))
			effect.placement.offset[2] = number(value(token, 2));
		else if (StartsWith(token, nameKey))
			effect.name = std::string(token.size() > nameKey.size() ? value(token, nameKey.size()) : next());
	}
	if (effect.name.empty() || Same(effect.name, "None"))
		return std::nullopt;
	return effect;
}
}

DamageEffects ReadDamageEffects(const content::ObjectDefinition &object)
{
	using namespace damage_effects_detail;
	DamageEffects effects;
	constexpr std::array<std::string_view, 4> levels{"Pristine", "Damaged", "ReallyDamaged", "Rubble"};
	for (const content::ModuleEntry &module : object.modules)
	{
		if (module.slot != content::ModuleSlot::Behavior || module.block == nullptr || module.type != "TransitionDamageFX")
			continue;
		// INI::parseDamageTypeFlags: ALL (the default), NONE, +TYPE, -TYPE.
		const auto flags = [&](std::string_view key) {
			std::uint64_t mask = ~std::uint64_t{0};
			if (const engine::config::Node *node = module.block->Find(key))
				for (const std::string_view token : node->values)
				{
					if (Same(token, "ALL"))
						mask = ~std::uint64_t{0};
					else if (Same(token, "NONE"))
						mask = 0;
					else if (!token.empty() && (token[0] == '+' || token[0] == '-'))
						if (const auto type = content::DamageTypeIndex(token.substr(1)); type && *type < 64)
							mask = token[0] == '+' ? mask | (std::uint64_t{1} << *type) : mask & ~(std::uint64_t{1} << *type);
				}
			return mask;
		};
		const std::uint64_t fxTypes = flags("DamageFXTypes"), particleTypes = flags("DamageParticleTypes");
		for (const engine::config::Node &field : module.block->children)
			for (std::size_t level = 0; level < levels.size(); ++level)
			{
				if (!StartsWith(field.key, levels[level]))
					continue;
				const std::string_view rest = std::string_view(field.key).substr(levels[level].size());
				std::optional<DamageEffect> effect;
				if (StartsWith(rest, "FXList"))
					effect = Parse(DamageEffect::Kind::FxList, field);
				else if (StartsWith(rest, "ParticleSystem"))
					effect = Parse(DamageEffect::Kind::Particles, field);
				if (effect)
				{
					effect->types = effect->kind == DamageEffect::Kind::FxList ? fxTypes : particleTypes;
					effects.byLevel[level].push_back(std::move(*effect));
				}
				break;
			}
	}
	return effects;
}

}
