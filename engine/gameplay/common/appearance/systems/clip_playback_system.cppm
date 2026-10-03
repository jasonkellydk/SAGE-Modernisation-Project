export module engine.gameplay.common.appearance.systems.clip_playback_system;
import std;
export import engine.ecs.system.system;
export import engine.gameplay.common.appearance.components.clip_playback;
export namespace engine::gameplay {
struct ClipPlaybackSystem {
    using Query=ecs::Query<ecs::Write<ClipPlayback>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        using namespace Engine::Math;
        for(auto& clip:chunk.Get<ClipPlayback>()) {
            if(!clip.frame_count || clip.frames_per_second<=Fixed{} || clip.mode==ClipMode::Hold) continue;
            const auto delta=context.Time().PerTick(clip.frames_per_second),end=Fixed::FromInt(clip.frame_count-1);
            switch(clip.mode) {
                case ClipMode::Loop: {
                    const auto count=(clip.loop_period>Fixed{} ? clip.loop_period : Fixed::FromInt(clip.frame_count)).Raw();const auto raw=(clip.frame+delta).Raw()%count;
                    clip.frame=Fixed::FromRaw(raw<0 ? raw+count : raw);break;
                }
                case ClipMode::Once:clip.frame=std::clamp(clip.frame+delta,Fixed{},end);break;
                case ClipMode::Target: {
                    const auto target=std::clamp(clip.target,Fixed{},end);
                    clip.frame=clip.frame<target ? std::min(clip.frame+delta,target) : std::max(clip.frame-delta,target);break;
                }
                default:break;
            }
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::ClipPlaybackSystem> {
    static constexpr std::string_view StableName="engine.gameplay.clip_playback";
    static constexpr std::size_t PieceRows=32;
    static constexpr SystemPhase Phase=SystemPhase::Simulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
