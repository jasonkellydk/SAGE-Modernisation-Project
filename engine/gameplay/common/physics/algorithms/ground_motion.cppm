export module engine.gameplay.common.physics.algorithms.ground_motion;
import std;
export import engine.gameplay.common.physics.algorithms.sweep_and_slide;

export namespace engine::gameplay {
struct GroundMotionOptions {
    Engine::Math::Fixed step_height{},snap_distance{},minimum_normal_z{},clearance{};
};
struct GroundMotionResult {
    Engine::Math::FixedVector3 applied;
    std::optional<engine::level::CollisionHit3> support;
    SweepContacts contacts;
};
inline std::optional<engine::level::CollisionHit3> ProbeGround(const engine::level::CollisionScene3& scene,
    Engine::Math::FixedVector3 position,const SweptHull& hull,Engine::Math::Fixed distance) {
    if(distance<=Engine::Math::Fixed{}) return {};
    return scene.Cast({position+hull.offset,hull.extent},{Engine::Math::Fixed{},Engine::Math::Fixed{},-distance},
        hull.categories,hull.ignored_subject ? std::optional(hull.ignored_subject) : std::nullopt);
}
// Generic sweep/step/snap mechanics. Games supply step limits, slope limits,
// snap distance and spacing; this adapter contains no character rules.
inline GroundMotionResult GroundMotion(const engine::level::CollisionScene3& scene,Engine::Math::FixedVector3 position,
    Engine::Math::FixedVector3 requested,const SweptHull& hull,const GroundMotionOptions& options) {
    using namespace Engine::Math;
    if(options.step_height<Fixed{} || options.snap_distance<Fixed{} || options.clearance<Fixed{} ||
        options.minimum_normal_z<Fixed{} || options.minimum_normal_z>Fixed::One()) throw std::invalid_argument("invalid ground motion options");
    GroundMotionResult result;result.applied=SweepAndSlide(scene,position,requested,hull,result.contacts);
    const FixedVector3 horizontal{requested.x,requested.y,Fixed{}};
    if(options.step_height>Fixed{} && LengthSquared(horizontal)>Fixed{} &&
        Dot(result.applied,horizontal)<LengthSquared(horizontal)) {
        const FixedVector3 up{Fixed{},Fixed{},options.step_height};
        const auto ceiling=scene.Cast({position+hull.offset,hull.extent},up,hull.categories,
            hull.ignored_subject ? std::optional(hull.ignored_subject) : std::nullopt);
        if(!ceiling) {
            SweepContacts contacts;const auto raised=SweepAndSlide(scene,position+up,requested,hull,contacts);
            const auto distance=options.step_height+options.snap_distance;
            auto support=ProbeGround(scene,position+up+raised,hull,distance);
            if(support && !support->starts_overlapping && support->normal.z>=options.minimum_normal_z) {
                const auto drop=std::max(Fixed{},distance*support->fraction-options.clearance);
                const auto candidate=up+raised-FixedVector3{Fixed{},Fixed{},drop};
                if(Dot(candidate,horizontal)>Dot(result.applied,horizontal)) {result.applied=candidate;result.contacts=contacts;result.support=support;}
            }
        }
    }
    if(!result.support) {
        auto support=ProbeGround(scene,position+result.applied,hull,options.snap_distance);
        if(support && !support->starts_overlapping && support->normal.z>=options.minimum_normal_z) {
            result.applied.z-=std::max(Fixed{},options.snap_distance*support->fraction-options.clearance);result.support=support;
        }
    }
    return result;
}
}
