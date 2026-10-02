export module games.generalszh.gameplay.orders.algorithms.command_button_readiness;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.content.control_bar.command_catalog;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.rts.powers.components.special_power_timers;
import engine.gameplay.rts.upgrades.components.upgradable;
import engine.gameplay.rts.upgrades.resources.upgrade_triggers;
import engine.gameplay.rts.upgrades.resources.player_upgrades;
export import games.generalszh.gameplay.upgrades.algorithms.upgrade_affects;

// Whether a team's command button is ready (ScriptConditions::evaluateSkirmishCommandButtonIsReady, over
// CommandButton::isReady): SKIRMISH_COMMAND_BUTTON_READY_ALL / _PARTIAL and the sequential scripts'
// SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_ALL / _PARTIAL.
export namespace generalszh::gameplay
{
// Object::affectedByUpgrade: one of its upgrade modules would go for its player's, its own and this upgrade together.
inline bool AffectedByUpgrade(const GameWorld &game, ecs::Entity unit, std::uint32_t upgrade)
{
	return AffectedByUpgrade(game, unit, OwnerPlayer(game, unit), upgrade);
}

// CommandButton::isReady: its power's module is fully charged (getPercentReady() == 1), or it names an upgrade the
// object would take and has not (Object::hasUpgrade: its own upgrades only).
inline bool CommandButtonIsReady(GameWorld &game, ecs::Entity unit, const content::CommandButtonContent &button)
{
	namespace gp = engine::gameplay;
	const auto &content = game.templates.Content();
	if (const auto power = content.powers.Template(button.specialPower))
		if (const auto *timers = game.world.Get<gp::SpecialPowerTimers>(unit))
			if (const gp::SpecialPowerTimer *timer = timers->Find(*power);
				timer != nullptr && gp::PercentReady(*timer, ClockFor(game, OwnerPlayer(game, unit))) == Engine::Math::Fixed::One())
				return true;
	if (const auto upgrade = content.upgrades.Find(button.upgrade))
	{
		const auto *own = game.world.Get<gp::Upgradable>(unit);
		if (AffectedByUpgrade(game, unit, *upgrade) && !(own != nullptr && own->completed.Has(*upgrade)))
			return true;
	}
	return false;
}

// evaluateSkirmishCommandButtonIsReady: no such team or button: false. Of the team's members, those with a module of
// the button's power's type (Object::hasSpecialPower), or all of them for a button with an upgrade and no power:
// every one ready (`all`; none: true), or any one.
inline bool TeamCommandButtonReady(GameWorld &game, const std::string &team, const std::string &buttonName, bool all)
{
	namespace gp = engine::gameplay;
	const auto index = ResolveTeam(game, team);
	const content::CommandButtonContent *button = game.templates.Content().commands.Button(buttonName);
	if (!index || button == nullptr)
		return false;
	const auto &powers = game.templates.Content().powers;
	const auto power = button->specialPower.empty() ? std::nullopt : powers.Template(button->specialPower);
	const bool hasUpgrade = !button->upgrade.empty() && game.templates.Content().upgrades.Find(button->upgrade).has_value();
	for (const ecs::Entity unit : game.roster.TeamAt(*index).members)
	{
		if (!game.world.IsAlive(unit))
			continue;
		if (power)
		{
			const auto *timers = game.world.Get<gp::SpecialPowerTimers>(unit);
			bool hasType = false;
			for (std::uint32_t slot = 0; timers != nullptr && slot < timers->count && !hasType; ++slot)
				hasType = powers.templates[timers->timers[slot].power].type == powers.templates[*power].type;
			if (!hasType)
				continue;
		}
		else if (!hasUpgrade)
			continue;
		if (CommandButtonIsReady(game, unit, *button))
		{
			if (!all)
				return true;
		}
		else if (all)
			return false;
	}
	return all;
}
}
