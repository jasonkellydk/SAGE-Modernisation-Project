export module engine.gameplay.common.health.systems.status_damage_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.status.components.status_damage;
export import engine.gameplay.common.status.components.status_flags;

// StatusDamageHelper, after the tick's damage (a batch): first its update, a status given by STATUS damage heals on
// its tick (clearStatusCondition: the bit cleared); then each STATUS hit of the tick, in order (ActiveBody's DAMAGE_STATUS:
// the damage after armor is milliseconds of it, ConvertDurationFromMsecsToFrames rounded up): another status it held
// from such damage goes first, then this one is set and heals that many ticks on (the same one again: its timer
// starts over).
export namespace engine::gameplay
{
struct StatusDamageSystem
{
	using Query = ecs::Query<ecs::Write<StatusDamage>, ecs::Write<StatusFlags>>;
	using Lookup = ecs::Lookup<ecs::Read<StatusDamage>, ecs::Read<StatusFlags>>;
	using Resources = ecs::Resources<ecs::Read<Hits>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto given = chunk.template Get<StatusDamage>();
			auto flags = chunk.template Get<StatusFlags>();
			for (std::size_t row = 0; row < given.size(); ++row)
				if (given[row].status != StatusDamage::NoStatus && tick >= given[row].healTick)
				{
					flags[row].bits &= ~(std::uint64_t{1} << given[row].status);
					given[row] = {};
				}
		});
		// The tick's STATUS hits on each target, applied in order to what it holds (as it stands after the healing).
		std::vector<std::tuple<ecs::Entity, StatusDamage, StatusFlags, bool>> touched;
		const auto lookup = context.Lookup<Lookup>();
		context.Read<Hits>().ForEach([&](const Hit &hit) {
			if (hit.statusType == DamageRecord::NoStatus || hit.statusType >= 64 || !lookup.IsAlive(hit.target))
				return;
			auto found = std::find_if(touched.begin(), touched.end(), [&](const auto &entry) { return std::get<0>(entry) == hit.target; });
			if (found == touched.end())
			{
				const StatusDamage *held = lookup.Get<StatusDamage>(hit.target);
				const StatusFlags *bits = lookup.Get<StatusFlags>(hit.target);
				StatusDamage current = held != nullptr ? *held : StatusDamage{};
				if (current.status != StatusDamage::NoStatus && tick >= current.healTick)
					current = {}; // healed above
				touched.emplace_back(hit.target, current, bits != nullptr ? *bits : StatusFlags{}, held != nullptr && bits != nullptr);
				found = std::prev(touched.end());
			}
			auto &[target, status, bits, present] = *found;
			if (status.status != hit.statusType && status.status != StatusDamage::NoStatus)
				bits.bits &= ~(std::uint64_t{1} << status.status);
			const auto frames = (hit.amount * Engine::Math::Fixed::FromInt(30) / Engine::Math::Fixed::FromInt(1000)).Ceil();
			bits.bits |= std::uint64_t{1} << hit.statusType;
			status.status = hit.statusType;
			status.healTick = tick + static_cast<std::uint64_t>(std::max<std::int64_t>(frames, 0));
		});
		auto &commands = context.Commands();
		for (const auto &[target, status, bits, present] : touched)
		{
			if (lookup.Get<StatusDamage>(target) != nullptr)
				commands.Set<StatusDamage>(target, status);
			else
				commands.Add<StatusDamage>(target, status);
			if (lookup.Get<StatusFlags>(target) != nullptr)
				commands.Set<StatusFlags>(target, bits);
			else
				commands.Add<StatusFlags>(target, bits);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::StatusDamageSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.status_damage";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the tick's damage.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
