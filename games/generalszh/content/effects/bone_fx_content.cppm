export module games.generalszh.content.effects.bone_fx_content;
import std;

export import games.generalszh.content.objects.object_definition;
export import games.generalszh.content.models.model_rigs;
export import games.generalszh.content.objects.model_states;
import games.generalszh.content.objects.model_conditions;
import games.generalszh.content.combat.combat_catalog;
import engine.config.binding.values;

// BoneFXUpdateModuleData: for each body damage state (Pristine, Damaged, ReallyDamaged, Rubble), up to eight FX lists,
// object creation lists and particle systems played at a bone of the object:
//   <State>FXList<n>         = Bone:<bone> OnlyOnce:<Yes|No> <min delay> <max delay> FXList:<name>
//   <State>OCL<n>            = Bone:<bone> OnlyOnce:<Yes|No> <min delay> <max delay> OCL:<name>
//   <State>ParticleSystem<n> = Bone:<bone> OnlyOnce:<Yes|No> <min delay> <max delay> PSys:<name>
// the delays in milliseconds (parseDurationReal: ticks as a real, unrounded); and DamageFXTypes, DamageOCLTypes and
// DamageParticleTypes (parseDamageTypeFlags; all by default): the last damage's type must be one of them. A slot is used
// when it names a bone (any name: "NULL" too). Each bone is where it rests (getPristineBonePositions) in the model its
// damage state's condition picks, at the object's scale; a bone the model lacks (NULL): the object's origin.
export namespace generalszh::content
{
inline constexpr std::size_t BoneFxStateCount = 4; // BODYDAMAGETYPE_COUNT
inline constexpr std::size_t BoneFxSlots = 8;       // BONE_FX_MAX_BONES

struct BoneFxEntry
{
	std::string bone; // empty: the slot is not used
	std::string name; // the FX list, object creation list or particle system
	Engine::Math::FixedVector3 at; // the bone at rest in the object's frame
	Engine::Math::Fixed minDelay;  // ticks
	Engine::Math::Fixed maxDelay;
	bool onlyOnce{true};

	bool Used() const noexcept { return !bone.empty(); }
};

using BoneFxTable = std::array<std::array<BoneFxEntry, BoneFxSlots>, BoneFxStateCount>;

struct BoneFxContent
{
	BoneFxTable fx;
	BoneFxTable ocl;
	BoneFxTable particles;
	std::uint64_t fxTypes{~std::uint64_t{0}};
	std::uint64_t oclTypes{~std::uint64_t{0}};
	std::uint64_t particleTypes{~std::uint64_t{0}};
};

namespace bone_fx_detail
{
inline bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}

// The field's words split at blanks and colons ("bone:NULL" -> "bone", "NULL"), as the original's tokenizer reads them.
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

// "Bone: <bone> OnlyOnce: <bool> <min> <max> <kind>: <name>"; malformed: nothing.
inline std::optional<BoneFxEntry> ReadEntry(const engine::config::Node &field, std::string_view kind, std::uint32_t ticksPerSecond)
{
	const auto words = Words(field);
	if (words.size() < 8 || !Same(words[0], "bone") || !Same(words[2], "onlyonce") || !Same(words[6], kind))
		return std::nullopt;
	BoneFxEntry entry;
	entry.bone = std::string(words[1]);
	entry.onlyOnce = engine::config::values::ParseBool(words[3]).value_or(true);
	const auto ticks = [&](std::string_view text) {
		// (Scaled up before the division: 3000 ms is exactly 90 ticks.)
		const Engine::Math::Fixed milliseconds = engine::config::values::ParseFixed(text).value_or(Engine::Math::Fixed{});
		return milliseconds * Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(ticksPerSecond)) / Engine::Math::Fixed::FromInt(1000);
	};
	entry.minDelay = ticks(words[4]);
	entry.maxDelay = ticks(words[5]);
	entry.name = std::string(words[7]);
	return entry;
}
}

inline std::optional<BoneFxContent> ReadBoneFx(const ObjectDefinition &object, ModelRigs &rigs, std::uint32_t ticksPerSecond)
{
	using namespace bone_fx_detail;
	// resolveBoneLocations -> getPristineBonePositions: in the model of the state its conditions pick (findBestInfo), at
	// its scale; each damage state's in the model its own condition picks (PRISTINE: none; DAMAGED, REALLYDAMAGED, RUBBLE).
	const ModelStates looks = ReadModelStates(object);
	std::array<std::string, BoneFxStateCount> models;
	for (std::size_t state = 0; state < BoneFxStateCount && !looks.Empty(); ++state)
	{
		ConditionBits bits{};
		if (state > 0)
		{
			static constexpr std::array<std::string_view, 3> damage{"DAMAGED", "REALLYDAMAGED", "RUBBLE"};
			const std::uint32_t bit = ModelConditionBit(damage[state - 1]);
			bits[bit / 64] |= std::uint64_t{1} << (bit % 64);
		}
		models[state] = looks.states[SelectModelState(looks, bits)].model;
	}
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "BoneFXUpdate")
			continue;
		static constexpr std::array<std::string_view, BoneFxStateCount> states{"Pristine", "Damaged", "ReallyDamaged", "Rubble"};
		BoneFxContent content;
		for (const engine::config::Node &field : module.block->children)
		{
			const std::string_view key = field.key;
			if (Same(key, "DamageFXTypes"))
			{
				content.fxTypes = ParseDamageTypeFlags(field);
				continue;
			}
			if (Same(key, "DamageOCLTypes"))
			{
				content.oclTypes = ParseDamageTypeFlags(field);
				continue;
			}
			if (Same(key, "DamageParticleTypes"))
			{
				content.particleTypes = ParseDamageTypeFlags(field);
				continue;
			}
			for (std::size_t state = 0; state < states.size(); ++state)
			{
				// (ReallyDamaged before Damaged would not matter: the prefixes differ from the start.)
				if (key.size() <= states[state].size() || !Same(key.substr(0, states[state].size()), states[state]))
					continue;
				const std::string_view rest = key.substr(states[state].size());
				const auto slotOf = [&](std::string_view prefix) -> std::optional<std::size_t> {
					if (rest.size() != prefix.size() + 1 || !Same(rest.substr(0, prefix.size()), prefix))
						return std::nullopt;
					const char digit = rest.back();
					if (digit < '1' || digit > '8')
						return std::nullopt;
					return static_cast<std::size_t>(digit - '1');
				};
				BoneFxTable *table = nullptr;
				std::string_view kind;
				std::optional<std::size_t> slot;
				if ((slot = slotOf("FXList")))
					table = &content.fx, kind = "FXList";
				else if ((slot = slotOf("OCL")))
					table = &content.ocl, kind = "OCL";
				else if ((slot = slotOf("ParticleSystem")))
					table = &content.particles, kind = "PSys";
				if (table == nullptr)
					break;
				if (auto entry = ReadEntry(field, kind, ticksPerSecond))
				{
					if (const auto bone = models[state].empty() ? std::nullopt : rigs.Bone(models[state], entry->bone))
						entry->at = {bone->position.x * object.scale, bone->position.y * object.scale, bone->position.z * object.scale};
					(*table)[state][*slot] = std::move(*entry);
				}
				break;
			}
		}
		return content;
	}
	return std::nullopt;
}
}
