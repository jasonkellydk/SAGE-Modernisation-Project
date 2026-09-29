export module games.generalszh.content.slaves.slaved_content;
import std;

export import engine.gameplay.rts.slaves.components.slaved;
export import games.generalszh.content.objects.object_definition;
export import games.generalszh.content.combat.weapon_bonus_content;
export import engine.time.simulation_time;
import engine.config.binding.values;

// An object that serves a master, from its SlavedUpdate module: GuardMaxRange,
// GuardWanderRange, AttackRange, AttackWanderRange, ScoutRange, ScoutWanderRange, DistToTargetToGrantRangeBonus
// (whole units). Repairing its master is not bound yet.
export namespace generalszh::content
{
inline std::optional<engine::gameplay::SlavedDefinition> ReadObjectSlaved(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "SlavedUpdate")
			continue;
		const auto range = [&](std::string_view key) {
			const auto *node = module.block->Find(key);
			return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{}) : Engine::Math::Fixed{};
		};
		engine::gameplay::SlavedDefinition slaved;
		slaved.guardMaxRange = range("GuardMaxRange");
		slaved.guardWanderRange = range("GuardWanderRange");
		slaved.attackRange = range("AttackRange");
		slaved.attackWanderRange = range("AttackWanderRange");
		slaved.scoutRange = range("ScoutRange");
		slaved.scoutWanderRange = range("ScoutWanderRange");
		slaved.spottingRange = range("DistToTargetToGrantRangeBonus");
		slaved.spottingBonus = weapon_bonus::DroneSpotting; // SlavedUpdate grants WEAPONBONUSCONDITION_DRONE_SPOTTING
		return slaved;
	}
	return std::nullopt;
}

// A SpawnBehavior: SpawnNumber of SpawnTemplateName (several: in turn), each lost one replaced after SpawnReplaceDelay
// (ms, whole frames rounded up), one batch only (OneShot), its spawns dying with it (SpawnedRequireSpawner),
// InitialBurst (a produced spawner makes its first spawns at once), ExitByBudding (out of the spawn nearest it),
// AggregateHealth (its health its spawns'). Not bound: CanReclaimOrphans (never set in the shipped data),
// PropagateDamageTypesToSlavesWhenExisting (parsed but unused by the original), SlavesHaveFreeWill.
struct SpawnerContent
{
	std::int64_t number{0};
	std::uint64_t replaceTicks{0};
	bool oneShot{false};
	bool requireSpawner{false};
	std::int64_t initialBurst{0};
	bool budding{false};
	bool aggregateHealth{false};
	std::vector<std::string> templates;
};

inline std::optional<SpawnerContent> ReadObjectSpawner(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "SpawnBehavior")
			continue;
		SpawnerContent spawner;
		for (const engine::config::Node &field : module.block->children)
		{
			const auto integer = [&] { return engine::config::values::ParseInt(field.Value()).value_or(0); };
			const auto flag = [&] { return engine::config::values::ParseBool(field.Value()).value_or(false); };
			if (field.key == "SpawnNumber")
				spawner.number = integer();
			else if (field.key == "SpawnReplaceDelay")
			{
				const std::int64_t ms = integer();
				spawner.replaceTicks = ms <= 0 ? 0u : static_cast<std::uint64_t>((ms * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
			}
			else if (field.key == "OneShot")
				spawner.oneShot = flag();
			else if (field.key == "SpawnedRequireSpawner")
				spawner.requireSpawner = flag();
			else if (field.key == "ExitByBudding")
				spawner.budding = flag();
			else if (field.key == "AggregateHealth")
				spawner.aggregateHealth = flag();
			else if (field.key == "InitialBurst")
				spawner.initialBurst = integer();
			else if (field.key == "SpawnTemplateName")
				for (const std::string_view name : field.values)
					spawner.templates.emplace_back(name);
		}
		return spawner;
	}
	return std::nullopt;
}
}
