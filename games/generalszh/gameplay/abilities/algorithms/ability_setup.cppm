export module games.generalszh.gameplay.abilities.algorithms.ability_setup;
import std;

export import games.generalszh.gameplay.abilities.components.special_abilities;
export import games.generalszh.content.powers.special_ability_content;
export import games.generalszh.content.loading.game_content;

// An object's special abilities as it is made.
export namespace generalszh::gameplay
{
// The ability kind of a SpecialPower Enum.
inline AbilityKind AbilityKindOf(std::string_view type)
{
	constexpr std::pair<std::string_view, AbilityKind> kinds[] = {{"SPECIAL_INFANTRY_CAPTURE_BUILDING", AbilityKind::InfantryCaptureBuilding},
		{"SPECIAL_BLACKLOTUS_CAPTURE_BUILDING", AbilityKind::BlackLotusCaptureBuilding}, {"SPECIAL_HACKER_DISABLE_BUILDING", AbilityKind::HackerDisableBuilding},
		{"SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK", AbilityKind::BlackLotusDisableVehicle},
		{"SPECIAL_BLACKLOTUS_STEAL_CASH_HACK", AbilityKind::BlackLotusStealCash}, {"SPECIAL_BOOBY_TRAP", AbilityKind::BoobyTrap},
		{"SPECIAL_REMOTE_CHARGES", AbilityKind::RemoteCharges}, {"SPECIAL_TIMED_CHARGES", AbilityKind::TimedCharges},
		{"SPECIAL_MISSILE_DEFENDER_LASER_GUIDED_MISSILES", AbilityKind::LaserGuidedMissiles},
		{"SPECIAL_TANKHUNTER_TNT_ATTACK", AbilityKind::TankHunterTnt}, {"SPECIAL_HELIX_NAPALM_BOMB", AbilityKind::HelixNapalmBomb},
		{"SPECIAL_DISGUISE_AS_VEHICLE", AbilityKind::DisguiseAsVehicle}};
	for (const auto &[name, kind] : kinds)
		if (name == type)
			return kind;
	return AbilityKind::Other;
}

// An object's SpecialAbilityUpdate modules as it is made (packed up and idle).
inline SpecialAbilities MakeSpecialAbilities(const content::GameContent &content, std::span<const content::SpecialAbilityContent> modules)
{
	SpecialAbilities made;
	for (const content::SpecialAbilityContent &module : modules)
	{
		const auto power = content.powers.Template(module.power);
		if (!power || made.count >= SpecialAbilities::MaxSlots)
			continue;
		AbilitySlot &slot = made.slots[made.count++];
		slot.power = *power;
		slot.kind = AbilityKindOf(content.powers.templates[*power].type);
		slot.startRange = module.startRange;
		slot.abortRange = module.abortRange;
		slot.variation = module.variation;
		slot.fleeRange = module.fleeRange;
		slot.preparationTicks = static_cast<std::uint32_t>(module.preparationTicks);
		slot.persistentPrepTicks = static_cast<std::uint32_t>(module.persistentPrepTicks);
		slot.packTicks = static_cast<std::uint32_t>(module.packTicks);
		slot.unpackTicks = static_cast<std::uint32_t>(module.unpackTicks);
		slot.preTriggerUnstealthTicks = static_cast<std::uint32_t>(module.preTriggerUnstealthTicks);
		slot.effectTicks = static_cast<std::uint32_t>(module.effectTicks);
		slot.awardXp = module.awardXp;
		slot.skillPoints = module.skillPoints;
		slot.effectValue = module.effectValue;
		const auto option = [&](bool on, std::uint8_t bit) {
			if (on)
				slot.options = static_cast<std::uint8_t>(slot.options | bit);
		};
		option(module.skipPackingWithNoTarget, ability_option::SkipPackingWithNoTarget);
		option(module.flipAfterPacking, ability_option::FlipAfterPacking);
		option(module.flipAfterUnpacking, ability_option::FlipAfterUnpacking);
		option(module.doCaptureFx, ability_option::DoCaptureFx);
		option(module.loseStealthOnTrigger, ability_option::LoseStealthOnTrigger);
		option(module.approachRequiresLos, ability_option::ApproachRequiresLos);
		option(module.needToFaceTarget, ability_option::NeedToFaceTarget);
		option(module.persistenceRequiresRecharge, ability_option::PersistenceRequiresRecharge);
		slot.maxSpecialObjects = module.maxSpecialObjects;
		const auto objectOption = [&](bool on, std::uint8_t bit) {
			if (on)
				slot.objectOptions = static_cast<std::uint8_t>(slot.objectOptions | bit);
		};
		objectOption(module.specialObjectsPersistent, special_object_option::Persistent);
		objectOption(module.specialObjectsPersistWhenOwnerDies, special_object_option::PersistWhenOwnerDies);
		objectOption(module.uniqueSpecialObjectTargets, special_object_option::UniqueTargets);
		objectOption(module.alwaysValidateSpecialObjects, special_object_option::AlwaysValidate);
		if (const content::ObjectDefinition *object = content.objects.Find(module.specialObject))
			objectOption(std::ranges::any_of(object->modules, [](const content::ModuleEntry &entry) { return entry.type == "LaserUpdate"; }),
				special_object_option::Laser);
	}
	return made;
}
}
