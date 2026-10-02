export module engine.gui.w3d.input_capture;
import std;
import engine.gui.mvvm.observable;

export namespace engine::gui::w3d {
struct InputAssignment {
    std::uint32_t input{};
    std::u16string caption;
    bool operator==(const InputAssignment&) const=default;
};
struct InputCaptureLimits {
    std::uint64_t primary_pointer_delay{};
    std::uint32_t maximum_keyboard_input{(std::numeric_limits<std::uint32_t>::max)()};
};
enum class CapturePointer { Primary,Secondary,Middle,Wheel };
// The widget owns focus timing and deferred keyboard delivery. The caller
// owns input identities, conflict decisions, accepted assignments and captions.
class InputCaptureModel final {
public:
    using Resolve=std::function<std::optional<InputAssignment>(std::uint32_t)>;
    engine::gui::mvvm::Observable<InputAssignment> assignment;
    engine::gui::mvvm::Observable<bool> focused{false},enabled{true};
    explicit InputCaptureModel(InputCaptureLimits limits={}):m_limits(limits) {}
    void Focus(bool value,std::uint64_t milliseconds) {
        if(value && !focused.Get()) {
            const auto maximum=(std::numeric_limits<std::uint64_t>::max)();
            m_primary_deadline=milliseconds>maximum-m_limits.primary_pointer_delay ? maximum : milliseconds+m_limits.primary_pointer_delay;
        }
        focused.Set(value);
    }
    bool QueueKeyboard(std::uint32_t input) {
        if(!focused.Get() || !enabled.Get()) return false;
        m_pending=input;return true;
    }
    bool FlushKeyboard(const Resolve& resolve) {
        const auto input=std::exchange(m_pending,{});
        return input && *input<=m_limits.maximum_keyboard_input && Accept(*input,resolve);
    }
    bool Pointer(CapturePointer pointer,std::uint32_t input,std::uint64_t milliseconds,const Resolve& resolve) {
        if(!focused.Get() || !enabled.Get() || (pointer==CapturePointer::Primary && milliseconds<=m_primary_deadline)) return false;
        return Accept(input,resolve);
    }
    void Restore(InputAssignment value) {assignment.Set(std::move(value));}
    bool Pending() const {return m_pending.has_value();}
private:
    bool Accept(std::uint32_t input,const Resolve& resolve) {
        if(!resolve) return false;
        if(auto value=resolve(input)) {assignment.Set(std::move(*value));return true;}
        return false;
    }
    InputCaptureLimits m_limits;
    std::uint64_t m_primary_deadline{};
    std::optional<std::uint32_t> m_pending;
};
}
