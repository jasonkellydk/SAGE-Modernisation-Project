export module games.generalszh.gameplay.production.systems.drone_repair_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.docking.resources.dock_repairs;
export import engine.gameplay.rts.docking.systems.repair_dock_system;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.identity.components.producer;
export import engine.gameplay.common.identity.components.definition_ref;
export import games.generalszh.gameplay.objects.resources.object_templates;

// RepairDockUpdate::action with AIDockProcessDockState::findMyDrone: each tick a docker is repaired, its drone (a
// KINDOF_DRONE thing it produced) is healed by its maximum (attemptHealing: made whole), chunk-parallel over the drones.
export namespace generalszh::gameplay
{
struct DroneRepairSystem
{
	using Query = ecs::Query<ecs::Write<engine::gameplay::Health>, ecs::Read<engine::gameplay::Producer>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::DockRepairs>, ecs::Read<ObjectTemplates>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const auto &repairs = context.Read<gp::DockRepairs>().list;
		if (repairs.empty())
			return;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		auto healths = chunk.Get<gp::Health>();
		const auto producers = chunk.Get<gp::Producer>();
		const auto definitions = chunk.Get<gp::DefinitionRef>();
		for (std::size_t row = 0; row < healths.size(); ++row)
		{
			const bool repaired = std::ranges::any_of(repairs, [&](const gp::DockRepair &repair) { return repair.docker == producers[row].entity; });
			if (!repaired || !templates.DefinitionAt(definitions[row].index).Is("DRONE") || gp::IsDead(healths[row]))
				continue;
			gp::Heal(healths[row], healths[row].maximum, context.Tick());
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::DroneRepairSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.drone_repair";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::RepairDockSystem>;
};
}
