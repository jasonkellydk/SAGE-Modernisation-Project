export module engine.gui.w3d.dialog_input;
import std;

export namespace engine::gui::w3d
{
struct HitRect { float left{},top{},right{},bottom{}; };
struct InputTarget { std::uint32_t id{}; HitRect bounds; bool enabled{true}; bool visible{true}; bool focusable{true}; };
struct DialogInputState { std::optional<std::uint32_t> focused,pressed,hovered; };
bool CanFocus(const InputTarget& target) { return target.enabled && target.visible && target.focusable; }
// DialogBase::On_Activate / Set_Default_Focus: restore a live parent control,
// otherwise select the first eligible control in the caller's resource order.
void ActivateDialog(DialogInputState& state,std::span<const InputTarget> targets,
    std::optional<std::uint32_t> previous={}) {
    state={};
    for(const auto& target:targets) if(previous==target.id && CanFocus(target)) {state.focused=target.id;return;}
    for(const auto& target:targets) if(CanFocus(target)) {state.focused=target.id;return;}
}
std::optional<std::uint32_t> HitTest(std::span<const InputTarget> targets,float x,float y) {
    for(const auto& target:targets) if(target.visible && target.enabled && x>=target.bounds.left && x<target.bounds.right &&
        y>=target.bounds.top && y<target.bounds.bottom) return target.id;
    return {};
}
void PointerMove(DialogInputState& state,std::span<const InputTarget> targets,float x,float y) {
    state.hovered=HitTest(targets,x,y);
}
void PointerPress(DialogInputState& state,std::span<const InputTarget> targets,float x,float y) {
    state.pressed=HitTest(targets,x,y);
    for(const auto& target:targets) if(state.pressed==target.id && CanFocus(target)) {state.focused=target.id;break;}
}
std::optional<std::uint32_t> PointerRelease(DialogInputState& state,std::span<const InputTarget> targets,float x,float y) {
    const auto pressed=std::exchange(state.pressed,{});
    return pressed && pressed==HitTest(targets,x,y) ? pressed : std::nullopt;
}
void MoveFocus(DialogInputState& state,std::span<const InputTarget> targets,bool backwards=false) {
    if(targets.empty()) { state.focused.reset();return; }
    std::size_t start=backwards ? 0 : targets.size()-1;
    for(std::size_t i=0;i<targets.size();++i) if(state.focused==targets[i].id) { start=i;break; }
    for(std::size_t offset=1;offset<=targets.size();++offset) {
        const auto index=backwards ? (start+targets.size()-offset)%targets.size() : (start+offset)%targets.size();
        if(CanFocus(targets[index])) { state.focused=targets[index].id;return; }
    }
    state.focused.reset();
}
std::optional<std::uint32_t> ActivateFocused(const DialogInputState& state,std::span<const InputTarget> targets) {
    for(const auto& target:targets) if(state.focused==target.id && CanFocus(target)) return state.focused;
    return {};
}
}
