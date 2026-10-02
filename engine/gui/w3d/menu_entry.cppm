export module engine.gui.w3d.menu_entry;
import std;
import engine.gui.mvvm.observable;
import engine.gui.w3d.dialog_template;

export namespace engine::gui::w3d {
enum class MenuEntryPhase { Idle,Pressed,Focused };
struct MenuEntryTimings { std::uint64_t press_milliseconds{},focus_milliseconds{}; };
bool IsMenuEntry(const DialogControlDefinition& definition) {
    const auto type=definition.style&15;
    return (definition.kind.ordinal==0x80 || definition.kind.text==u"Button") &&
        type!=2 && type!=3 && (definition.style&0x8000)!=0;
}
// W3D menu entries retain a press through release and focus loss. Their
// command becomes due on the animation deadline. Styling and durations are
// supplied by the composing view model, independently of the renderer.
class MenuEntryModel final {
public:
    explicit MenuEntryModel(MenuEntryTimings timings):m_timings(timings) {
        if(!timings.press_milliseconds || !timings.focus_milliseconds)
            throw std::invalid_argument("menu entry requires positive durations");
    }
    mvvm::Observable<MenuEntryPhase> phase{MenuEntryPhase::Idle};
    // Reports an acquired highlight for the composing view to attach feedback.
    // Retained pointer presses suppress this notification on focus acquisition.
    bool Focus(bool focused,std::uint64_t now) {
        if(focused==m_focused) return false;
        m_focused=focused;
        if(focused) {
            if(!m_mouse_pressed) {SetPhase(MenuEntryPhase::Focused,now);return true;}
        } else {
            if(phase.Get()!=MenuEntryPhase::Pressed) SetPhase(MenuEntryPhase::Idle,now);
            m_mouse_pressed=false;
        }
        return false;
    }
    bool Press(std::uint64_t now,bool pointer=false) {
        if(pointer) m_mouse_pressed=true;
        if(phase.Get()==MenuEntryPhase::Pressed) return false;
        SetPhase(MenuEntryPhase::Pressed,now);return true;
    }
    void Release(std::uint64_t now) {
        m_mouse_pressed=false;
        if(phase.Get()!=MenuEntryPhase::Pressed) SetPhase(m_focused ? MenuEntryPhase::Focused : MenuEntryPhase::Idle,now);
    }
    std::uint64_t Elapsed(std::uint64_t now) const {return now>=m_started ? now-m_started : 0;}
    bool Tick(std::uint64_t now) {
        if(phase.Get()==MenuEntryPhase::Pressed && Elapsed(now)>=m_timings.press_milliseconds) {
            SetPhase(m_focused ? MenuEntryPhase::Focused : MenuEntryPhase::Idle,now);return true;
        }
        if(phase.Get()==MenuEntryPhase::Focused && Elapsed(now)>=m_timings.focus_milliseconds) m_started=now;
        return false;
    }
private:
    void SetPhase(MenuEntryPhase value,std::uint64_t now) {
        if(value==phase.Get()) return;
        m_started=now;phase.Set(value);
    }
    MenuEntryTimings m_timings;
    std::uint64_t m_started{};
    bool m_focused{},m_mouse_pressed{};
};
}
