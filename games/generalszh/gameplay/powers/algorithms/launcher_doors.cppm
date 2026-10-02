export module games.generalszh.gameplay.powers.algorithms.launcher_doors;
import std;

export import games.generalszh.gameplay.powers.components.launcher_door;

// MissileLauncherBuildingUpdate::switchToState: the door's new state and its timeout - opening until the tick before
// the power is ready (then open); open with none; waiting to close DoorWaitOpenTime (then closing); closing
// DoorCloseTime, or half the time until the power is ready again if that is sooner (then closed); closed with none -
// and the new state's effect where the building stands. The same state again: nothing.
export namespace generalszh::gameplay
{
inline void SwitchLauncherDoor(LauncherDoor &door, const LauncherDoorConfig &config, LauncherDoorState state, std::uint64_t now, std::uint64_t readyTick,
	const Engine::Math::FixedVector3 &at, LauncherDoorEffects *effects)
{
	if (door.state == state)
		return;
	switch (state)
	{
	case LauncherDoorState::Closed:
		door.timeoutTick = 0;
		door.timeoutState = LauncherDoorState::Closed;
		break;
	case LauncherDoorState::Opening:
		door.timeoutTick = readyTick > 0 ? readyTick - 1 : 0;
		door.timeoutState = LauncherDoorState::Open;
		break;
	case LauncherDoorState::Open:
		door.timeoutTick = 0;
		door.timeoutState = LauncherDoorState::Open;
		break;
	case LauncherDoorState::WaitingToClose:
		door.timeoutTick = now + config.waitOpenTicks;
		door.timeoutState = LauncherDoorState::Closing;
		break;
	case LauncherDoorState::Closing:
	{
		door.timeoutTick = now + config.closeTicks;
		const std::int64_t delta = static_cast<std::int64_t>(readyTick) - static_cast<std::int64_t>(now);
		const std::int64_t half = static_cast<std::int64_t>(now) + delta / 2;
		if (static_cast<std::int64_t>(door.timeoutTick) > half)
			door.timeoutTick = static_cast<std::uint64_t>(std::max<std::int64_t>(half, 1));
		door.timeoutState = LauncherDoorState::Closed;
		break;
	}
	}
	door.state = state;
	const std::uint32_t effect = config.effects[static_cast<std::size_t>(state)];
	if (effects != nullptr && effect != 0xFFFFFFFFu)
		effects->played.push_back({effect, at});
}
}
