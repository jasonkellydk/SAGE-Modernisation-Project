export module games.generalszh.hud.superweapon_timers;
import std;

export import games.generalszh.session.session_view;
import engine.gameplay.rts.powers.algorithms.special_power_timing;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.construction.components.under_construction;
import engine.ecs.query.query;
import games.generalszh.presentation.hud.algorithms.superweapon_list;

// The superweapon countdowns on screen (the original's InGameUI::postDraw "draw superweapon timers" over the
// SuperweaponInfo lists that SpecialPowerModule fills through addSuperweapon): read from the session's view each frame,
// headless. Player by player, their powers by name (the list's map order), each power's countdowns in the order they
// were put up: hidden ones skipped; one still being built takes no line; a shared power shows only its first.
export namespace generalszh::hud
{
struct SuperweaponEntry
{
	bool shown{false};         // a line of its own (else: skipped, still being built)
	std::uint32_t player{0};   // the owner (its colour)
	std::string power;         // the SpecialPower template's name (GUI:<name> is its label)
	std::int64_t readySeconds{0};
	bool ready{false};
};

// Every countdown on screen this frame (nothing before the first tick), in the order the original visits them.
inline std::vector<SuperweaponEntry> ReadSuperweaponTimers(session::SessionView &view, std::uint64_t now)
{
	namespace gp = engine::gameplay;
	std::vector<SuperweaponEntry> out;
	auto &world = view.World();
	const auto *rules = world.FindResource<gp::SpecialPowerRules>();
	const auto *shared = world.FindResource<gp::SharedPowerTimers>();
	if (now == 0 || rules == nullptr || shared == nullptr)
		return out;
	const auto &templates = view.Content().powers.templates;
	std::vector<presentation::SuperweaponSource> sources;
	ecs::Query<ecs::Read<gp::SpecialPowerTimers>, ecs::Read<gp::Owner>> query(world);
	query.ForEachChunk([&](auto chunk) {
		const auto timers = chunk.template Get<gp::SpecialPowerTimers>();
		const auto owners = chunk.template Get<gp::Owner>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < timers.size(); ++row)
			for (std::uint32_t slot = 0; slot < timers[row].count; ++slot)
			{
				const gp::SpecialPowerTimer &timer = timers[row].timers[slot];
				if ((timer.flags & gp::power_flag::PublicTimer) == 0 || timer.power >= templates.size())
					continue;
				const auto *off = world.Get<gp::Disabled>(entities[row]);
				sources.push_back({entities[row], owners[row].player, slot, timer, world.Get<gp::UnderConstruction>(entities[row]) != nullptr,
					off != nullptr && off->mask != 0});
			}
	});
	const auto names = [&](std::uint32_t power) { return std::string_view(templates[power].name); };
	for (const presentation::SuperweaponLine &line : presentation::OrderSuperweapons(sources, names, *rules, *shared, now))
	{
		const presentation::SuperweaponSource &source = sources[line.source];
		out.push_back({line.shown, source.player, templates[source.timer.power].name, line.readySeconds, line.ready});
	}
	return out;
}

// "m:ss" (the time's "%d:%2.2d").
inline std::u16string CountdownText(std::int64_t seconds)
{
	const std::string text = std::format("{}:{:02}", seconds / 60, seconds % 60);
	return std::u16string(text.begin(), text.end());
}

// The ready countdowns' flash: every FlashDuration ticks (0: none) the first ready one drawn swaps between the flash
// colour and its player's (InGameUI's m_superweaponLastFlashFrame / m_superweaponUsedFlashColor).
struct SuperweaponFlash
{
	std::uint64_t lastFlash{0};
	bool usedFlashColor{true}; // so the first is the flash colour

	// Whether a ready countdown drawn on `tick` shows the flash colour; `durationTicks` the flash's length (a Real in
	// the original: none at 0, its whole ticks otherwise).
	bool FlashColor(std::uint64_t tick, Engine::Math::Fixed durationTicks)
	{
		if (durationTicks == Engine::Math::Fixed{})
			return false;
		if (tick >= lastFlash + static_cast<std::uint64_t>(std::max<std::int64_t>(durationTicks.Floor(), 0)))
		{
			usedFlashColor = !usedFlashColor;
			lastFlash = tick;
		}
		return !usedFlashColor;
	}
};
}
