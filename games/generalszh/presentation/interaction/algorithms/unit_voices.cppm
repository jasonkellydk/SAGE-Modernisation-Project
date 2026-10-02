export module games.generalszh.presentation.interaction.algorithms.unit_voices;
import std;

export import engine.ecs.core.entity;
export import engine.gameplay.common.identity.resources.relationships;
export import games.generalszh.content.objects.object_definition;
export import games.generalszh.presentation.interaction.resources.unit_voice_cues;
export import games.generalszh.commands.game_commands;
import games.generalszh.content.combat.combat_catalog;

// CommandXlat.cpp pickAndPlayUnitVoiceResponse: which of the selected things answers an order, and with what. The
// selection is walked in its order (InGameUI's selected list: the most recently selected first); things IGNORED_IN_GUI
// are passed over; each order's case sets the sound and the one that says it, a "skip" case ending the walk at once (the
// first that comes along), the low-priority ones (a move, an attack) only taking the first unit and letting a later one
// upgrade the sound (a crush, a salvage, a GLA worker's shoes; a special weapon). The car bomb's extra line
// (TerroristInCar*Voice) follows the answer when the one answering is a car bomb (OBJECT_STATUS_IS_CARBOMB).
export namespace generalszh::presentation
{
// What the original asks of each selected thing (the object's template, not a disguise's).
struct VoiceUnit
{
	ecs::Entity entity;
	const content::ObjectDefinition *definition{nullptr};
	bool ignoredInGui{false};      // KINDOF_IGNORED_IN_GUI
	bool hasAi{false};             // getAI()
	bool effectivelyMoving{false}; // isMoving() || isWaitingForPath()
	bool canCrushTarget{false};    // canCrushOrSquish(target)
	bool shoedWorker{false};       // INFANTRY, DOZER and HARVESTER, its player with Upgrade_GLAWorkerShoes complete
	// Its weapon (getCurrentWeapon, or the info's slot's: none there, none at all).
	bool hasWeapon{false};
	std::uint32_t damageType{0};
	std::uint8_t weaponSlot{0}; // PRIMARY_WEAPON 0, SECONDARY 1, TERTIARY 2
	bool rocketPods{false};     // the weapon is ComancheRocketPodWeapon
	engine::gameplay::Relationship toTarget{engine::gameplay::Relationship::Neutral}; // getRelationship(target)
	// findSpecialPowerModuleInterface(the info's power type): one there, and its module's InitiateSound.
	bool hasPowerModule{false};
	std::string_view powerInitiateSound;
	bool carBomb{false}; // OBJECT_STATUS_IS_CARBOMB
};

struct VoiceTarget
{
	bool exists{false};
	bool healPad{false};   // KINDOF_HEAL_PAD
	bool structure{false}; // KINDOF_STRUCTURE
};

// The car bomb's extra line (MiscAudio TerroristInCarAttackVoice / MoveVoice / SelectVoice).
enum class CarBombVoice : std::uint8_t
{
	None,
	Attack,
	Move,
	Select,
};

struct VoicePick
{
	std::size_t unit{0}; // the one that says it (objectWithSound)
	std::string_view sound;
	CarBombVoice carBomb{CarBombVoice::None};
};

// ControlBar::processCommandUI's answers: GUI_COMMAND_EVACUATE (with no spot to evacuate to) MSG_EVACUATE,
// GUI_COMMAND_SWITCH_WEAPON MSG_SWITCH_WEAPONS with its slot, GUI_COMMAND_HACK_INTERNET MSG_INTERNET_HACK; the bar's other
// commands are not answered.
inline std::optional<UnitVoiceCue> ControlBarVoice(const commands::GameCommand &command)
{
	if (std::holds_alternative<commands::Evacuate>(command))
		return UnitVoiceCue{VoiceOrder::Evacuate};
	if (std::holds_alternative<commands::HackInternet>(command))
		return UnitVoiceCue{VoiceOrder::InternetHack};
	if (const auto *swap = std::get_if<commands::SwitchWeapon>(&command))
	{
		UnitVoiceCue cue{VoiceOrder::SwitchWeapons};
		cue.weaponSlot = static_cast<std::uint8_t>((std::min<std::uint32_t>)(swap->slot, 0xFEu));
		return cue;
	}
	return std::nullopt;
}

// `valid`: AudioManager::isValidAudioEvent (a named event the audio knows).
template<typename Valid>
std::optional<VoicePick> PickUnitVoice(std::span<const VoiceUnit> units, const UnitVoiceCue &cue, const VoiceTarget &target, Valid &&valid)
{
	static const std::uint32_t surrender = content::DamageTypeIndex("SURRENDER").value_or(0xFFFFFFFFu);
	static const std::uint32_t disarm = content::DamageTypeIndex("DISARM").value_or(0xFFFFFFFFu);
	static const std::uint32_t killPilot = content::DamageTypeIndex("KILL_PILOT").value_or(0xFFFFFFFFu);
	static const std::uint32_t melee = content::DamageTypeIndex("MELEE").value_or(0xFFFFFFFFu);
	static const std::uint32_t flame = content::DamageTypeIndex("FLAME").value_or(0xFFFFFFFFu);
	static const std::uint32_t poison = content::DamageTypeIndex("POISON").value_or(0xFFFFFFFFu);
	bool have = false; // soundToPlayPtr set
	std::string_view sound;
	std::size_t speaker = 0;
	const auto set = [&](std::size_t index, std::string_view chosen) {
		have = true;
		sound = chosen;
		speaker = index;
	};
	for (std::size_t index = 0; index < units.size(); ++index)
	{
		const VoiceUnit &unit = units[index];
		if (unit.ignoredInGui)
			continue;
		if (unit.definition == nullptr)
			return std::nullopt;
		const content::ObjectDefinition &templ = *unit.definition;
		const auto perUnit = [&](std::string_view name) { return templ.UnitSound(name); };
		bool skip = false;
		const auto attackCase = [&](bool specialty) {
			if (!have)
				set(index, cue.air ? templ.Sound("VoiceAttackAir") : templ.Sound("VoiceAttack"));
			if (!unit.hasWeapon)
				return;
			if (unit.damageType == surrender)
			{
				set(index, target.exists && target.structure ? perUnit("VoiceClearBuilding") : perUnit("VoiceSubdue"));
				skip = true;
			}
			else if (unit.damageType == disarm)
			{
				set(index, perUnit("VoiceDisarm"));
				skip = true;
			}
			else if (unit.damageType == killPilot && specialty)
			{
				set(index, perUnit("VoiceSnipePilot"));
				skip = true;
			}
			else if (unit.damageType == melee && specialty)
			{
				set(index, perUnit("VoiceMelee"));
				skip = true;
			}
		};
		switch (cue.order)
		{
		case VoiceOrder::Dock: set(index, perUnit("VoiceSupply")), skip = true; break;
		case VoiceOrder::CreateGroup: set(index, templ.Sound("VoiceSelect")), skip = true; break;
		case VoiceOrder::Evacuate: set(index, perUnit("VoiceUnload")), skip = true; break;
		case VoiceOrder::Repair: set(index, perUnit("VoiceRepair")), skip = true; break;
		case VoiceOrder::CombatDrop: set(index, perUnit("VoiceCombatDrop")), skip = true; break;
		case VoiceOrder::Enter:
			if (target.exists && target.healPad)
				set(index, perUnit("VoiceGetHealed"));
			else if (target.exists && target.structure)
				set(index, unit.toTarget == engine::gameplay::Relationship::Enemies ? perUnit("VoiceEnterHostile") : perUnit("VoiceGarrison"));
			else if (target.exists && unit.toTarget != engine::gameplay::Relationship::Allies)
				set(index, perUnit("VoiceEnterHostile"));
			else
				set(index, perUnit("VoiceEnter"));
			skip = true;
			break;
		case VoiceOrder::Move:
		case VoiceOrder::AttackMove:
		case VoiceOrder::GetRepaired:
		case VoiceOrder::GetHealed:
		case VoiceOrder::Salvage:
			if (!unit.hasAi)
				break;
			// In waypoint mode only one not yet moving answers.
			if (cue.waypointMode && unit.effectivelyMoving)
				continue;
			if (!have)
				set(index, templ.Sound("VoiceMove"));
			if (cue.forceMoveMode && target.exists && unit.canCrushTarget)
			{
				set(index, perUnit("VoiceCrush"));
				skip = true;
			}
			if (cue.order == VoiceOrder::Salvage)
				if (const std::string_view salvage = perUnit("VoiceSalvage"); valid(salvage))
				{
					set(index, salvage);
					skip = true;
				}
			if (unit.shoedWorker)
			{
				set(index, perUnit("VoiceMoveUpgraded"));
				skip = true;
			}
			break;
		case VoiceOrder::Construct: set(index, perUnit("VoiceBuildResponse")), skip = true; break;
		case VoiceOrder::SwitchWeapons:
			if (cue.weaponSlot != UnitVoiceCue::NoSlot)
			{
				set(index, cue.weaponSlot == 0 ? perUnit("VoicePrimaryWeaponMode")
						: cue.weaponSlot == 1  ? perUnit("VoiceSecondaryWeaponMode")
											   : perUnit("VoiceTertiaryWeaponMode"));
				skip = true;
			}
			break;
		case VoiceOrder::ForceAttackGround:
		{
			const std::string_view bombard = perUnit("VoiceBombard");
			set(index, bombard);
			skip = true;
			if (valid(bombard))
				break;
			// Not one: cleared, and on as an attack.
			have = false;
			sound = {};
			attackCase(false);
			break;
		}
		case VoiceOrder::ForceAttackObject:
		case VoiceOrder::AttackObject: attackCase(false); break;
		case VoiceOrder::WeaponAtObject: attackCase(true); break;
		case VoiceOrder::WeaponAtLocation:
			if (!have)
				set(index, cue.air ? templ.Sound("VoiceAttackAir") : templ.Sound("VoiceAttack"));
			if (unit.hasWeapon)
			{
				if (unit.damageType == surrender)
				{
				}
				else if (unit.damageType == disarm)
					set(index, perUnit("VoiceDisarm"));
				// The toxin sprinkler's and the firestorm's GUI-command ground attacks (not the primary weapon).
				else if (unit.damageType == flame)
				{
					if (unit.weaponSlot != 0)
						set(index, perUnit("VoiceFlameLocation"));
				}
				else if (unit.damageType == poison)
				{
					if (unit.weaponSlot != 0)
						set(index, perUnit("VoicePoisonLocation"));
				}
				else if (unit.rocketPods)
				{
					set(index, perUnit("VoiceFireRocketPods"));
					skip = true;
				}
			}
			break;
		case VoiceOrder::Guard: set(index, templ.Sound("VoiceGuard")), skip = true; break;
		case VoiceOrder::SpecialPower:
			if (!cue.specialPower.empty() && unit.hasPowerModule)
			{
				set(index, unit.powerInitiateSound);
				skip = true;
			}
			break;
		case VoiceOrder::InternetHack: set(index, perUnit("VoiceHackInternet")), skip = true; break;
		}
		if (skip)
			break;
	}
	if (!have)
		return std::nullopt;
	VoicePick pick{speaker, sound, CarBombVoice::None};
	if (units[speaker].carBomb)
		switch (cue.order)
		{
		case VoiceOrder::ForceAttackGround:
		case VoiceOrder::ForceAttackObject:
		case VoiceOrder::AttackObject: pick.carBomb = CarBombVoice::Attack; break;
		case VoiceOrder::Move:
		case VoiceOrder::AttackMove:
		case VoiceOrder::GetRepaired:
		case VoiceOrder::GetHealed:
		case VoiceOrder::Salvage: pick.carBomb = CarBombVoice::Move; break;
		case VoiceOrder::CreateGroup: pick.carBomb = CarBombVoice::Select; break;
		default: break;
		}
	return pick;
}
}
