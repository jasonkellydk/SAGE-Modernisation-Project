export module games.generalszh.presentation.hud.algorithms.superweapon_list;
import std;

export import engine.ecs.core.entity;
export import engine.gameplay.rts.powers.algorithms.special_power_timing;

// The superweapon countdowns InGameUI keeps (SuperweaponInfo, filled by addSuperweapon) in the order its postDraw loop
// visits them: player by player, their powers by name (the lists' map: byte order), each power's in the order they
// were put up; hidden ones skipped; one still being built takes no line (but is visited); a shared power stops after
// its first. Callers gather the registered countdowns (each object's PublicTimer slots) however they read the world.
export namespace generalszh::presentation
{
struct SuperweaponSource
{
	ecs::Entity entity;
	std::uint32_t player{0};
	std::uint32_t slot{0};
	engine::gameplay::SpecialPowerTimer timer;
	bool underConstruction{false};
	bool disabled{false};
};

struct SuperweaponLine
{
	std::size_t source{0};     // which of the sources
	bool shown{false};         // drawn (else: visited, still being built)
	std::int64_t readySeconds{0};
	bool ready{false};
};

// `names[power]`: each power's template name.
inline std::vector<SuperweaponLine> OrderSuperweapons(const std::vector<SuperweaponSource> &sources, const std::function<std::string_view(std::uint32_t)> &names,
	const engine::gameplay::SpecialPowerRules &rules, const engine::gameplay::SharedPowerTimers &shared, std::uint64_t now)
{
	namespace gp = engine::gameplay;
	std::vector<std::size_t> order(sources.size());
	std::iota(order.begin(), order.end(), std::size_t{0});
	std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
		const SuperweaponSource &x = sources[a], &y = sources[b];
		if (x.player != y.player)
			return x.player < y.player;
		if (x.timer.power != y.timer.power)
			return names(x.timer.power) < names(y.timer.power);
		return x.timer.publicOrder < y.timer.publicOrder;
	});
	std::vector<SuperweaponLine> out;
	for (std::size_t at = 0; at < order.size();)
	{
		std::size_t end = at;
		const SuperweaponSource &first = sources[order[at]];
		while (end < order.size() && sources[order[end]].player == first.player && sources[order[end]].timer.power == first.timer.power)
			++end;
		for (std::size_t index = at; index < end; ++index)
		{
			const SuperweaponSource &each = sources[order[index]];
			if ((each.timer.flags & (gp::power_flag::HiddenByScript | gp::power_flag::HiddenByScience)) != 0)
				continue;
			SuperweaponLine line{order[index]};
			if (each.underConstruction)
			{
				out.push_back(line);
				continue;
			}
			const std::uint64_t readyOn = gp::PeekReadyFrame(each.timer, each.disabled, rules, shared, each.player, now);
			line.shown = true;
			line.ready = gp::PeekIsReady(each.timer, rules, shared, each.player, now);
			// Whole seconds (LOGICFRAMES_PER_SECOND, integer maths), 0 once due.
			line.readySeconds = readyOn < now ? 0 : static_cast<std::int64_t>((readyOn - now) / 30u);
			out.push_back(line);
			if (const gp::SpecialPowerRule *rule = rules.Of(each.timer.power); rule != nullptr && rule->sharedSynced)
				break;
		}
		at = end;
	}
	return out;
}
}
