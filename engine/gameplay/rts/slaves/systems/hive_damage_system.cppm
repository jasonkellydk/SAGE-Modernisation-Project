export module engine.gameplay.rts.slaves.systems.hive_damage_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.slaves.components.hive_body;
export import engine.gameplay.rts.slaves.components.spawner;
export import engine.gameplay.common.health.resources.incoming_damage;
export import engine.gameplay.common.spatial.components.transform;

// HiveStructureBody::attemptDamage over the tick's damage, after it is all dealt and before bodies take it: damage to
// a hive of a type it passes on goes to its spawn nearest the dealer (SpawnBehavior::getClosestSlave: 2D, centres; the
// first of equals) - taken there as that spawn's own; with no spawn, a type it swallows is dropped (no effect); a
// dealer gone, the hive takes it. The records are sorted again after.
export namespace engine::gameplay
{
struct HiveDamageSystem
{
	using Query = ecs::Query<ecs::Read<HiveBody>>;
	using Lookup = ecs::Lookup<ecs::Read<HiveBody>, ecs::Read<Spawner>, ecs::Read<Transform>>;
	using Resources = ecs::Resources<ecs::Write<IncomingDamage>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		IncomingDamage &incoming = context.Write<IncomingDamage>();
		if (incoming.Empty())
			return;
		const auto lookup = context.Lookup<Lookup>();
		std::vector<DamageRecord> &records = incoming.Records();
		bool changed = false;
		std::size_t kept = 0;
		for (std::size_t index = 0; index < records.size(); ++index)
		{
			DamageRecord record = records[index];
			const HiveBody *hive = lookup.Get<HiveBody>(record.target);
			const std::uint64_t bit = record.damageType < 64 ? std::uint64_t{1} << record.damageType : 0;
			if (hive != nullptr && (hive->propagate & bit) != 0)
				if (const Spawner *spawner = lookup.Get<Spawner>(record.target))
					if (const Transform *shooter = lookup.IsAlive(record.source) ? lookup.Get<Transform>(record.source) : nullptr)
					{
						ecs::Entity closest;
						Engine::Math::Fixed best;
						for (std::size_t slot = 0; slot < spawner->spawnedCount; ++slot)
						{
							const ecs::Entity spawn = spawner->spawned[slot];
							const Transform *at = lookup.IsAlive(spawn) ? lookup.Get<Transform>(spawn) : nullptr;
							if (at == nullptr)
								continue;
							const Engine::Math::Fixed distance = Engine::Math::DistanceSquared(at->position.XY(), shooter->position.XY());
							if (closest == ecs::Entity{} || best > distance)
							{
								closest = spawn;
								best = distance;
							}
						}
						if (closest != ecs::Entity{})
						{
							record.target = closest;
							changed = true;
						}
						else if ((hive->swallow & bit) != 0)
						{
							changed = true;
							continue;
						}
					}
			records[kept++] = record;
		}
		records.resize(kept);
		if (changed)
			incoming.Seal();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::HiveDamageSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.hive_damage";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
