export module engine.gameplay.common.timing.resources.pose_tracks;
import std;
import engine.ecs.core.resource_store;
export import engine.gameplay.common.timing.components.sampled_pose;
export import engine.level.model.pose_track;

export namespace engine::gameplay {
struct PoseTrackLibrary {std::vector<engine::level::PoseTrack> tracks;};
}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::PoseTrackLibrary> {static constexpr std::string_view StableName="engine.gameplay.pose_tracks";};
}
