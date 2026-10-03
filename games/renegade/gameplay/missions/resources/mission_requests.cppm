export module games.renegade.gameplay.missions.resources.mission_requests;
import std;
export import engine.gameplay.common.scripts.resources.behavior_programs;
export namespace renegade {
// Completed callback commands that still require a gameplay/presentation
// consumer. They are retained explicitly, never counted as executed effects.
struct MissionRequests : ecs::ChunkOutputs<engine::gameplay::BehaviorEmission> {};
}
export namespace ecs {
template<> struct ResourceTraits<renegade::MissionRequests> {
    static constexpr std::string_view StableName="renegade.mission_requests";
};
}
