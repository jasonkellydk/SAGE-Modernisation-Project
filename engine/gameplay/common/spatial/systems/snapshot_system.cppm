export module engine.gameplay.common.spatial.systems.snapshot_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.attitude;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.appearance.components.model_override;
export import engine.gameplay.common.appearance.components.debris_look;

// After the simulation step, copies where every entity is, what it is, who
// owns it and how it looks into per-chunk outputs (in parallel), for the
// client to draw without touching the world.
export namespace engine::gameplay
{
struct VisibleObject
{
	ecs::Entity entity;
	std::uint32_t definition{0};
	Transform transform;
	Attitude attitude; // upright unless the entity tumbles
	std::uint32_t player{0};
	Appearance appearance;
	std::uint32_t model{0}; // a ModelOverride, 0 = the definition's
	DebrisLook debris;      // with a model override: how the piece animates
};

using VisibleObjects = ecs::ChunkOutputs<VisibleObject>;

}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::VisibleObjects>
{
	static constexpr std::string_view StableName = "engine.gameplay.visible_objects";
};
}

export namespace engine::gameplay
{
struct SnapshotSystem
{
	using Query = ecs::Query<ecs::Read<Transform>, ecs::Read<DefinitionRef>, ecs::Optional<Owner>, ecs::Optional<Appearance>, ecs::Optional<Attitude>, ecs::Optional<ModelOverride>,
		ecs::Optional<DebrisLook>, ecs::Exclude<OffMap>>;
	using Resources = ecs::Resources<ecs::Write<VisibleObjects>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<VisibleObjects>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		VisibleObjects &visible = context.Write<VisibleObjects>();
		const auto transforms = chunk.Get<Transform>();
		const auto definitions = chunk.Get<DefinitionRef>();
		const auto owners = chunk.Get<Owner>();
		const auto appearances = chunk.Get<Appearance>();
		const auto attitudes = chunk.Get<Attitude>();
		const auto models = chunk.Get<ModelOverride>();
		const auto debris = chunk.Get<DebrisLook>();
		const auto entities = chunk.Entities();
		auto &out = visible.Slot(context);
		out.reserve(out.size() + transforms.size());
		for (std::size_t row = 0; row < transforms.size(); ++row)
			out.push_back({entities[row], definitions[row].index, transforms[row], attitudes.empty() ? Attitude{} : attitudes[row],
				owners.empty() ? 0u : owners[row].player,
				appearances.empty() ? Appearance{} : appearances[row], models.empty() ? 0u : models[row].model,
				debris.empty() ? DebrisLook{} : debris[row]});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::SnapshotSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.snapshot";
	// Its rows are independent: large chunks are shared out in pieces of 32 rows.
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
