export module games.generalszh.content.powers.special_ability_content;
import std;

export import games.generalszh.content.objects.object_definition;
export import engine.time.simulation_time;
import engine.config.binding.values;

// An object's SpecialAbilityUpdate modules, in module order (SpecialAbilityUpdateModuleData, defaults as there): the
// power it carries out, how near it must get to start (StartAbilityRange, SPECIAL_ABILITY_HUGE_DISTANCE when unset) and
// how far its target may go before it gives up (AbilityAbortRange), its timings in ticks (PreparationTime,
// PersistentPrepTime, PackTime, UnpackTime, PreTriggerUnstealthTime, EffectDuration), PackUnpackVariationFactor,
// FleeRangeAfterCompletion, EffectValue, AwardXPForTriggering, SkillPointsForTriggering (-1: the XP award) and its
// switches. Its special objects, lasers, particle system and sounds are the presentation's.
export namespace generalszh::content
{
struct SpecialAbilityContent
{
	static constexpr std::int64_t HugeDistance = 10000000; // SPECIAL_ABILITY_HUGE_DISTANCE
	std::string power;
	Engine::Math::Fixed startRange{Engine::Math::Fixed::FromInt(HugeDistance)};
	Engine::Math::Fixed abortRange{Engine::Math::Fixed::FromInt(HugeDistance)};
	Engine::Math::Fixed variation;
	Engine::Math::Fixed fleeRange;
	std::uint64_t preparationTicks{0};
	std::uint64_t persistentPrepTicks{0};
	std::uint64_t packTicks{0};
	std::uint64_t unpackTicks{0};
	std::uint64_t preTriggerUnstealthTicks{0};
	std::uint64_t effectTicks{0};
	std::uint32_t maxSpecialObjects{1};
	std::int32_t effectValue{1};
	std::int32_t awardXp{0};
	std::int32_t skillPoints{-1};
	std::string specialObject;
	std::string specialObjectAttachToBone; // SpecialObjectAttachToBone: where its laser special object streams from
	std::string disableFxParticleSystem;   // DisableFXParticleSystem: over what its hack disables
	std::string packSound, unpackSound, prepSoundLoop, triggerSound; // PackSound, UnpackSound, PrepSoundLoop, TriggerSound
	bool skipPackingWithNoTarget{false};
	bool specialObjectsPersistent{false};
	bool uniqueSpecialObjectTargets{false};
	bool specialObjectsPersistWhenOwnerDies{false};
	bool flipAfterPacking{false};
	bool flipAfterUnpacking{false};
	bool alwaysValidateSpecialObjects{false};
	bool doCaptureFx{false};
	bool loseStealthOnTrigger{false};
	bool approachRequiresLos{true};
	bool needToFaceTarget{true};
	bool persistenceRequiresRecharge{false};
};

// A CommandButtonHuntUpdate (CommandButtonHuntUpdateModuleData): ScanRate (a duration, LOGICFRAMES_PER_SECOND unset) and
// ScanRange (9999 unset).
struct CommandButtonHuntContent
{
	std::uint64_t scanTicks{0};
	Engine::Math::Fixed scanRange{Engine::Math::Fixed::FromInt(9999)};
};

inline std::optional<CommandButtonHuntContent> ReadCommandButtonHunt(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.slot != ModuleSlot::Behavior || module.type != "CommandButtonHuntUpdate")
			continue;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		CommandButtonHuntContent hunt;
		hunt.scanTicks = step.TicksPerSecond();
		if (const auto *node = module.block->Find("ScanRate"); node != nullptr && !node->values.empty())
			hunt.scanTicks = engine::config::ReadDurationTicks(*node, bind).value_or(hunt.scanTicks);
		if (const auto *node = module.block->Find("ScanRange"); node != nullptr && !node->values.empty())
			hunt.scanRange = engine::config::values::ParseFixed(node->Value()).value_or(hunt.scanRange);
		return hunt;
	}
	return std::nullopt;
}

inline std::vector<SpecialAbilityContent> ReadSpecialAbilities(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	std::vector<SpecialAbilityContent> out;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.slot != ModuleSlot::Behavior || module.type != "SpecialAbilityUpdate")
			continue;
		const engine::config::Node &block = *module.block;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		SpecialAbilityContent ability;
		const auto fixed = [&](std::string_view key, Engine::Math::Fixed &into) {
			if (const auto *node = block.Find(key); node != nullptr && !node->values.empty())
				into = engine::config::values::ParseFixed(node->Value()).value_or(into);
		};
		const auto ticks = [&](std::string_view key, std::uint64_t &into) {
			if (const auto *node = block.Find(key); node != nullptr && !node->values.empty())
				into = engine::config::ReadDurationTicks(*node, bind).value_or(into);
		};
		const auto integer = [&](std::string_view key, auto &into) {
			if (const auto *node = block.Find(key); node != nullptr && !node->values.empty())
				if (const auto value = engine::config::values::ParseInt(node->Value()))
					into = static_cast<std::remove_reference_t<decltype(into)>>(*value);
		};
		const auto flag = [&](std::string_view key, bool &into) {
			if (const auto *node = block.Find(key); node != nullptr && !node->values.empty())
				into = engine::config::values::ParseBool(node->Value()).value_or(into);
		};
		if (const auto *node = block.Find("SpecialPowerTemplate"); node != nullptr && !node->values.empty())
			ability.power = std::string(node->Value());
		if (const auto *node = block.Find("SpecialObject"); node != nullptr && !node->values.empty())
			ability.specialObject = std::string(node->Value());
		const auto text = [&](std::string_view key, std::string &into) {
			if (const auto *node = block.Find(key); node != nullptr && !node->values.empty())
				into = std::string(node->Value());
		};
		text("PackSound", ability.packSound);
		text("UnpackSound", ability.unpackSound);
		text("PrepSoundLoop", ability.prepSoundLoop);
		text("SpecialObjectAttachToBone", ability.specialObjectAttachToBone);
		text("DisableFXParticleSystem", ability.disableFxParticleSystem);
		text("TriggerSound", ability.triggerSound);
		fixed("StartAbilityRange", ability.startRange);
		fixed("AbilityAbortRange", ability.abortRange);
		fixed("PackUnpackVariationFactor", ability.variation);
		fixed("FleeRangeAfterCompletion", ability.fleeRange);
		ticks("PreparationTime", ability.preparationTicks);
		ticks("PersistentPrepTime", ability.persistentPrepTicks);
		ticks("PackTime", ability.packTicks);
		ticks("UnpackTime", ability.unpackTicks);
		ticks("PreTriggerUnstealthTime", ability.preTriggerUnstealthTicks);
		ticks("EffectDuration", ability.effectTicks);
		integer("MaxSpecialObjects", ability.maxSpecialObjects);
		integer("EffectValue", ability.effectValue);
		integer("AwardXPForTriggering", ability.awardXp);
		integer("SkillPointsForTriggering", ability.skillPoints);
		flag("SkipPackingWithNoTarget", ability.skipPackingWithNoTarget);
		flag("SpecialObjectsPersistent", ability.specialObjectsPersistent);
		flag("UniqueSpecialObjectTargets", ability.uniqueSpecialObjectTargets);
		flag("SpecialObjectsPersistWhenOwnerDies", ability.specialObjectsPersistWhenOwnerDies);
		flag("FlipOwnerAfterPacking", ability.flipAfterPacking);
		flag("FlipOwnerAfterUnpacking", ability.flipAfterUnpacking);
		flag("AlwaysValidateSpecialObjects", ability.alwaysValidateSpecialObjects);
		flag("DoCaptureFX", ability.doCaptureFx);
		flag("LoseStealthOnTrigger", ability.loseStealthOnTrigger);
		flag("ApproachRequiresLOS", ability.approachRequiresLos);
		flag("NeedToFaceTarget", ability.needToFaceTarget);
		flag("PersistenceRequiresRecharge", ability.persistenceRequiresRecharge);
		out.push_back(std::move(ability));
	}
	return out;
}
}
