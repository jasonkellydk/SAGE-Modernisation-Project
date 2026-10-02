export module engine.gameplay.rts.slaves.systems.disable_follow_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.status.components.disabled_until;
export import engine.gameplay.common.status.systems.disable_systems;
export import engine.gameplay.rts.slaves.components.spawner;
export import engine.gameplay.rts.containment.components.mount;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.common.weapons.components.armament;

// Object::setDisabledUntil (GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Object.cpp): a timed disable that changes
// an object's end for its type is passed on, with the same end, to its rider (ContainModuleInterface::friend_getRider:
// the portable structure mounted on it) and, when its spawns are its weapons (SPAWNS_ARE_THE_WEAPONS), to each of its
// spawns, which also go idle (SpawnBehavior::orderSlavesDisabledUntil: aiIdle); and on from them in turn. Runs before
// DisableApplySystem, adding to the tick's requests in the order the original would make them. A clear (clearDisabled)
// of a type the object has is passed on to its rider when it held the type without an end, and to its weapon spawns.
export namespace engine::gameplay
{
struct DisableFollowSystem
{
	using Query = ecs::Query<ecs::Read<DisabledUntil>>;
	using Lookup = ecs::Lookup<ecs::Read<DisabledUntil>, ecs::Read<Disabled>, ecs::Read<Mount>, ecs::Read<Spawner>, ecs::Read<MoveOrder>,
		ecs::Read<AttackTarget>>;
	using Resources = ecs::Resources<ecs::Write<DisableRequests>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		auto &list = context.Write<DisableRequests>().list;
		if (list.empty())
			return;
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		// Each (entity, type)'s end as the requests so far leave it.
		std::vector<std::tuple<ecs::Entity, std::uint32_t, std::uint64_t>> ends;
		const auto endOf = [&](ecs::Entity entity, std::uint32_t type) -> std::uint64_t {
			for (auto it = ends.rbegin(); it != ends.rend(); ++it)
				if (std::get<0>(*it) == entity && std::get<1>(*it) == type)
					return std::get<2>(*it);
			const DisabledUntil *timers = lookup.Get<DisabledUntil>(entity);
			return timers != nullptr ? timers->until[static_cast<std::size_t>(std::countr_zero(type))] : 0u;
		};
		std::vector<DisableRequest> expanded;
		expanded.reserve(list.size());
		// Depth first, as the original's recursion: an object's followers right after it.
		const auto apply = [&](auto &self, const DisableRequest &request) -> void {
			if (!lookup.IsAlive(request.entity) || request.type == 0)
				return;
			if (request.clear != 0)
			{
				const Disabled *mask = lookup.Get<Disabled>(request.entity);
				const bool held = mask != nullptr && (mask->mask & request.type) != 0;
				const std::uint64_t end = endOf(request.entity, request.type);
				const bool untimed = end == 0 || end == DisabledForever;
				expanded.push_back(request);
				ends.emplace_back(request.entity, request.type, 0);
				if (!held)
					return;
				if (const Mount *mount = lookup.Get<Mount>(request.entity); mount != nullptr && lookup.IsAlive(mount->rider) && untimed)
					self(self, DisableRequest{mount->rider, request.type, 1, 0});
				if (const Spawner *spawner = lookup.Get<Spawner>(request.entity); spawner != nullptr && spawner->spawnsAreWeapons)
					for (std::size_t index = 0; index < spawner->spawnedCount; ++index)
						if (lookup.IsAlive(spawner->spawned[index]))
							self(self, DisableRequest{spawner->spawned[index], request.type, 1, 0});
				return;
			}
			const bool changed = endOf(request.entity, request.type) != request.until;
			expanded.push_back(request);
			ends.emplace_back(request.entity, request.type, request.until);
			if (!changed)
				return;
			if (const Mount *mount = lookup.Get<Mount>(request.entity); mount != nullptr && lookup.IsAlive(mount->rider))
				self(self, DisableRequest{mount->rider, request.type, 0, request.until});
			if (const Spawner *spawner = lookup.Get<Spawner>(request.entity); spawner != nullptr && spawner->spawnsAreWeapons)
				for (std::size_t index = 0; index < spawner->spawnedCount; ++index)
				{
					const ecs::Entity slave = spawner->spawned[index];
					if (!lookup.IsAlive(slave))
						continue;
					// aiIdle.
					if (const MoveOrder *order = lookup.Get<MoveOrder>(slave))
					{
						MoveOrder idle = *order;
						idle.mode = MoveMode::Idle;
						commands.Set<MoveOrder>(slave, idle);
					}
					if (lookup.Get<AttackTarget>(slave) != nullptr)
						commands.Set<AttackTarget>(slave, AttackTarget{});
					self(self, DisableRequest{slave, request.type, 0, request.until});
				}
		};
		for (const DisableRequest &request : list)
			apply(apply, request);
		list = std::move(expanded);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::DisableFollowSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.disable_follow";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<engine::gameplay::DisableApplySystem>;
	using After = SystemTypeList<>;
};
}
