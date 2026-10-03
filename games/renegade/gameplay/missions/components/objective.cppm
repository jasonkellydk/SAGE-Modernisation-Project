export module games.renegade.gameplay.missions.components.objective;
import std;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.timing.systems.simulation_age_system;
export import engine.gameplay.common.spatial.systems.tracked_position_system;

export namespace renegade {
// Combat/objectives.h. These values and the HUD policy belong to this game.
enum class ObjectiveStatus : std::uint32_t {Pending,Accomplished,Failed,Hidden};
enum class ObjectiveType : std::uint32_t {Primary=1,Secondary,Tertiary};
struct MissionObjective {
    std::int32_t id{};
    ObjectiveType type{ObjectiveType::Primary};
    ObjectiveStatus status{ObjectiveStatus::Pending};
    std::uint32_t short_text{},long_text{},description_sound{},pog{},hud_message{},draw_blip{},reserved{};
    Engine::Math::Fixed priority;
};
struct ObjectiveVocabulary {
    std::map<std::string,std::uint32_t,std::less<>> translations;
    std::vector<std::string> tokens{std::string{}};
    std::uint32_t Intern(const std::string& value) {
        const auto found=std::ranges::find(tokens,value);if(found!=tokens.end()) return std::uint32_t(found-tokens.begin());
        if(tokens.size()>=std::numeric_limits<std::uint32_t>::max()) throw std::length_error("objective vocabulary exhausted");
        tokens.push_back(value);return std::uint32_t(tokens.size()-1);
    }
};
enum class ObjectiveNotice : std::uint32_t {Added,Cancelled,StatusChanged,Refreshed};
struct ObjectiveChange {MissionObjective value;ObjectiveNotice notice;};
using ObjectiveChanges=ecs::ChunkOutputs<ObjectiveChange>;
}
export namespace ecs {
template<> struct ComponentTraits<renegade::MissionObjective> {
    static constexpr std::string_view StableName="renegade.mission_objective";
    static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
template<> struct ResourceTraits<renegade::ObjectiveVocabulary> {static constexpr std::string_view StableName="renegade.objective_vocabulary";};
template<> struct ResourceTraits<renegade::ObjectiveChanges> {static constexpr std::string_view StableName="renegade.objective_changes";};
}
