export module engine.gameplay.rts.death.systems.mount_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.containment.components.mount;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.rts.lifecycle.resources.kill_requests;
export import engine.gameplay.rts.death.components.dying;

// After the step, before the deaths: each mounted thing rides where its carrier is, facing its way
// (HelixContain::update: setPosition / setOrientation); one whose carrier died this tick (killed, or asked to be) is
// killed with it (HelixContain / OverlordContain::onDie: kill()); one whose carrier is gone is removed with it (onDelete:
// destroyObject). In entity order.
export namespace engine::gameplay
{
struct MountSystem
{
	using Query = ecs::Query<ecs::Read<Mounted>, ecs::Read<Transform>>;
	using Lookup = ecs::Lookup<ecs::Read<Transform>, ecs::Read<Dying>>;
	using Resources = ecs::Resources<ecs::Read<Deaths>, ecs::Write<KillRequests>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		KillRequests &kills = context.Write<KillRequests>();
		std::vector<ecs::Entity> died;
		context.Read<Deaths>().ForEach([&](const Death &death) { died.push_back(death.entity); });
		died.insert(died.end(), kills.entities.begin(), kills.entities.end());
		const auto dies = [&](ecs::Entity carrier) { return std::find(died.begin(), died.end(), carrier) != died.end(); };
		auto &commands = context.Commands();
		std::vector<ecs::Entity> killed;
		query.ForEachChunk([&](auto chunk) {
			const auto mounts = chunk.template Get<Mounted>();
			const auto transforms = chunk.template Get<Transform>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < mounts.size(); ++row)
			{
				const ecs::Entity carrier = mounts[row].carrier;
				if (!lookup.IsAlive(carrier))
				{
					commands.Destroy(entities[row]);
					continue;
				}
				if (lookup.Get<Dying>(entities[row]) == nullptr && (dies(carrier) || lookup.Get<Dying>(carrier) != nullptr))
					killed.push_back(entities[row]);
				if (const Transform *at = lookup.Get<Transform>(carrier); at != nullptr && (at->position != transforms[row].position || at->facing != transforms[row].facing))
					commands.Set<Transform>(entities[row], *at);
			}
		});
		std::sort(killed.begin(), killed.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
		for (const ecs::Entity rider : killed)
			if (std::find(kills.entities.begin(), kills.entities.end(), rider) == kills.entities.end())
				kills.entities.push_back(rider);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::MountSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.mounts";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	// The game orders it before the deaths (DeathSystem).
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
