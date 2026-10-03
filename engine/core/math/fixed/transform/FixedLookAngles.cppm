export module Engine.Core.Math.FixedLookAngles;
import std;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;

export namespace Engine::Math {
// +X is heading zero, +Z is positive pitch. Angles describe a direction,
// without camera profiles, input limits or game-specific state.
struct LookAngles {TurnAngle heading;Fixed pitch;};
std::optional<LookAngles> LookAnglesForDirection(FixedVector3 direction) {
    if(direction.x==Fixed{} && direction.y==Fixed{} && direction.z==Fixed{}) return std::nullopt;
    const auto heading=Atan2(direction.y,direction.x),elevation=Atan2(direction.z,Length(direction.XY()));
    const auto pitch=std::bit_cast<std::int32_t>(elevation.units)<0 ? -Radians(-elevation) : Radians(elevation);
    return LookAngles{heading,pitch};
}
}
