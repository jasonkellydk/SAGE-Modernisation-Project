export module engine.gameplay.common.timing.systems.pose_track_system;
import std;
export import engine.gameplay.common.timing.components.sampled_pose;
export import engine.gameplay.common.timing.resources.pose_tracks;
export import engine.gameplay.common.timing.systems.timeline_system;
export import engine.gameplay.common.spatial.components.affine_pose;
export import engine.level.model.pose_track;
export namespace engine::gameplay {
struct PoseTrackSystem {
    using Query=ecs::Query<ecs::Read<SampledPose>,ecs::Write<AffinePose>>;
    using Lookup=ecs::Lookup<ecs::Read<TimelinePlayback>>;
    using Resources=ecs::Resources<ecs::Read<PoseTrackLibrary>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto tracks=chunk.Get<SampledPose>();const auto poses=chunk.Get<AffinePose>();const auto lookup=context.Lookup<Lookup>();const auto& library=context.Read<PoseTrackLibrary>();
        for(std::size_t row=0;row<tracks.size();++row) {
            const auto& selected=tracks[row];if(!selected.track || selected.track>library.tracks.size() || !lookup.IsAlive(selected.clock)) continue;
            const auto* clock=lookup.Get<TimelinePlayback>(selected.clock);if(!clock || !clock->enabled || !clock->start_tick) continue;
            const auto& track=library.tracks[selected.track-1];if(track.samples.empty() || !track.samples_per_second) continue;
            // Integer tick arithmetic preserves exact boundaries, including
            // 30fps tracks played by 60Hz simulation, without fixed rounding.
            const auto ticks=context.Tick()+1-clock->start_tick;const auto rate=context.Time().Step().TicksPerSecond();
            const auto index=(ticks/rate)*track.samples_per_second+(ticks%rate)*track.samples_per_second/rate;
            poses[row].transform=track.samples[std::min<std::uint64_t>(index,track.samples.size()-1)];
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::PoseTrackSystem> {
    static constexpr std::string_view StableName="engine.gameplay.pose_track";static constexpr std::size_t PieceRows=32;
    static constexpr SystemPhase Phase=SystemPhase::Simulation;using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
