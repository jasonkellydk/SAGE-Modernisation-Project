export module games.renegade.presentation.menu.menu_entry;
import std;
export import engine.gui.w3d.menu_entry;
import engine.gui.w3d.dialog_template;

export namespace renegade::presentation {
constexpr engine::gui::w3d::MenuEntryTimings menu_entry_timings{300,1000};
enum class InterfaceControlKind { Entry,Button,Value,Other };
enum class InterfaceInteraction { Focus,Press,PointerDown,KeyboardCommand,PointerUp,ValueChange };
InterfaceControlKind InterfaceKind(const engine::gui::w3d::DialogControlDefinition& definition) {
    if(definition.kind.ordinal!=0x80 && definition.kind.text!=u"Button") return InterfaceControlKind::Other;
    const auto type=definition.style&15;
    if(type==2 || type==3) return InterfaceControlKind::Value;
    return engine::gui::w3d::IsMenuEntry(definition) ? InterfaceControlKind::Entry : InterfaceControlKind::Button;
}
// Original wwui menuentryctrl/buttonctrl sounds are interaction feedback,
// not a side effect of every dialog command or value-control hover.
std::optional<unsigned> InterfaceSound(InterfaceControlKind kind,InterfaceInteraction action) {
    if(kind==InterfaceControlKind::Entry) {
        if(action==InterfaceInteraction::Focus) return 1;
        if(action==InterfaceInteraction::Press) return 0;
    }
    if(kind==InterfaceControlKind::Button &&
        (action==InterfaceInteraction::PointerDown || action==InterfaceInteraction::KeyboardCommand)) return 0;
    return {};
}
struct MenuEntryVisual { int radius_x{5},radius_y{5};std::uint32_t glow{0xff090000u}; };
// wwui/menuentryctrl.cpp Update_State: modern targets use the original 32bpp
// palette. Durations, radius expansions and color transitions stay game-local.
MenuEntryVisual MenuEntryAppearance(engine::gui::w3d::MenuEntryPhase phase,std::uint64_t elapsed,bool pushed=false) {
    using engine::gui::w3d::MenuEntryPhase;
    MenuEntryVisual value;
    if(phase==MenuEntryPhase::Idle) return value;
    if(phase==MenuEntryPhase::Focused) {
        value.radius_x=value.radius_y=static_cast<int>(5+55*(std::min(elapsed,std::uint64_t{1000})/1000.0f));
        const int red=elapsed>500 ? static_cast<int>((1-std::min(elapsed-500,std::uint64_t{500})/500.0f)*16) : 16;
        value.glow=0xff000000u | (std::uint32_t(red)<<16);
        // At the press deadline the original sets its final radii, then starts
        // the focus phase without changing those radii until the next update.
        if(pushed) {value.radius_x=160;value.radius_y=60;}
        return value;
    }
    const float fraction=std::min(elapsed,std::uint64_t{300})/300.0f;
    value.radius_x=static_cast<int>(5+155*fraction);value.radius_y=static_cast<int>(5+25*fraction);
    const float fade=elapsed>=150 ? 1-std::min(elapsed-150,std::uint64_t{150})/150.0f : 1;
    const int red=static_cast<int>(48*fade);
    const int green=static_cast<int>(28.8f*(elapsed>=150 ? fade : elapsed/150.0f));
    value.glow=0xff000000u | (std::uint32_t(red)<<16) | (std::uint32_t(green)<<8);
    if(elapsed>=300) {value.radius_x=160;value.radius_y=60;}
    return value;
}
}
