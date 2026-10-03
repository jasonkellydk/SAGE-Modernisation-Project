export module games.renegade.gameplay.missions.resources.camera_requests;
import std;
export import engine.ecs.system.chunk_outputs;
export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import Engine.Core.Math.FixedLookAngles;

export namespace renegade {
// Typed presentation requests, as in GeneralsZH's camera script host. Actor
// targets resolve after simulation/relocation, never on the loading worker.
struct CameraLookRequest {
    ecs::Entity target;
    Engine::Math::FixedVector3 point;
    Engine::Math::Fixed height_offset;
    std::uint64_t order{};
    bool actor_target{};
};
using CameraLookRequests=ecs::ChunkOutputs<CameraLookRequest>;
std::optional<Engine::Math::LookAngles> CameraLookOrientation(Engine::Math::FixedVector3 origin,
    Engine::Math::FixedVector3 target,Engine::Math::Fixed profile_view_tilt_degrees) {
    using namespace Engine::Math;auto angles=LookAnglesForDirection(target-origin);if(!angles) return std::nullopt;
    // CCameraClass::Force_Look subtracts ViewTilt from downward Tilt;
    // shared LookAngles describes positive elevation instead.
    const auto profile_angle=TurnFromDegrees(profile_view_tilt_degrees);
    angles->pitch+=std::bit_cast<std::int32_t>(profile_angle.units)<0 ? -Radians(-profile_angle) : Radians(profile_angle);
    return angles;
}
std::optional<Engine::Math::FixedVector3> ResolveCameraLook(const ecs::World& world,const CameraLookRequest& request) {
    if(!request.actor_target) return request.point;
    const auto* pose=world.Get<engine::gameplay::Transform>(request.target);
    // Mission00.cpp Say_Something deliberately suppresses actor lookup at X=0.
    if(!pose || pose->position.x==Engine::Math::Fixed{}) return std::nullopt;
    auto point=pose->position;point.z+=request.height_offset;return point;
}
}
export namespace ecs {
template<> struct ResourceTraits<renegade::CameraLookRequests> {static constexpr std::string_view StableName="renegade.camera_look_requests";};
}
