export module games.generalszh.content.effects.transition_damage_content;
import std;

export import games.generalszh.content.objects.object_definition;
export import games.generalszh.content.models.model_rigs;
export import games.generalszh.content.objects.model_states;
import games.generalszh.content.objects.model_conditions;
import games.generalszh.content.combat.combat_catalog;
import engine.config.binding.values;

// TransitionDamageFXModuleData's object creation lists (the simulation's part of it; its FX lists and particle
// systems are the presentation's, ReadDamageEffects): for each worse body damage state (Damaged, ReallyDamaged,
// Rubble), up to twelve
//   <State>OCL<n> = Bone:<bone> RandomBone:<Yes|No> OCL:<name>   or   <State>OCL<n> = Loc: X:<x> Y:<y> Z:<z> OCL:<name>
// and DamageOCLTypes (parseDamageTypeFlags; all by default): the last damage's type must be one of them. Each module of
// an object keeps its own. Where each plays (getLocalEffectPos): a bone at rest (getPristineBonePositions) in the model
// its state's condition picks, at the object's scale, or one of its numbered family (Smoke01, Smoke02, ... up to 32) for
// a random bone; a bone the model lacks: the location given (the origin for a bone).
export namespace generalszh::content
{
inline constexpr std::size_t TransitionStateCount = 4; // BODYDAMAGETYPE_COUNT
inline constexpr std::size_t TransitionSlots = 12;     // DAMAGE_MODULE_MAX_FX

struct TransitionCreation
{
	std::string list; // the object creation list; empty: the slot is not used
	// Where it may play in the object's frame: one place, or (a random bone) any one of its family's.
	std::vector<Engine::Math::FixedVector3> at;

	bool Used() const noexcept { return !list.empty(); }
};

struct TransitionCreationModule
{
	std::array<std::array<TransitionCreation, TransitionSlots>, TransitionStateCount> slots;
	std::uint64_t types{~std::uint64_t{0}};
};

struct TransitionCreations
{
	std::vector<TransitionCreationModule> modules;
};

namespace transition_damage_detail
{
inline bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}

// The field's words split at blanks and colons, as the original's tokenizer reads them.
inline std::vector<std::string_view> Words(const engine::config::Node &field)
{
	std::vector<std::string_view> words;
	for (const std::string_view value : field.values)
	{
		std::size_t start = 0;
		for (std::size_t index = 0; index <= value.size(); ++index)
			if (index == value.size() || value[index] == ':' || std::isspace(static_cast<unsigned char>(value[index])) != 0)
			{
				if (index > start)
					words.push_back(value.substr(start, index - start));
				start = index + 1;
			}
	}
	return words;
}

struct ParsedSlot
{
	std::string bone; // empty: a location
	bool random{false};
	Engine::Math::FixedVector3 loc;
	std::string list;
};

// parseFXLocInfo then "OCL:" and the list (parseObjectCreationList: "None" is none); malformed: nothing.
inline std::optional<ParsedSlot> ReadSlot(const engine::config::Node &field)
{
	const auto words = Words(field);
	ParsedSlot slot;
	std::size_t next = 0;
	if (words.size() >= 4 && Same(words[0], "bone") && Same(words[2], "randombone"))
	{
		slot.bone = std::string(words[1]);
		slot.random = engine::config::values::ParseBool(words[3]).value_or(false);
		next = 4;
	}
	else if (words.size() >= 7 && Same(words[0], "loc") && Same(words[1], "X") && Same(words[3], "Y") && Same(words[5], "Z"))
	{
		const auto real = [](std::string_view text) { return engine::config::values::ParseFixed(text).value_or(Engine::Math::Fixed{}); };
		slot.loc = {real(words[2]), real(words[4]), real(words[6])};
		next = 7;
	}
	else
		return std::nullopt;
	if (words.size() < next + 2 || !Same(words[next], "ocl"))
		return std::nullopt;
	if (Same(words[next + 1], "None"))
		return slot;
	slot.list = std::string(words[next + 1]);
	return slot;
}
}

inline std::optional<TransitionCreations> ReadTransitionCreations(const ObjectDefinition &object, ModelRigs &rigs)
{
	using namespace transition_damage_detail;
	static constexpr std::array<std::string_view, 3> states{"Damaged", "ReallyDamaged", "Rubble"};
	TransitionCreations creations;
	bool any = false;
	std::optional<ModelStates> looks;
	std::array<std::string, TransitionStateCount> models;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "TransitionDamageFX")
			continue;
		TransitionCreationModule &into = creations.modules.emplace_back();
		for (const engine::config::Node &field : module.block->children)
		{
			const std::string_view key = field.key;
			if (Same(key, "DamageOCLTypes"))
			{
				into.types = ParseDamageTypeFlags(field);
				continue;
			}
			for (std::size_t index = 0; index < states.size(); ++index)
			{
				const std::string_view prefix = states[index];
				// "<State>OCL<1..12>"
				if (key.size() <= prefix.size() + 3 || !Same(key.substr(0, prefix.size()), prefix) || !Same(key.substr(prefix.size(), 3), "OCL"))
					continue;
				const std::string_view number = key.substr(prefix.size() + 3);
				std::size_t slot = 0;
				if (std::from_chars(number.data(), number.data() + number.size(), slot).ptr != number.data() + number.size() || slot < 1 ||
					slot > TransitionSlots)
					break;
				const auto parsed = ReadSlot(field);
				if (!parsed || parsed->list.empty())
					break;
				const std::size_t state = index + 1;
				if (!looks)
				{
					// The model each damage state's condition picks (findBestInfo with DAMAGED, REALLYDAMAGED or RUBBLE).
					looks = ReadModelStates(object);
					for (std::size_t s = 1; s < TransitionStateCount && !looks->Empty(); ++s)
					{
						ConditionBits bits{};
						const std::uint32_t bit = ModelConditionBit(states[s - 1] == "Damaged" ? "DAMAGED" : states[s - 1] == "ReallyDamaged" ? "REALLYDAMAGED" : "RUBBLE");
						bits[bit / 64] |= std::uint64_t{1} << (bit % 64);
						models[s] = looks->states[SelectModelState(*looks, bits)].model;
					}
				}
				TransitionCreation &creation = into.slots[state][slot - 1];
				creation.list = parsed->list;
				const auto scaled = [&](const RestBone &bone) {
					return Engine::Math::FixedVector3{bone.position.x * object.scale, bone.position.y * object.scale, bone.position.z * object.scale};
				};
				if (!parsed->bone.empty() && !models[state].empty())
				{
					if (!parsed->random)
					{
						if (const auto bone = rigs.Bone(models[state], parsed->bone))
							creation.at.push_back(scaled(*bone));
					}
					else
						// getPristineBonePositions(name, 1, positions, MAX_BONES 32): Name01, Name02, ... to the first gap.
						for (int family = 1; family <= 32; ++family)
						{
							char suffix[4];
							std::snprintf(suffix, sizeof(suffix), "%02d", family);
							const auto bone = rigs.Bone(models[state], parsed->bone + suffix);
							if (!bone)
								break;
							creation.at.push_back(scaled(*bone));
						}
				}
				if (creation.at.empty())
					creation.at.push_back(parsed->loc);
				any = true;
				break;
			}
		}
	}
	if (!any)
		return std::nullopt;
	return creations;
}
}
