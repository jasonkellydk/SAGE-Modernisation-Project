export module engine.level.model.pose_track;
import std;
export import Engine.Core.Math.FixedAffineTransform3;
export namespace engine::level {
// Immutable authored motion, prepared at the content boundary. The clock
// and selected track belong to ECS columns, rather than to scene objects.
struct PoseTrack {std::uint32_t samples_per_second{};std::vector<Engine::Math::FixedAffineTransform3> samples;};
}
