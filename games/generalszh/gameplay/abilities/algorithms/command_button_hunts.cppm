export module games.generalszh.gameplay.abilities.algorithms.command_button_hunts;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.abilities.components.command_button_hunt;
export import games.generalszh.gameplay.abilities.systems.command_button_hunt_system;
import games.generalszh.gameplay.orders.algorithms.command_buttons;
import games.generalszh.gameplay.crates.algorithms.car_bombs;
import games.generalszh.gameplay.orders.algorithms.command_availability;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.powers.algorithms.special_power_launch;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.orders.resources.command_bar_overrides;

// CommandButtonHuntUpdate's ends outside its system (CommandButtonHuntSystem updates the hunts): a script setting a
// unit's button (setCommandButton, TEAM_HUNT_WITH_COMMAND_BUTTON), and, once the systems have run, each scan's unit using
// its button (CMD_FROM_AI) on the best candidate its power may be used on (canDoSpecialPowerAtObject).
export namespace generalszh::gameplay
{
// CommandButtonHuntUpdate::setCommandButton: the first button of its command set so named (none: it hunts with
// nothing); with an AI it idles, updates this tick, and again the next.
inline void StartCommandButtonHunt(GameWorld &game, ecs::Entity unit, const std::string &ability)
{
	auto *hunt = game.world.IsAlive(unit) ? game.world.Get<CommandButtonHunt>(unit) : nullptr;
	if (hunt == nullptr)
		return;
	hunt->set = CommandButtonHunt::NotHunting;
	const std::string setName = CommandSetOf(game, unit);
	const auto effective = EffectiveCommandSet(game.templates.Content().commands, game.world.FindResource<CommandBarOverrides>(), setName);
	if (const auto *set = effective ? &*effective : nullptr)
		for (std::uint32_t slot = 0; slot < set->buttons.size(); ++slot)
			if (!set->buttons[slot].empty() && set->buttons[slot] == ability)
			{
				hunt->set = game.templates.CommandSet(setName);
				hunt->slot = slot;
				break;
			}
	if (!hunt->Hunting() || !HasAi(game, unit))
		return;
	AiIdle(game, unit);
	// Updated as the scripts' tick goes on (CommandButtonHuntSystem), and again the tick after.
	hunt->nextTick = game.tick;
	hunt->again = 1;
}

// ScriptActions::doTeamHuntWithCommandButton: a button that hunts (a special power at objects, a weapon, an enter
// mode); every member with an AI whose command set has it, and a CommandButtonHuntUpdate, hunts with it.
inline void TeamHuntWithCommandButton(GameWorld &game, const std::string &team, const std::string &ability)
{
	const auto index = ResolveTeam(game, team);
	const content::GameContent &content = game.templates.Content();
	const auto *button = content.commands.Button(ability);
	if (!index || button == nullptr)
		return;
	const std::string &command = button->commandName;
	if (command == "SPECIAL_POWER")
	{
		if (button->specialPower.empty() || (button->options & content::button_option::NeedObjectTarget) == 0)
			return;
	}
	else if (command != "SWITCH_WEAPON" && command != "FIRE_WEAPON" && command != "HIJACK_VEHICLE" && command != "CONVERT_TO_CARBOMB" &&
		command != "SABOTAGE_BUILDING")
		return;
	const std::vector<ecs::Entity> members = game.roster.TeamAt(*index).members;
	for (auto it = members.rbegin(); it != members.rend(); ++it)
	{
		const ecs::Entity member = *it;
		if (!game.world.IsAlive(member) || !HasAi(game, member))
			continue;
		const auto effective = EffectiveCommandSet(content.commands, game.world.FindResource<CommandBarOverrides>(), CommandSetOf(game, member));
		const auto *set = effective ? &*effective : nullptr;
		if (set == nullptr || std::ranges::find(set->buttons, ability) == set->buttons.end())
			continue;
		StartCommandButtonHunt(game, member, ability);
	}
}

// The tick's hunt scans, in order: the first candidate (best first) the unit's power may be used on from its AI gets the
// button, if it still hunts with it.
inline void ApplyHuntScans(GameWorld &game)
{
	auto *resource = game.world.FindResource<HuntScans>();
	if (resource == nullptr)
		return;
	std::vector<HuntScan> scans;
	resource->AppendTo(scans);
	resource->Reset(0);
	for (const HuntScan &scan : scans)
	{
		const auto *hunt = game.world.IsAlive(scan.unit) ? game.world.Get<CommandButtonHunt>(scan.unit) : nullptr;
		if (hunt == nullptr || hunt->set != scan.set || hunt->slot != scan.slot)
			continue;
		const auto *button = hunt_system_detail::ButtonOf(game.templates, game.world.FindResource<CommandBarOverrides>(), *hunt);
		if (button != nullptr && button->commandName == "CONVERT_TO_CARBOMB")
		{
			for (const HuntCandidate &candidate : scan.candidates)
				if (game.world.IsAlive(candidate.entity) && CanConvertToCarBomb(game, scan.unit, candidate.entity, false))
				{
					DoCommandButtonAtObject(game, scan.unit, *button, candidate.entity, true);
					break;
				}
			continue;
		}
		const auto power = button != nullptr ? game.templates.Content().powers.Template(button->specialPower) : std::nullopt;
		if (!power)
			continue;
		for (const HuntCandidate &candidate : scan.candidates)
			if (game.world.IsAlive(candidate.entity) && CanTargetWithPower(game, scan.unit, *power, candidate.entity, false))
			{
				DoCommandButtonAtObject(game, scan.unit, *button, candidate.entity, true);
				break;
			}
	}
}
}
