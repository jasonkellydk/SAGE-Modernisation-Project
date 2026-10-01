export module games.generalszh.gameplay.orders.algorithms.command_availability;
import std;
import engine.gameplay.rts.harvesting.resources.harvest_catalog;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.content.control_bar.command_catalog;
import games.generalszh.gameplay.construction.algorithms.building;
import games.generalszh.gameplay.upgrades.algorithms.research;
import games.generalszh.gameplay.upgrades.components.command_set_override;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.production.components.production_queue;
import engine.gameplay.rts.upgrades.components.upgradable;
import engine.gameplay.rts.upgrades.resources.player_upgrades;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.economy.components.overcharge;
import engine.gameplay.common.weapons.components.weapon_slots;
import games.generalszh.gameplay.orders.resources.buildable_overrides;
import engine.gameplay.rts.powers.algorithms.special_power_timing;
import engine.gameplay.rts.movement.components.move_order;

// What the control bar offers an object (the original's ControlBar::getCommandAvailability and
// Object::getCommandSetString): the command set it shows, and each of its buttons as hidden, restricted (shown
// disabled), marked as done or under way (COMMAND_CANT_AFFORD: an upgrade it already has or is researching), or
// available, or on (COMMAND_ACTIVE: a toggle that is on, shown checked and still pressable).
export namespace generalszh::gameplay
{
enum class ButtonState : std::uint8_t
{
	Hidden,
	Restricted,
	NotReady, // COMMAND_NOT_READY: a special power still charging (shown disabled, its inverse clock over it)
	Done,
	Available,
	Active,
};

// Its command set, or the one an upgrade gave it (CommandSetUpgrade).
inline std::string CommandSetOf(const GameWorld &game, ecs::Entity entity)
{
	namespace gp = engine::gameplay;
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gp::DefinitionRef>(entity) : nullptr;
	if (ref == nullptr)
		return {};
	if (const auto *swapped = game.world.Get<CommandSetOverride>(entity))
		return std::string(game.templates.CommandSetName(swapped->id));
	return game.templates.DefinitionAt(ref->index).commandSet;
}

// The object's module for the button's special power (Object::getSpecialPowerModule), if any.
inline const engine::gameplay::SpecialPowerTimer *PowerTimerFor(const GameWorld &game, ecs::Entity entity, const content::CommandButtonContent &button)
{
	const auto power = game.templates.Content().powers.Template(button.specialPower);
	const auto *timers = power && game.world.IsAlive(entity) ? game.world.Get<engine::gameplay::SpecialPowerTimers>(entity) : nullptr;
	return timers != nullptr ? timers->Find(*power) : nullptr;
}

// A charging power button's inverse clock (GadgetButtonDrawInverseClock(getPercentReady() * 100), in whole percent), per
// mille; 1000: none.
inline std::uint32_t CommandClock(const GameWorld &game, ecs::Entity entity, const content::CommandButtonContent &button)
{
	namespace gp = engine::gameplay;
	const auto *timer = button.command == content::ButtonCommand::SpecialPower ? PowerTimerFor(game, entity, button) : nullptr;
	const auto *owner = timer != nullptr ? game.world.Get<gp::Owner>(entity) : nullptr;
	if (owner == nullptr)
		return 1000;
	const auto &rules = game.world.Resource<gp::SpecialPowerRules>();
	const auto &shared = game.world.Resource<gp::SharedPowerTimers>();
	if (gp::PeekIsReady(*timer, rules, shared, owner->player, game.tick))
		return 1000;
	const Engine::Math::Fixed percent = gp::PeekPercentReady(*timer, rules, shared, owner->player, game.tick) * Engine::Math::Fixed::FromInt(100);
	return static_cast<std::uint32_t>(std::clamp<std::int64_t>(percent.Floor(), 0, 100)) * 10;
}

// `forceDisabledEvaluation`: as if not disabled (the original's second look, to tell hidden from restricted).
inline ButtonState CommandAvailability(GameWorld &game, ecs::Entity entity, const content::CommandButtonContent &button, bool forceDisabledEvaluation = false)
{
	namespace gp = engine::gameplay;
	namespace bo = content::button_option;
	using content::ButtonCommand;
	using Buildable = content::ObjectDefinition::Buildable;
	auto &world = game.world;
	const auto *owner = world.IsAlive(entity) ? world.Get<gp::Owner>(entity) : nullptr;
	if (owner == nullptr || (button.options & bo::ScriptOnly) != 0)
		return ButtonState::Hidden;
	const std::uint32_t player = owner->player;
	const auto *off = world.Get<gp::Disabled>(entity);
	const std::uint32_t mask = off != nullptr ? off->mask : 0u;
	// Disabled or unpowered by a script, or unmanned: nothing to offer.
	if ((mask & (gp::disabled_type::ScriptDisabled | gp::disabled_type::ScriptUnderpowered | gp::disabled_type::Unmanned)) != 0)
		return ButtonState::Hidden;
	// MUST_BE_STOPPED: not while it moves (AIUpdateInterface::isMoving).
	if ((button.options & bo::MustBeStopped) != 0)
		if (const auto *order = world.Get<gp::MoveOrder>(entity); order != nullptr && order->mode != gp::MoveMode::Idle)
			return ButtonState::Restricted;
	// Disabled otherwise (only underpowered not counting for an IGNORES_UNDERPOWERED button): only its selling, stopping
	// and the like; the rest greyed, or hidden where they would be anyway.
	const bool disabled = mask != 0 && !((button.options & bo::IgnoresUnderpowered) != 0 && mask == gp::disabled_type::Underpowered);
	if (disabled && !forceDisabledEvaluation && button.command != ButtonCommand::Sell && button.command != ButtonCommand::Stop &&
		button.commandName != "EVACUATE" && button.commandName != "EXIT_CONTAINER" && button.commandName != "BEACON_DELETE" &&
		button.commandName != "SET_RALLY_POINT" && button.commandName != "SWITCH_WEAPON")
		return CommandAvailability(game, entity, button, true) == ButtonState::Hidden ? ButtonState::Hidden : ButtonState::Restricted;
	// NEED_UPGRADE: a player upgrade the player has completed, or an object upgrade the object has.
	if ((button.options & bo::NeedUpgrade) != 0)
		if (const auto upgrade = game.templates.Content().upgrades.Find(button.upgrade))
		{
			if (game.templates.Content().upgrades.upgrades[*upgrade].player)
			{
				if (!world.Resource<gp::PlayerUpgrades>().Completed(player).Has(*upgrade))
					return ButtonState::Restricted;
			}
			else if (const auto *own = world.Get<gp::Upgradable>(entity); own == nullptr || !own->completed.Has(*upgrade))
				return ButtonState::Restricted;
		}
	const auto *queue = world.Get<gp::ProductionQueue>(entity);
	// NOT_QUEUEABLE: not while anything is in production.
	if ((button.options & bo::NotQueueable) != 0 && queue != nullptr && queue->count > 0)
		return ButtonState::Restricted;
	const bool queueMaxed = queue != nullptr && queue->count >= gp::ProductionQueue::MaxEntries;
	// COMBATDROP: nobody aboard who can rappel (getRappellerCount: KINDOF_CAN_RAPPEL riders), restricted.
	if (button.commandName == "COMBATDROP")
	{
		std::size_t rappellers = 0;
		for (const ecs::Entity rider : game.manifest.Aboard(entity))
			if (const auto *ref = world.Get<gp::DefinitionRef>(rider); ref != nullptr && game.templates.DefinitionAt(ref->index).Is("CAN_RAPPEL"))
				++rappellers;
		if (rappellers == 0)
			return ButtonState::Restricted;
	}
	// EXIT_CONTAINER: a rider's inventory button is greyed while its container is subdued (DISABLED_SUBDUED).
	if (button.commandName == "EXIT_CONTAINER" && (mask & gp::disabled_type::Subdued) != 0)
		return ButtonState::Restricted;
	switch (button.command)
	{
	case ButtonCommand::UnitBuild:
	case ButtonCommand::DozerConstruct:
	{
		const content::ObjectDefinition *what = game.templates.Content().objects.Find(button.object);
		const Buildable buildable = what != nullptr ? BuildableOf(world.FindResource<BuildableOverrides>(), *what) : Buildable::No;
		if (what == nullptr || buildable == Buildable::No || (buildable == Buildable::OnlyByAI && !world.Resource<gp::HarvestCatalog>().Computer(player)))
			return ButtonState::Hidden;
		if (button.command == ButtonCommand::UnitBuild && queueMaxed)
			return ButtonState::Restricted;
		// canBuild, canMakeUnit (not enough money shows restricted too, as the original).
		return CanMakeUnit(game, entity, *what) == CanMake::Ok ? ButtonState::Available : ButtonState::Restricted;
	}
	case ButtonCommand::PlayerUpgrade:
	case ButtonCommand::ObjectUpgrade:
	{
		if (queueMaxed)
			return ButtonState::Restricted;
		const auto found = game.templates.Content().upgrades.Find(button.upgrade);
		if (!found)
			return ButtonState::Hidden;
		const content::UpgradeContent &upgrade = game.templates.Content().upgrades.upgrades[*found];
		const std::uint32_t bit = *found;
		if (button.command == ButtonCommand::PlayerUpgrade)
		{
			const auto &players = world.Resource<gp::PlayerUpgrades>();
			if (players.Completed(player).Has(bit) || players.InProduction(player, bit))
				return ButtonState::Done;
		}
		else
		{
			bool had = false;
			if (const auto *own = world.Get<gp::Upgradable>(entity); own != nullptr && own->completed.Has(bit))
				had = true;
			if (queue != nullptr)
				for (std::uint32_t index = 0; index < queue->count; ++index)
					had = had || (queue->entries[index].kind == gp::ProductionKind::Upgrade && queue->entries[index].definition == bit);
			if (had)
				return ButtonState::Done;
		}
		if (world.Resource<gp::PlayerMoney>().Balance(player) < upgrade.cost)
			return ButtonState::Restricted;
		return ButtonState::Available;
	}
	case ButtonCommand::SwitchWeapon:
	{
		// ControlBar::getCommandAvailability: hidden without a weapon in its slot, active while locked to it.
		const auto *slots = world.Get<gp::WeaponSlots>(entity);
		if (slots == nullptr || button.weaponSlot >= gp::WeaponSlotCount || slots->slots[button.weaponSlot].weapon == 0xFFFFFFFFu)
			return ButtonState::Hidden;
		return slots->locked == button.weaponSlot ? ButtonState::Active : ButtonState::Available;
	}
	case ButtonCommand::ToggleOvercharge:
		// ControlBar::getCommandAvailability: active while its overcharge is on.
		if (const auto *overcharge = world.Get<gp::Overcharge>(entity); overcharge != nullptr && overcharge->active != 0)
			return ButtonState::Active;
		return ButtonState::Available;
	case ButtonCommand::SpecialPower:
	{
		// getSpecialPowerModule(template): no module, nothing more (the original asserts); not ready, charging.
		const auto *timer = PowerTimerFor(game, entity, button);
		if (timer != nullptr && !gp::PeekIsReady(*timer, world.Resource<gp::SpecialPowerRules>(), world.Resource<gp::SharedPowerTimers>(), player, game.tick))
			return ButtonState::NotReady;
		return ButtonState::Available;
	}
	case ButtonCommand::Sell:
		if (const auto *off = world.Get<gp::Disabled>(entity); off != nullptr && (off->mask & gp::disabled_type::Subdued) != 0)
			return ButtonState::Restricted;
		return ButtonState::Available;
	default:
		return ButtonState::Available;
	}
}
}
