export module engine.gameplay.rts.powers.algorithms.special_power_timing;
import std;

export import engine.gameplay.rts.powers.components.special_power_timers;
export import engine.gameplay.rts.powers.resources.shared_power_timers;
export import engine.gameplay.rts.powers.definitions.special_power_rules;

// SpecialPowerModule's countdown: startPowerRecharge, onSpecialPowerCreation, isReady, getReadyFrame, getPercentReady,
// pauseCountdown, setReadyFrame. A power with a shared timer runs on its player's (SharedPowerTimers).
export namespace engine::gameplay
{
struct PowerClock
{
	const SpecialPowerRules &rules;
	SharedPowerTimers &shared;
	std::uint32_t player;
	std::uint64_t now;
};

// startPowerRecharge: a shared power resets its player's timer (a reload from now; a new one: now); else this one is
// available a reload from now.
inline void StartPowerRecharge(SpecialPowerTimer &timer, const PowerClock &clock)
{
	const SpecialPowerRule *rule = clock.rules.Of(timer.power);
	if (rule == nullptr)
		return;
	if (rule->sharedSynced)
		clock.shared.ResetOrStart(clock.player, timer.power, clock.now, rule->reloadTicks);
	else
		timer.availableOn = clock.now + rule->reloadTicks;
}

// pauseCountdown(FALSE): one pause fewer; the last pushes it back by the pause (multiple unpauses do no harm).
inline void ResumeCountdown(SpecialPowerTimer &timer, std::uint64_t now)
{
	if (timer.pausedCount == 0)
		return;
	--timer.pausedCount;
	if (timer.pausedCount == 0)
		timer.availableOn += now - timer.pausedOn;
}

// pauseCountdown: the first pause notes when and how ready it was; the last unpause pushes it back by the pause.
inline Engine::Math::Fixed PercentReady(const SpecialPowerTimer &timer, const PowerClock &clock);
inline void PauseCountdown(SpecialPowerTimer &timer, bool pause, const PowerClock &clock)
{
	if (pause)
	{
		if (timer.pausedCount == 0)
		{
			timer.pausedOn = clock.now;
			timer.pausedPercent = PercentReady(timer, clock).Raw();
		}
		++timer.pausedCount;
	}
	else
		ResumeCountdown(timer, clock.now);
}

// The module's constructor: a power not shared starts its countdown unless the object is still being built; one that
// starts paused is paused.
inline void StartSpecialPower(SpecialPowerTimer &timer, bool underConstruction, const PowerClock &clock)
{
	const SpecialPowerRule *rule = clock.rules.Of(timer.power);
	if (!underConstruction && rule != nullptr && !rule->sharedSynced)
		StartPowerRecharge(timer, clock);
	if ((timer.flags & power_flag::StartsPaused) != 0)
		PauseCountdown(timer, true, clock);
}

// onSpecialPowerCreation (built, or its science learnt): the countdown starts; a shared power is ready at once (its
// player's timer set to now); one that starts paused is paused.
inline void OnSpecialPowerCreation(SpecialPowerTimer &timer, const PowerClock &clock)
{
	StartPowerRecharge(timer, clock);
	const SpecialPowerRule *rule = clock.rules.Of(timer.power);
	if (rule != nullptr && rule->sharedSynced)
	{
		clock.shared.Express(clock.player, timer.power, clock.now);
		timer.availableOn = clock.shared.GetOrStart(clock.player, timer.power, clock.now);
	}
	if ((timer.flags & power_flag::StartsPaused) != 0)
		PauseCountdown(timer, true, clock);
}

// setReadyFrame.
inline void SetReadyFrame(SpecialPowerTimer &timer, std::uint64_t tick, const PowerClock &clock)
{
	timer.availableOn = tick;
	timer.pausedOn = clock.now;
}

// isReady: a shared power when its player's timer is due; else unpaused and due.
inline bool IsReady(const SpecialPowerTimer &timer, const PowerClock &clock)
{
	const SpecialPowerRule *rule = clock.rules.Of(timer.power);
	if (rule != nullptr && rule->sharedSynced)
		return clock.now >= clock.shared.GetOrStart(clock.player, timer.power, clock.now);
	return timer.pausedCount == 0 && clock.now >= timer.availableOn;
}

// getReadyFrame: a shared power's player timer; a paused (or disabled) one's due tick pushed back by the pause so far.
inline std::uint64_t ReadyFrame(const SpecialPowerTimer &timer, bool disabled, const PowerClock &clock)
{
	const SpecialPowerRule *rule = clock.rules.Of(timer.power);
	if (rule != nullptr && rule->sharedSynced)
		return clock.shared.GetOrStart(clock.player, timer.power, clock.now);
	if (timer.pausedCount > 0 || disabled)
		return timer.availableOn + (clock.now - timer.pausedOn);
	return timer.availableOn;
}

// getPercentReady: 1 when ready; paused: as it was (just short of 1 if it was full); else 1 - left / reload.
inline Engine::Math::Fixed PercentReady(const SpecialPowerTimer &timer, const PowerClock &clock)
{
	using Engine::Math::Fixed;
	if (timer.pausedCount > 0 && timer.pausedPercent == Fixed::One().Raw())
		return Fixed::One() - Fixed::FromRaw(1);
	if (IsReady(timer, clock))
		return Fixed::One();
	if (timer.pausedCount > 0)
		return Fixed::FromRaw(timer.pausedPercent);
	const SpecialPowerRule *rule = clock.rules.Of(timer.power);
	if (rule == nullptr || rule->reloadTicks == 0)
		return Fixed{};
	const std::uint64_t ready = rule->sharedSynced ? clock.shared.GetOrStart(clock.player, timer.power, clock.now) : timer.availableOn;
	const std::int64_t left = static_cast<std::int64_t>(ready) - static_cast<std::int64_t>(clock.now);
	return Fixed::One() - Fixed::FromRatio(left, static_cast<std::int64_t>(rule->reloadTicks));
}

// InGameUI::addSuperweapon: a countdown goes on screen once (again: nothing), after all put there so far; hidden for
// good if its player lacked the power's science then (`knowsScience`).
inline void AddPublicTimer(SpecialPowerTimer &timer, SharedPowerTimers &shared, bool knowsScience)
{
	if ((timer.flags & power_flag::PublicTimer) != 0)
		return;
	timer.flags |= power_flag::PublicTimer;
	if (!knowsScience)
		timer.flags |= power_flag::HiddenByScience;
	timer.publicOrder = shared.nextPublicOrder++;
}

// InGameUI::hide / showObjectSuperweaponDisplayByScript: the object's countdowns on screen hide or show.
inline void HidePublicTimers(SpecialPowerTimers &timers, bool hide)
{
	for (std::uint32_t index = 0; index < timers.count; ++index)
		if ((timers.timers[index].flags & power_flag::PublicTimer) != 0)
		{
			if (hide)
				timers.timers[index].flags |= power_flag::HiddenByScript;
			else
				timers.timers[index].flags &= ~power_flag::HiddenByScript;
		}
}

// For readers outside the simulation (the countdowns on screen, EVA): isReady and getReadyFrame without starting a
// shared timer that is not there yet (it reads as due now, as getOrStartSpecialPowerReadyFrame would make it).
inline std::uint64_t PeekReadyFrame(const SpecialPowerTimer &timer, bool disabled, const SpecialPowerRules &rules, const SharedPowerTimers &shared,
	std::uint32_t player, std::uint64_t now)
{
	const SpecialPowerRule *rule = rules.Of(timer.power);
	if (rule != nullptr && rule->sharedSynced)
	{
		for (const SharedPowerTimer &entry : shared.timers)
			if (entry.player == player && entry.power == timer.power)
				return entry.readyOn;
		return now;
	}
	if (timer.pausedCount > 0 || disabled)
		return timer.availableOn + (now - timer.pausedOn);
	return timer.availableOn;
}

inline bool PeekIsReady(const SpecialPowerTimer &timer, const SpecialPowerRules &rules, const SharedPowerTimers &shared, std::uint32_t player,
	std::uint64_t now)
{
	const SpecialPowerRule *rule = rules.Of(timer.power);
	if (rule != nullptr && rule->sharedSynced)
		return now >= PeekReadyFrame(timer, false, rules, shared, player, now);
	return timer.pausedCount == 0 && now >= timer.availableOn;
}

// getPercentReady for readers outside the simulation (PeekReadyFrame's shared timers).
inline Engine::Math::Fixed PeekPercentReady(const SpecialPowerTimer &timer, const SpecialPowerRules &rules, const SharedPowerTimers &shared,
	std::uint32_t player, std::uint64_t now)
{
	using Engine::Math::Fixed;
	if (timer.pausedCount > 0 && timer.pausedPercent == Fixed::One().Raw())
		return Fixed::One() - Fixed::FromRaw(1);
	if (PeekIsReady(timer, rules, shared, player, now))
		return Fixed::One();
	if (timer.pausedCount > 0)
		return Fixed::FromRaw(timer.pausedPercent);
	const SpecialPowerRule *rule = rules.Of(timer.power);
	if (rule == nullptr || rule->reloadTicks == 0)
		return Fixed{};
	const std::uint64_t ready = PeekReadyFrame(timer, false, rules, shared, player, now);
	const std::int64_t left = static_cast<std::int64_t>(ready) - static_cast<std::int64_t>(now);
	return Fixed::One() - Fixed::FromRatio(left, static_cast<std::int64_t>(rule->reloadTicks));
}
}
