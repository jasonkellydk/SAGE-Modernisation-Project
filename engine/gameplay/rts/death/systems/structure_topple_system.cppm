export module engine.gameplay.rts.death.systems.structure_topple_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.death.components.structure_topple;
export import engine.gameplay.rts.death.resources.death_events;
export import engine.gameplay.rts.death.algorithms.structure_topple;
export import engine.gameplay.rts.death.systems.death_system;
export import engine.gameplay.rts.death.systems.slow_death_system;
export import engine.gameplay.rts.lifecycle.systems.removal_system;

// StructureToppleUpdate::update for every toppling structure, chunk-parallel: bursts while it waits, the fall, the
// crushing near the ground, and, flat, standing again turned to its fall (doToppleDoneStuff). What it plays, makes and
// fires goes out as StructureToppleEvents (the game carries them out after the step).
export namespace engine::gameplay
{
struct StructureToppleSystem
{
	using Query = ecs::Query<ecs::Write<StructureTopple>, ecs::Write<Transform>, ecs::Read<Dying>, ecs::Optional<TeamMember>>;
	using Resources = ecs::Resources<ecs::Read<RandomSeed>, ecs::Read<DeathCatalog>, ecs::Read<GroundHeight>, ecs::Write<StructureToppleEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<StructureToppleEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const DeathCatalog &catalog = context.Read<DeathCatalog>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		auto &played = context.Write<StructureToppleEvents>().Slot(context);
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0x7099u;
		const std::uint64_t tick = context.Tick();
		auto topples = chunk.Get<StructureTopple>();
		auto transforms = chunk.Get<Transform>();
		const auto dyings = chunk.Get<Dying>();
		const auto members = chunk.Get<TeamMember>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < topples.size(); ++row)
		{
			StructureTopple &topple = topples[row];
			if (topple.state == StructureToppleState::Done)
				continue;
			const DeathDefinition &definition = catalog.At(dyings[row].death);
			if (topple.topple >= definition.topples.size())
				continue;
			Transform &transform = transforms[row];
			const structure_topple::ToppleContext at{definition.topples[topple.topple], transform.position, transform.facing, ground, tick};
			const std::uint32_t team = members.empty() ? NoTeam : members[row].team;
			auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation, 0x7099u});
			const auto emit = [&](DeathEffectKind kind, std::uint32_t id, Engine::Math::FixedVector3 where, bool orient) {
				DeathEvent event{entities[row], dyings[row].killer, kind, id, where, transform.facing, team};
				event.orient = orient;
				played.push_back(event);
			};
			if (structure_topple::StepStructureTopple(topple, at, random, emit))
				transform.facing = topple.toppleAngle;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::StructureToppleSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.structure_topple";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	// After the deaths of this tick have begun toppling (next tick they fall).
	using Before = SystemTypeList<engine::gameplay::RemovalSystem>;
	using After = SystemTypeList<engine::gameplay::DeathSystem, engine::gameplay::SlowDeathSystem>;
};
}
