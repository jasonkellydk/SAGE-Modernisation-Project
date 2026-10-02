module;
#include <SDL3/SDL.h>
export module engine.platform.adapters.sdl3.keys;
import std;
import engine.platform.events;

export namespace engine::platform::sdl3 {
// One physical-key mapping serves event posting, event polling and typed
// keyboard-state queries. SDL identities remain inside the platform adapter.
inline constexpr auto native_scan_codes=[] {
    std::array<SDL_Scancode,static_cast<unsigned>(KeyCode::count)> keys{};
    const auto set=[&](KeyCode key,SDL_Scancode native) {keys[static_cast<unsigned>(key)]=native;};
    const auto range=[&](KeyCode first,SDL_Scancode native,unsigned count) {
        for(unsigned i=0;i<count;++i) keys[static_cast<unsigned>(first)+i]=static_cast<SDL_Scancode>(native+i);
    };
    range(KeyCode::a,SDL_SCANCODE_A,26);
    range(KeyCode::digit_1,SDL_SCANCODE_1,9);set(KeyCode::digit_0,SDL_SCANCODE_0);
    range(KeyCode::f1,SDL_SCANCODE_F1,12);range(KeyCode::f13,SDL_SCANCODE_F13,12);
    range(KeyCode::keypad_1,SDL_SCANCODE_KP_1,9);set(KeyCode::keypad_0,SDL_SCANCODE_KP_0);
    set(KeyCode::enter,SDL_SCANCODE_RETURN);set(KeyCode::keypad_enter,SDL_SCANCODE_KP_ENTER);
    set(KeyCode::backspace,SDL_SCANCODE_BACKSPACE);set(KeyCode::escape,SDL_SCANCODE_ESCAPE);set(KeyCode::tab,SDL_SCANCODE_TAB);
    set(KeyCode::up,SDL_SCANCODE_UP);set(KeyCode::down,SDL_SCANCODE_DOWN);set(KeyCode::left,SDL_SCANCODE_LEFT);set(KeyCode::right,SDL_SCANCODE_RIGHT);
    set(KeyCode::del,SDL_SCANCODE_DELETE);set(KeyCode::space,SDL_SCANCODE_SPACE);set(KeyCode::home,SDL_SCANCODE_HOME);set(KeyCode::end,SDL_SCANCODE_END);
    set(KeyCode::page_up,SDL_SCANCODE_PAGEUP);set(KeyCode::page_down,SDL_SCANCODE_PAGEDOWN);set(KeyCode::insert,SDL_SCANCODE_INSERT);
    set(KeyCode::minus,SDL_SCANCODE_MINUS);set(KeyCode::equals,SDL_SCANCODE_EQUALS);
    set(KeyCode::left_bracket,SDL_SCANCODE_LEFTBRACKET);set(KeyCode::right_bracket,SDL_SCANCODE_RIGHTBRACKET);
    set(KeyCode::semicolon,SDL_SCANCODE_SEMICOLON);set(KeyCode::apostrophe,SDL_SCANCODE_APOSTROPHE);set(KeyCode::grave,SDL_SCANCODE_GRAVE);
    set(KeyCode::backslash,SDL_SCANCODE_BACKSLASH);set(KeyCode::comma,SDL_SCANCODE_COMMA);set(KeyCode::period,SDL_SCANCODE_PERIOD);set(KeyCode::slash,SDL_SCANCODE_SLASH);
    set(KeyCode::caps_lock,SDL_SCANCODE_CAPSLOCK);set(KeyCode::num_lock,SDL_SCANCODE_NUMLOCKCLEAR);set(KeyCode::scroll_lock,SDL_SCANCODE_SCROLLLOCK);
    set(KeyCode::print_screen,SDL_SCANCODE_PRINTSCREEN);set(KeyCode::pause,SDL_SCANCODE_PAUSE);
    set(KeyCode::keypad_minus,SDL_SCANCODE_KP_MINUS);set(KeyCode::keypad_multiply,SDL_SCANCODE_KP_MULTIPLY);set(KeyCode::keypad_plus,SDL_SCANCODE_KP_PLUS);
    set(KeyCode::keypad_period,SDL_SCANCODE_KP_PERIOD);set(KeyCode::keypad_divide,SDL_SCANCODE_KP_DIVIDE);
    set(KeyCode::left_shift,SDL_SCANCODE_LSHIFT);set(KeyCode::right_shift,SDL_SCANCODE_RSHIFT);
    set(KeyCode::left_control,SDL_SCANCODE_LCTRL);set(KeyCode::right_control,SDL_SCANCODE_RCTRL);
    set(KeyCode::left_alt,SDL_SCANCODE_LALT);set(KeyCode::right_alt,SDL_SCANCODE_RALT);
    set(KeyCode::left_command,SDL_SCANCODE_LGUI);set(KeyCode::right_command,SDL_SCANCODE_RGUI);set(KeyCode::application,SDL_SCANCODE_APPLICATION);
    return keys;
}();
constexpr SDL_Scancode NativeScanCode(KeyCode key) noexcept {
    const auto index=static_cast<unsigned>(key);
    return index<native_scan_codes.size() ? native_scan_codes[index] : SDL_SCANCODE_UNKNOWN;
}
constexpr KeyCode PortableKey(SDL_Scancode native) noexcept {
    if(native==SDL_SCANCODE_UNKNOWN) return KeyCode::unknown;
    for(unsigned i=1;i<native_scan_codes.size();++i) if(native_scan_codes[i]==native) return static_cast<KeyCode>(i);
    return KeyCode::unknown;
}
}
