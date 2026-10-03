export module engine.gameplay.common.audio.systems.emitter_snapshot_system;
import std;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.audio.components.sound_emitter;
export import engine.gameplay.common.spatial.components.transform;
export namespace engine::gameplay {
struct SoundEmitterSnapshot {ecs::Entity entity;SoundEmitter emitter;Engine::Math::FixedVector3 position;};
using SoundEmitterSnapshots=ecs::ChunkOutputs<SoundEmitterSnapshot>;
}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::SoundEmitterSnapshots> {static constexpr std::string_view StableName="engine.gameplay.sound_emitter_snapshots";};
}
export namespace engine::gameplay {
struct EmitterSnapshotSystem {
    using Query=ecs::Query<ecs::Read<SoundEmitter>,ecs::Read<Transform>>;
    using Resources=ecs::Resources<ecs::Write<SoundEmitterSnapshots>>;
    void BeforeChunks(Query& query,ecs::SystemContext& context) const {context.Write<SoundEmitterSnapshots>().Reset(query.PreparedChunkCount());}
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto emitters=chunk.Get<SoundEmitter>();const auto poses=chunk.Get<Transform>();const auto entities=chunk.Entities();auto& out=context.Write<SoundEmitterSnapshots>().Slot(context);
        for(std::size_t row=0;row<emitters.size();++row) out.push_back({entities[row],emitters[row],poses[row].position});
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::EmitterSnapshotSystem> {
    static constexpr std::string_view StableName="engine.gameplay.emitter_snapshot";
    static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
