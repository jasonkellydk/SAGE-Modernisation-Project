export module engine.level.model.timeline;
import std;
export import Engine.Core.Math.Fixed;
export namespace engine::level {
struct TimelineCue {Engine::Math::Fixed time;std::uint32_t action{};};
struct Timeline {std::vector<TimelineCue> cues;};
inline std::expected<void,std::string> ValidateTimeline(const Timeline& timeline) {
    Engine::Math::Fixed previous;
    for(const auto& cue:timeline.cues) {
        if(cue.time<previous) return std::unexpected("timeline cues must have nonnegative ordered times");
        previous=cue.time;
    }
    return {};
}
}
