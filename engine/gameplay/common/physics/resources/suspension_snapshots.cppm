export module engine.gameplay.common.physics.resources.suspension_snapshots;
import std;
import engine.ecs.core.resource_store;
export import engine.gameplay.common.physics.components.suspension;
export import engine.ecs.system.chunk_outputs;

export namespace engine::gameplay {
struct SuspensionSnapshot {ecs::Entity parent;std::uint32_t position_bone{},rotation_bone{};Engine::Math::Fixed displacement{},rotation{};};
using SuspensionSnapshots=ecs::ChunkOutputs<SuspensionSnapshot>;
}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::SuspensionSnapshots> {static constexpr std::string_view StableName="engine.gameplay.suspension_snapshots";};
}
