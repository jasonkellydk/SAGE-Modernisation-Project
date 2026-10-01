export module games.generalszh.gameplay.orders.algorithms.command_buttons;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.common.weapons.components.armament;
import games.generalszh.gameplay.construction.algorithms.building;
import games.generalszh.gameplay.combat_drop.algorithms.combat_drop_orders;
import std;
import games.generalszh.gameplay.crates.algorithms.hijacking;
import games.generalszh.gameplay.hacking.algorithms.hack_orders;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.content.control_bar.command_catalog;
import games.generalszh.gameplay.production.algorithms.unit_queue;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.crates.algorithms.car_bombs;
import games.generalszh.gameplay.crates.algorithms.sabotage;
import games.generalszh.gameplay.powers.algorithms.special_power_launch;
import games.generalszh.gameplay.upgrades.algorithms.research;
import games.generalszh.gameplay.construction.algorithms.selling;
import games.generalszh.gameplay.orders.algorithms.command_availability;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.weapons.components.weapon_slots;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.rts.production.components.production_queue;
import engine.gameplay.rts.upgrades.components.upgradable;
import games.generalszh.gameplay.orders.resources.command_bar_overrides;

// A command button used on an object by the scripts, without a target (Object::doCommandButton, CMD_FROM_SCRIPT) or at
// another object (doCommandButtonAtObject), and whether a button may be used on it (CommandButton::isValidToUseOn).
// Which of a team's members a button's order is for (AIGroup::getSpecialPowerSourceObject /
// getCommandButtonSourceObject).
export namespace generalszh::gameplay
{
namespace command_button_detail
{
// Object::isDisabled: any disabled type.
inline bool Disabled(const GameWorld &game, ecs::Entity unit)
{
	const auto *off = game.world.Get<engine::gameplay::Disabled>(unit);
	return off != nullptr && off->mask != 0;
}

// setWeaponLock(slot, LOCKED_TEMPORARILY): false when it has no such weapon to lock.
inline bool LockTemporarily(GameWorld &game, ecs::Entity unit, std::uint32_t slot)
{
	namespace gp = engine::gameplay;
	auto *set = game.world.Get<gp::WeaponSlots>(unit);
	auto *armament = game.world.Get<gp::Armament>(unit);
	if (set != nullptr && armament != nullptr)
		return gp::LockSlotTemporarily(*set, *armament, slot);
	return armament != nullptr && armament->weapon != gp::WeaponCatalog::None && slot == 0;
}

// CommandButton::isValidObjectTarget -> isValidRelationshipTarget: the source's relationship to the target among the
// button's NEED_TARGET_ENEMY / ALLY / NEUTRAL_OBJECT options.
inline bool ValidRelationshipTarget(GameWorld &game, ecs::Entity unit, ecs::Entity target, const content::CommandButtonContent &button)
{
	namespace bo = content::button_option;
	switch (RelationOf(game, unit, target))
	{
	case engine::gameplay::Relationship::Enemies: return (button.options & bo::NeedTargetEnemy) != 0;
	case engine::gameplay::Relationship::Allies: return (button.options & bo::NeedTargetAlly) != 0;
	default: return (button.options & bo::NeedTargetNeutral) != 0;
	}
}

// isValidToUseOn's upgrade test: on a producer with no upgrade in its queue that the upgrade would change and does not
// yet have it. Only an upgrade button takes it: the original took it for any button naming an Upgrade, so the capture
// buttons (SPECIAL_POWER with NEED_UPGRADE's Upgrade_InfantryCaptureBuilding, for the control bar) were never valid on
// anything and the skirmish AI's tech building captures (TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_OBJECTTYPE) found
// nothing. A quirk fixed: their powers wait, paused, for the upgrade anyway.
inline bool UpgradeButton(const content::CommandButtonContent &button)
{
	return !button.upgrade.empty() && (button.command == content::ButtonCommand::PlayerUpgrade || button.command == content::ButtonCommand::ObjectUpgrade);
}

inline bool UpgradeValidOn(GameWorld &game, ecs::Entity unit, const content::CommandButtonContent &button)
{
	namespace gp = engine::gameplay;
	const auto *queue = game.world.Get<gp::ProductionQueue>(unit);
	if (queue == nullptr)
		return false;
	for (std::uint32_t index = 0; index < queue->count; ++index)
		if (queue->entries[index].kind == gp::ProductionKind::Upgrade)
			return false;
	const auto bit = game.templates.Content().upgrades.Find(button.upgrade);
	if (!bit)
		return false;
	const auto *own = game.world.Get<gp::Upgradable>(unit);
	return own == nullptr || !own->completed.Has(*bit);
}
}

// isValidToUseOn (no target): a button with an upgrade as UpgradeValidOn; one needing a target, never; else its special
// power may be used by it (canDoSpecialPower: it has the module).
inline bool ButtonValidOn(GameWorld &game, ecs::Entity unit, const content::CommandButtonContent &button)
{
	if (!game.world.IsAlive(unit))
		return false;
	if (command_button_detail::UpgradeButton(button))
		return command_button_detail::UpgradeValidOn(game, unit, button);
	if ((button.options & content::button_option::NeedTarget) != 0)
		return false;
	return !button.specialPower.empty() && PowerModuleFor(game, unit, button.specialPower, true) != nullptr;
}

// isValidToUseOn at an object, from a script (canDoSpecialPower... without the source's own requirements): a button
// with an upgrade as UpgradeValidOn; one needing an object target, canDoSpecialPowerAtObject; one needing a position,
// the object's (canDoSpecialPowerAtLocation's own tests are not ported: it has the module); else canDoSpecialPower (it
// has the module).
inline bool ButtonValidOnObject(GameWorld &game, ecs::Entity unit, const content::CommandButtonContent &button, ecs::Entity target)
{
	if (!game.world.IsAlive(unit))
		return false;
	if (command_button_detail::UpgradeButton(button))
		return command_button_detail::UpgradeValidOn(game, unit, button);
	const auto power = game.templates.Content().powers.Template(button.specialPower);
	if ((button.options & content::button_option::NeedObjectTarget) != 0)
		return game.world.IsAlive(target) && power && CanTargetWithPower(game, unit, *power, target, true);
	return !button.specialPower.empty() && PowerModuleFor(game, unit, button.specialPower, true) != nullptr;
}

// AIGroup::getSpecialPowerSourceObject (a button with a power: the first member with a module for it) or
// getCommandButtonSourceObject (the first whose command set has a button of its command).
inline ecs::Entity ButtonSourceIn(GameWorld &game, std::span<const ecs::Entity> members, const content::CommandButtonContent &button)
{
	namespace gp = engine::gameplay;
	const content::GameContent &content = game.templates.Content();
	for (const ecs::Entity member : members)
	{
		if (!game.world.IsAlive(member))
			continue;
		if (!button.specialPower.empty())
		{
			if (PowerModuleFor(game, member, button.specialPower, true) != nullptr)
				return member;
			continue;
		}
		const auto effective = EffectiveCommandSet(content.commands, game.world.FindResource<CommandBarOverrides>(), CommandSetOf(game, member));
		const auto *set = effective ? &*effective : nullptr;
		if (set == nullptr)
			continue;
		for (const std::string &name : set->buttons)
			if (const auto *other = name.empty() ? nullptr : content.commands.Button(name); other != nullptr && other->commandName == button.commandName)
				return member;
	}
	return {};
}

// doCommandButtonAtObject from a script, or from the unit's own AI (`fromAi`: CMD_FROM_AI): nothing while disabled;
// SPECIAL_POWER fires its power at the object (doSpecialPowerAtObject: forced from a script; from its AI only when its
// player may use the power, canUseSpecialPower); STOP idles it; HIJACK_VEHICLE, CONVERT_TO_CARBOMB and
// SABOTAGE_BUILDING go in (aiEnter). The rest do nothing (FIRE_WEAPON at an object is not ported yet).
inline void DoCommandButtonAtObject(GameWorld &game, ecs::Entity unit, const content::CommandButtonContent &button, ecs::Entity target, bool fromAi = false)
{
	if (!game.world.IsAlive(unit) || command_button_detail::Disabled(game, unit))
		return;
	if (button.commandName == "SPECIAL_POWER")
	{
		if (button.specialPower.empty() || !game.world.IsAlive(target))
			return;
		if (fromAi)
			if (const auto power = game.templates.Content().powers.Template(button.specialPower); !power || !CanUseSpecialPower(game, unit, *power))
				return;
		FireSpecialPowerAtObject(game, unit, button.specialPower, target, true);
	}
	else if (button.command == content::ButtonCommand::Stop)
		OrderStop(game, unit, false, !fromAi);
	// GUI_COMMAND_COMBATDROP: aiCombatDrop into the object, at its position.
	else if (button.commandName == "COMBATDROP")
	{
		if (const auto *at = game.world.IsAlive(target) ? game.world.Get<engine::gameplay::Transform>(target) : nullptr)
			OrderCombatDrop(game, unit, target, at->position.XY(), false);
	}
	// GUI_COMMAND_FIRE_WEAPON: needing an object target, at a valid one (isValidObjectTarget), its slot locked for now and
	// the object attacked (its position, ATTACK_OBJECTS_POSITION) with the button's MaxShotsToFire; otherwise nothing.
	else if (button.commandName == "FIRE_WEAPON")
	{
		if ((button.options & content::button_option::NeedObjectTarget) == 0 || !game.world.IsAlive(target) ||
			!command_button_detail::ValidRelationshipTarget(game, unit, target, button))
			return;
		command_button_detail::LockTemporarily(game, unit, button.weaponSlot);
		if ((button.options & content::button_option::AttackObjectsPosition) != 0)
		{
			if (const auto *at = game.world.Get<engine::gameplay::Transform>(target))
				OrderAttackPosition(game, unit, at->position, button.maxShotsToFire, !fromAi);
		}
		else
			OrderAttack(game, unit, target, button.maxShotsToFire);
	}
	else if (button.commandName == "HIJACK_VEHICLE" || button.commandName == "CONVERT_TO_CARBOMB" || button.commandName == "SABOTAGE_BUILDING")
	{
		// aiEnter: a hijacker or a car bomber at a vehicle, a saboteur at a building, it would take sets out for it; else a
		// transport is boarded.
		if (!OrderHijack(game, unit, target, !fromAi, false) && !OrderConvertToCarBomb(game, unit, target, !fromAi, false) &&
			!OrderSabotage(game, unit, target, !fromAi, false))
			OrderBoard(game, unit, target, !fromAi);
	}
}

// doCommandButton: nothing while disabled; a special power fired where it stands; STOP idles it; SWITCH_WEAPON locks
// that slot for good; an upgrade queued at it (an object upgrade it has or would not change: nothing); a unit or
// structure queued at it; SELL puts it on sale; HACK_INTERNET hacks. The rest (fire weapon with no position and the
// modes needing a target) do nothing yet.
inline void DoCommandButton(GameWorld &game, ecs::Entity unit, const content::CommandButtonContent &button)
{
	namespace gp = engine::gameplay;
	using content::ButtonCommand;
	if (!game.world.IsAlive(unit) || command_button_detail::Disabled(game, unit))
		return;
	switch (button.command)
	{
	case ButtonCommand::SpecialPower:
		if (!button.specialPower.empty())
			if (const auto *at = game.world.Get<gp::Transform>(unit))
				FireSpecialPower(game, unit, button.specialPower, at->position.XY(), true, false, button.options);
		break;
	case ButtonCommand::Stop:
		OrderStop(game, unit);
		break;
	case ButtonCommand::Other:
		// GUI_COMMAND_HACK_INTERNET: aiHackInternet.
		if (button.commandName == "HACK_INTERNET")
			HackInternet(game, unit, true);
		// GUI_COMMAND_FIRE_WEAPON needing no target: its slot locked for now and its own spot attacked with MaxShotsToFire
		// (a bomb truck's detonation); one needing a target does nothing here.
		else if (button.commandName == "FIRE_WEAPON" &&
			(button.options & (content::button_option::NeedObjectTarget | content::button_option::NeedTargetPos)) == 0)
		{
			if (const auto *at = game.world.Get<gp::Transform>(unit))
			{
				const Engine::Math::FixedVector3 spot = at->position;
				command_button_detail::LockTemporarily(game, unit, button.weaponSlot);
				OrderAttackPosition(game, unit, spot, button.maxShotsToFire, true);
			}
		}
		break;
	case ButtonCommand::SwitchWeapon:
		// setWeaponLock: current at once.
		if (auto *slots = game.world.Get<gp::WeaponSlots>(unit); slots != nullptr && button.weaponSlot < gp::WeaponSlotCount)
			if (auto *armament = game.world.Get<gp::Armament>(unit))
				gp::LockSlot(*slots, *armament, button.weaponSlot);
		break;
	case ButtonCommand::ObjectUpgrade:
	case ButtonCommand::PlayerUpgrade:
		if (!button.upgrade.empty())
			QueueResearch(game, unit, button.upgrade);
		break;
	case ButtonCommand::UnitBuild:
	case ButtonCommand::DozerConstruct:
		if (const auto *owner = game.world.Get<gp::Owner>(unit); owner != nullptr && !button.object.empty())
			QueueUnit(game, owner->player, unit, button.object);
		break;
	case ButtonCommand::Sell:
		BeginSale(game, unit);
		break;
	default:
		break;
	}
}

// doCommandButtonAtPosition from a script: nothing while disabled; a special power fired at the spot (forced,
// doSpecialPowerAtLocation); ATTACK_MOVE there; STOP idles it; DOZER_CONSTRUCT has a dozer build the structure there
// facing east (buildObjectNow: every DOZER_CONSTRUCT button in the data belongs to a dozer or worker); FIRE_WEAPON
// needing a position locks its slot for now and attacks the spot with MaxShotsToFire. The rest do nothing.
inline void DoCommandButtonAtPosition(GameWorld &game, ecs::Entity unit, const content::CommandButtonContent &button, Engine::Math::FixedVector2 at)
{
	namespace gp = engine::gameplay;
	using content::ButtonCommand;
	if (!game.world.IsAlive(unit) || command_button_detail::Disabled(game, unit))
		return;
	const Engine::Math::FixedVector3 spot{at.x, at.y, game.ground.At(at)};
	switch (button.command)
	{
	case ButtonCommand::SpecialPower:
		if (!button.specialPower.empty())
			FireSpecialPower(game, unit, button.specialPower, at, true, true, button.options);
		break;
	case ButtonCommand::Stop:
		OrderStop(game, unit);
		break;
	case ButtonCommand::DozerConstruct:
		if (!button.object.empty() && building_detail::IsBuilder(game.templates.DefinitionAt(game.world.Get<gp::DefinitionRef>(unit)->index)))
		{
			OrderStop(game, unit, false, false);
			BeginConstruction(game, unit, button.object, at, Engine::Math::TurnAngle{});
		}
		break;
	case ButtonCommand::Other:
		if (button.commandName == "ATTACK_MOVE")
			OrderAttackMove(game, unit, at);
		else if (button.commandName == "FIRE_WEAPON" && (button.options & content::button_option::NeedTargetPos) != 0)
		{
			command_button_detail::LockTemporarily(game, unit, button.weaponSlot);
			OrderAttackPosition(game, unit, spot, button.maxShotsToFire, true);
		}
		break;
	default:
		break;
	}
}
}
