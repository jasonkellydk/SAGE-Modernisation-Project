export module engine.gameplay.rts.powers.systems.special_power_pause_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.powers.algorithms.special_power_timing;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.identity.components.owner;

// Object::setDisabledUntil / clearDisabled -> pauseAllSpecialPowers: each disabled type (but HELD, which never pauses
// them) that comes on pauses every power module's countdown once, and going off unpauses it once. The disabled bits are
// written by several systems, so this looks for their edges once they have run this tick (their tick is the pause's).
export namespace engine::gameplay
{
struct SpecialPowerPauseSystem
{
	using Query = ecs::Query<ecs::Write<SpecialPowerTimers>, ecs::Optional<Disabled>, ecs::Optional<Owner>>;
	using Resources = ecs::Resources<ecs::Read<SpecialPowerRules>, ecs::Write<SharedPowerTimers>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const SpecialPowerRules &rules = context.Read<SpecialPowerRules>();
		SharedPowerTimers &shared = context.Write<SharedPowerTimers>();
		const std::uint64_t now = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto timers = chunk.template Get<SpecialPowerTimers>();
			const auto disabled = chunk.template Get<Disabled>();
			const auto owners = chunk.template Get<Owner>();
			for (std::size_t row = 0; row < timers.size(); ++row)
			{
				const std::uint32_t mask = (disabled.empty() ? 0u : disabled[row].mask) & ~disabled_type::Held & disabled_type::All;
				SpecialPowerTimers &set = timers[row];
				if (mask == set.disabledSeen)
					continue;
				const PowerClock clock{rules, shared, owners.empty() ? 0u : owners[row].player, now};
				const std::uint32_t on = mask & ~set.disabledSeen, off = set.disabledSeen & ~mask;
				for (std::uint32_t bit = 0; bit < 32; ++bit)
				{
					const std::uint32_t type = 1u << bit;
					if ((on & type) == 0 && (off & type) == 0)
						continue;
					for (std::uint32_t index = 0; index < set.count; ++index)
						PauseCountdown(set.timers[index], (on & type) != 0, clock);
				}
				set.disabledSeen = mask;
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::SpecialPowerPauseSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.special_power_pause";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
