export module engine.gui.w3d.scrollbar;
import std;
import engine.gui.mvvm.observable;
import engine.gui.w3d.dialog_input;

export namespace engine::gui::w3d {
enum class ScrollAction {Previous,Next,First,Last,PagePrevious,PageNext};
enum class ScrollInteraction {Idle,Previous,Next,Drag};
struct ScrollGeometry {HitRect previous,next,thumb,track;};
// W3D ScrollBarCtrl's range, notification and render-driven hold policy.
// Caller owns content, geometry, capture routing and the two advise effects.
// The view does not advance this model when painting validation probes.
class ScrollBarModel final {
public:
    mvvm::Observable<int> position{0};
    mvvm::Observable<bool> enabled{true};
    using Notify=std::function<void(int)>;
    explicit ScrollBarModel(Notify changed={},Notify page={}):m_changed(std::move(changed)),m_paged(std::move(page)) {}
    void ParentNotifications(Notify changed,Notify page) {m_parent_changed=std::move(changed);m_parent_paged=std::move(page);}
    void Range(int minimum,int maximum) {
        // Set_Range intentionally leaves CurrPos unchanged, including a value
        // outside new bounds. List composition silently synchronizes it later.
        m_minimum=minimum;m_maximum=std::max(minimum,maximum);
    }
    void PageSize(int size) noexcept {m_page=size;}
    int Minimum() const noexcept {return m_minimum;}
    int Maximum() const noexcept {return m_maximum;}
    int PageSize() const noexcept {return m_page;}
    bool Set(std::int64_t requested,bool notify=true) {
        const int next=static_cast<int>(std::clamp(requested,std::int64_t(m_minimum),std::int64_t(m_maximum)));
        if(next==position.Get()) return false;
        position.Set(next);
        if(notify) {if(m_parent_changed) m_parent_changed(next);if(m_changed) m_changed(next);}
        return true;
    }
    void Page(int direction,bool notify=true) {
        Set(std::int64_t(position.Get())+std::int64_t(direction)*m_page,notify);
        if(notify) {if(m_parent_paged) m_parent_paged(direction);if(m_paged) m_paged(direction);} // even when clamped or PageSize==0
    }
    bool Key(ScrollAction action) {
        if(!enabled.Get()) return false;
        switch(action) {
        case ScrollAction::Previous:Set(std::int64_t(position.Get())-1);break;
        case ScrollAction::Next:Set(std::int64_t(position.Get())+1);break;
        case ScrollAction::First:Set(m_minimum);break;
        case ScrollAction::Last:Set(m_maximum);break;
        case ScrollAction::PagePrevious:Page(-1);break;
        case ScrollAction::PageNext:Page(1);break;
        }
        return true;
    }
    bool PointerDown(float x,float y,const ScrollGeometry& geometry) {
        if(!enabled.Get() || !Valid(geometry) || !std::isfinite(x) || !std::isfinite(y)) return false;
        m_captured=m_pressed=true;m_interaction=ScrollInteraction::Idle;
        if(Contains(geometry.previous,x,y)) m_interaction=ScrollInteraction::Previous;
        else if(Contains(geometry.next,x,y)) m_interaction=ScrollInteraction::Next;
        else if(Contains(geometry.thumb,x,y)) {
            if(geometry.track.bottom>geometry.track.top) m_interaction=ScrollInteraction::Drag;
        }
        else if(y<geometry.thumb.top) Page(-1);
        else if(y>geometry.thumb.bottom) Page(1);
        m_mouse_y=y;m_thumb_top=geometry.thumb.top;return true;
    }
    bool PointerMove(float y,const ScrollGeometry& geometry) {
        if(!enabled.Get() || !m_captured || m_interaction!=ScrollInteraction::Drag ||
            !Valid(geometry) || geometry.track.bottom<=geometry.track.top || !std::isfinite(y)) return false;
        const float percent=((m_thumb_top+(y-m_mouse_y))-geometry.track.top)/(geometry.track.bottom-geometry.track.top);
        if(!std::isfinite(percent)) return false;
        if(percent<=0) Set(m_minimum);
        else if(percent>=1) Set(m_maximum);
        else Set(std::int64_t(m_minimum)+static_cast<std::int64_t>(percent*float(std::int64_t(m_maximum)-m_minimum)));
        return true;
    }
    void AdvanceRender() {
        if(!enabled.Get() || !m_captured) return;
        if(m_interaction==ScrollInteraction::Previous) Set(std::int64_t(position.Get())-1);
        else if(m_interaction==ScrollInteraction::Next) Set(std::int64_t(position.Get())+1);
    }
    void LoseFocus() noexcept {m_interaction=ScrollInteraction::Idle;}
    void PointerUp() noexcept {m_captured=m_pressed=false;m_interaction=ScrollInteraction::Idle;}
    void CaptureLost() noexcept {PointerUp();} // deliberate stale-capture hardening
    bool Captured() const noexcept {return m_captured;}
    bool Pressed() const noexcept {return m_pressed;}
    ScrollInteraction Interaction() const noexcept {return m_interaction;}
private:
    static bool Contains(HitRect rectangle,float x,float y) {
        return x>=rectangle.left && x<=rectangle.right && y>=rectangle.top && y<=rectangle.bottom;
    }
    static bool Valid(const ScrollGeometry& geometry) {
        for(const auto rectangle:{geometry.previous,geometry.next,geometry.thumb})
            if(!std::isfinite(rectangle.left) || !std::isfinite(rectangle.top) || !std::isfinite(rectangle.right) ||
                !std::isfinite(rectangle.bottom) || rectangle.right<=rectangle.left || rectangle.bottom<=rectangle.top) return false;
        const auto track=geometry.track;
        return std::isfinite(track.left) && std::isfinite(track.right) && std::isfinite(track.top) && std::isfinite(track.bottom) &&
            track.right>track.left && track.bottom>=track.top;
    }
    Notify m_changed,m_paged,m_parent_changed,m_parent_paged;
    int m_minimum{},m_maximum{100},m_page{10};
    bool m_captured{},m_pressed{};
    ScrollInteraction m_interaction{ScrollInteraction::Idle};
    float m_mouse_y{},m_thumb_top{};
};
}
