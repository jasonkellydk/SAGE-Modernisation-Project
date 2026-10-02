export module engine.gameplay.rts.economy.algorithms.overcharge_switch;
import std;

export import engine.gameplay.rts.economy.components.overcharge;
export import engine.gameplay.rts.economy.components.energy_source;

// OverchargeBehavior::enable: switching on counts its EnergyBonus (Player::addPowerBonus) and starts the drain the
// tick after `tick`; switching off takes the bonus away (removePowerBonus) and has its rods drawn in. Switching to
// what it already is does nothing. True when it changed.
export namespace engine::gameplay
{
inline bool SetOvercharge(Overcharge &overcharge, EnergySource *energy, bool on, std::uint64_t tick) noexcept
{
	if ((overcharge.active != 0) == on)
		return false;
	overcharge.active = on ? 1 : 0;
	if (on)
	{
		overcharge.since = tick;
		overcharge.retract = 0;
		if (energy != nullptr)
			++energy->bonusSources;
	}
	else
	{
		overcharge.retract = 1;
		if (energy != nullptr && energy->bonusSources > 0)
			--energy->bonusSources;
	}
	return true;
}
}
