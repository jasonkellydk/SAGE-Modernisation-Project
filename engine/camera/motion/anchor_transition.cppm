export module engine.camera.motion.anchor_transition;
import std;
export import engine.camera.motion.camera_motion;

export namespace engine::camera {
// Presentation-only transition to a live anchor. Games choose when it begins
// and its duration; authoritative positions and movement are unaffected.
class AnchorTransition {
public:
    void Begin(Math::Vector3 origin,float heading,float duration) {
        if(!std::isfinite(duration) || duration<0 || !std::isfinite(heading) ||
            !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
            throw std::invalid_argument("invalid camera anchor transition");
        m_origin=origin;m_heading=heading;m_total=duration;m_remaining=duration;
    }
    float PreviousWeight() const noexcept {return m_total>0 ? std::clamp(m_remaining/m_total,0.f,1.f) : 0.f;}
    Math::Vector3 Anchor(Math::Vector3 target) const noexcept {
        const auto weight=PreviousWeight();return {target.x+(m_origin.x-target.x)*weight,
            target.y+(m_origin.y-target.y)*weight,target.z+(m_origin.z-target.z)*weight};
    }
    float Heading(float target) const noexcept {return target+NormalizeAngle(m_heading-target)*PreviousWeight();}
    void Advance(float seconds) {
        if(!std::isfinite(seconds) || seconds<0) throw std::invalid_argument("invalid camera transition delta");
        m_remaining=std::max(0.f,m_remaining-seconds);
    }
    bool Active() const noexcept {return m_remaining>0;}
private:
    Math::Vector3 m_origin{};float m_heading{},m_total{},m_remaining{};
};
}
