export module engine.gameplay.rts.containment.systems.contained_definitions_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.rts.combat.components.contained_definitions;
export import engine.gameplay.common.identity.components.definition_ref;

// Before the step, chunk-parallel and stateless: each container's riders by definition, in the order they got in (what
// ContainModuleInterface::iterateContained shows AI::findClosestEnemy), for the targeting system's priority sets.
export namespace engine::gameplay
{
struct ContainedDefinitionsSystem
{
	using Query = ecs::Query<ecs::Read<Transport>, ecs::Write<ContainedDefinitions>>;
	using Lookup = ecs::Lookup<ecs::Read<DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<CargoManifest>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const CargoManifest &manifest = context.Read<CargoManifest>();
		const auto lookup = context.Lookup<Lookup>();
		auto contents = chunk.Get<ContainedDefinitions>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < contents.size(); ++row)
		{
			ContainedDefinitions &held = contents[row];
			held.count = 0;
			for (const ecs::Entity rider : manifest.Aboard(entities[row]))
			{
				if (held.count >= ContainedDefinitions::Capacity)
					break;
				if (const DefinitionRef *ref = lookup.IsAlive(rider) ? lookup.Get<DefinitionRef>(rider) : nullptr)
					held.definitions[held.count++] = ref->index;
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::ContainedDefinitionsSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.contained_definitions";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
