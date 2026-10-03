export module engine.time.value_transition;
import std;
export import Engine.Core.Math.Fixed;
export namespace engine::time {
struct ValueTransition {Engine::Math::Fixed from{},to{},start{},duration{};};
inline Engine::Math::Fixed Sample(const ValueTransition& value,Engine::Math::Fixed now) noexcept {
    using Engine::Math::Fixed;if(value.duration<=Fixed{}) return value.to;
    const auto fraction=std::clamp((now-value.start)/value.duration,Fixed{},Fixed::One());return value.from+(value.to-value.from)*fraction;
}
inline void Retarget(ValueTransition& value,Engine::Math::Fixed now,Engine::Math::Fixed target,Engine::Math::Fixed duration) noexcept {
    value={Sample(value,now),target,now,duration};
}
}
