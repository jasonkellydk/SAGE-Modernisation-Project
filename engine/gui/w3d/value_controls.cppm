export module engine.gui.w3d.value_controls;
import std;
import engine.gui.mvvm.observable;
export import engine.gui.w3d.dialog_input;

export namespace engine::gui::w3d {
enum class RangeAction { Decrement, Increment, Minimum, Maximum };
enum class ValueKey { Left,Right,Up,Down,Home,End,Space,Enter,Other };

// wwui/sliderctrl.cpp's discrete range and mouse-to-thumb geometry. Values
// are observable; the view supplies geometry and the game owns setting effects.
class SliderValueModel final {
public:
    mvvm::Observable<int> position{0};
    mvvm::Observable<bool> enabled{true};
    void Range(int minimum,int maximum) {
        if(maximum<minimum) throw std::invalid_argument("reversed slider range");
        m_minimum=minimum;m_maximum=maximum;Set(minimum);
    }
    void Set(int value) { position.Set(std::clamp(value,m_minimum,m_maximum)); }
    int Minimum() const noexcept { return m_minimum; }
    int Maximum() const noexcept { return m_maximum; }
    bool Key(RangeAction action) {
        if(!enabled.Get()) return false;
        const auto current=position.Get();
        switch(action) {
        case RangeAction::Decrement: if(current>m_minimum) Set(current-1);break;
        case RangeAction::Increment: if(current<m_maximum) Set(current+1);break;
        case RangeAction::Minimum:Set(m_minimum);break;
        case RangeAction::Maximum:Set(m_maximum);break;
        }
        return true;
    }
    bool Pointer(float x,const HitRect& client) {
        if(!enabled.Get() || !std::isfinite(x) || !std::isfinite(client.left) || !std::isfinite(client.right) ||
            !std::isfinite(client.top) || !std::isfinite(client.bottom) || client.right<=client.left ||
            client.bottom<=client.top || double(client.bottom)-client.top>double(std::numeric_limits<int>::max())) return false;
        if(x<client.left) Set(m_minimum);
        else if(x>=client.right) Set(m_maximum);
        else {
            const int thumb_width=(static_cast<int>(client.bottom-client.top)-2)/2;
            const float percent=std::clamp((x-(client.left-thumb_width*0.5f))/(client.right-client.left),0.0f,1.0f);
            const auto range=std::int64_t(m_maximum)-m_minimum;
            const auto value=std::int64_t(m_minimum)+static_cast<std::int64_t>(percent*range);
            Set(static_cast<int>(std::clamp(value,std::int64_t(m_minimum),std::int64_t(m_maximum))));
        }
        return true;
    }
private:
    int m_minimum=0,m_maximum=100;
};

class CheckValueModel final {
public:
    mvvm::Observable<bool> checked{false},enabled{true};
    bool Toggle() {
        if(!enabled.Get()) return false;
        checked.Set(!checked.Get());return true;
    }
};

// Value controls consume these keys before dialog focus/default-button
// navigation. Keep portable platform key translation at the host boundary.
bool HandleValueKey(SliderValueModel& model,ValueKey key) {
    switch(key) {
    case ValueKey::Left:case ValueKey::Down:return model.Key(RangeAction::Decrement);
    case ValueKey::Right:case ValueKey::Up:return model.Key(RangeAction::Increment);
    case ValueKey::Home:return model.Key(RangeAction::Minimum);
    case ValueKey::End:return model.Key(RangeAction::Maximum);
    default:return false;
    }
}
bool HandleValueKey(CheckValueModel& model,ValueKey key) {
    return key==ValueKey::Space && model.Toggle();
}
}
