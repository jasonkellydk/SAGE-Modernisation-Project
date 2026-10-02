export module engine.gameplay.common.status.systems.disable_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.status.components.disabled_until;

// DisableExpirySystem, before the step and in parallel (Object::checkDisabledStatus): a timed disable ends on its tick
// (now >= its end: the type cleared).
// DisableApplySystem, after the step (Object::setDisabledUntil / clearDisabled): the tick's disable requests, in order,
// set their types and ends or clear them (a live entity's only), and are spent.
export namespace engine::gameplay
{
struct DisableExpirySystem
{
	using Query = ecs::Query<ecs::Write<DisabledUntil>, ecs::Write<Disabled>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const std::uint64_t tick = context.Tick();
		auto timers = chunk.Get<DisabledUntil>();
		auto disabled = chunk.Get<Disabled>();
		for (std::size_t row = 0; row < timers.size(); ++row)
			for (std::size_t type = 0; type < DisabledTypeCount; ++type)
			{
				std::uint64_t &until = timers[row].until[type];
				if (until == 0 || tick < until)
					continue;
				disabled[row].mask &= ~(1u << type);
				until = 0;
			}
	}
};

struct DisableApplySystem
{
	using Query = ecs::Query<ecs::Read<Disabled>>;
	using Lookup = ecs::Lookup<ecs::Read<Disabled>, ecs::Read<DisabledUntil>>;
	using Resources = ecs::Resources<ecs::Write<DisableRequests>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		DisableRequests &requests = context.Write<DisableRequests>();
		if (requests.list.empty())
			return;
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		// Each entity's result, in the order it was first asked for.
		std::vector<std::tuple<ecs::Entity, Disabled, DisabledUntil, bool, bool>> results;
		for (const DisableRequest &request : requests.list)
		{
			if (!lookup.IsAlive(request.entity) || request.type == 0)
				continue;
			auto found = std::find_if(results.begin(), results.end(), [&](const auto &entry) { return std::get<0>(entry) == request.entity; });
			if (found == results.end())
			{
				const Disabled *mask = lookup.Get<Disabled>(request.entity);
				const DisabledUntil *timers = lookup.Get<DisabledUntil>(request.entity);
				results.emplace_back(request.entity, mask != nullptr ? *mask : Disabled{}, timers != nullptr ? *timers : DisabledUntil{}, mask != nullptr,
					timers != nullptr);
				found = std::prev(results.end());
			}
			auto &[entity, mask, timers, hasMask, hasTimers] = *found;
			if (request.clear != 0)
			{
				mask.mask &= ~request.type;
				timers.until[static_cast<std::size_t>(std::countr_zero(request.type))] = 0;
				continue;
			}
			mask.mask |= request.type;
			timers.until[static_cast<std::size_t>(std::countr_zero(request.type))] = request.until;
		}
		for (const auto &[entity, mask, timers, hasMask, hasTimers] : results)
		{
			if (hasMask)
				commands.Set<Disabled>(entity, mask);
			else
				commands.Add<Disabled>(entity, mask);
			if (hasTimers)
				commands.Set<DisabledUntil>(entity, timers);
			else
				commands.Add<DisabledUntil>(entity, timers);
		}
		requests.list.clear();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::DisableExpirySystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.disable_expiry";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<engine::gameplay::DisableApplySystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.disable_apply";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
