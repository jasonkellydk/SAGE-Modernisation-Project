export module engine.gui.w3d.navigation;
import std;
import engine.gui.mvvm.observable;
import engine.gui.w3d.dialog_input;

export namespace engine::gui::w3d {
struct GroupControl {bool enabled{true};bool begins_group{};};
// DialogBase::Find_Next_Group_Control builds an enabled control list before
// considering group boundaries. Non-focusable controls still delimit groups.
std::optional<std::size_t> FindGroupControl(std::span<const GroupControl> controls,
    std::optional<std::size_t> current,int direction) {
    std::vector<std::size_t> enabled;
    for(std::size_t i=0;i<controls.size();++i) if(controls[i].enabled) enabled.push_back(i);
    if(enabled.empty()) return {};
    std::size_t index{};
    for(std::size_t i=0;i<enabled.size();++i) if(current==enabled[i]) {index=i;break;}
    if(direction<0) {
        if(index==0 || controls[enabled[index]].begins_group) {
            std::optional<std::size_t> last;
            for(auto next=index+1;next<enabled.size() && !controls[enabled[next]].begins_group;++next) last=enabled[next];
            return last; // The source returns null for a one-control group.
        }
        return enabled[index-1];
    }
    if(index+1==enabled.size() || controls[enabled[index+1]].begins_group) {
        while(index>0 && !controls[enabled[index]].begins_group) --index;
        return enabled[index];
    }
    return enabled[index+1];
}

enum class SelectionAction {Previous,Next,First,Last};
class TabSelectionModel final {
public:
    explicit TabSelectionModel(unsigned count):m_count(count) {}
    mvvm::Observable<unsigned> position{0};
    unsigned Count() const noexcept {return m_count;}
    bool Select(std::int64_t index) {
        if(!m_count) return false;
        position.Set(static_cast<unsigned>(std::clamp(index,std::int64_t{0},std::int64_t(m_count)-1)));return true;
    }
    bool Key(SelectionAction action) {
        if(!m_count) return false;
        const auto current=std::int64_t(position.Get());
        switch(action) {
        case SelectionAction::Previous:return Select(current-1);
        case SelectionAction::Next:return Select(current+1);
        case SelectionAction::First:return Select(0);
        case SelectionAction::Last:return Select(std::int64_t(m_count)-1);
        }
        return false;
    }
private:
    unsigned m_count{};
};

// MenuEntryCtrl::Center_Mouse has a distinct authored anchor for left and
// centered captions. The platform adapter owns the actual pointer movement.
std::optional<std::array<float,2>> MenuEntryCursor(HitRect bounds,float caption_width,bool left_aligned) {
    if(!std::isfinite(bounds.left) || !std::isfinite(bounds.right) || !std::isfinite(bounds.top) ||
        !std::isfinite(bounds.bottom) || !std::isfinite(caption_width) || caption_width<0 ||
        bounds.right<bounds.left || bounds.bottom<bounds.top) return {};
    const float x=left_aligned ? bounds.left+caption_width*0.5f : bounds.left+(bounds.right-bounds.left-caption_width)*0.5f;
    const float y=bounds.top+(bounds.bottom-bounds.top)*0.5f;
    if(!std::isfinite(x) || !std::isfinite(y)) return {};
    return std::array{std::trunc(x),std::trunc(y)};
}
}
