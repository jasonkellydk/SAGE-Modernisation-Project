module;
#include <string>
#include <string_view>
#if defined(RTS_PROFILE_TRACY)
#include <tracy/tracy/Tracy.hpp>
#include <tracy/tracy/TracyC.h>
#endif

module engine.profiling;

namespace engine::profiling
{
struct ZoneState
{
#if defined(RTS_PROFILE_TRACY)
    TracyCZoneCtx context{};
#endif
};

Scope::Scope(std::string_view name, std::uint32_t color)
{
#if defined(RTS_PROFILE_TRACY)
    auto* state = new ZoneState;
    const auto* label = name.data();
    const auto location = ___tracy_alloc_srcloc_name(0, "", 0, "", 0, label, name.size(), color);
    state->context = ___tracy_emit_zone_begin_alloc(location, 1);
    m_context = state;
#else
    (void)name; (void)color;
#endif
}
Scope::~Scope()
{
#if defined(RTS_PROFILE_TRACY)
    if (m_context) {
        auto* state = static_cast<ZoneState*>(m_context);
        TracyCZoneEnd(state->context);
        delete state;
    }
#endif
}
void mark_frame() {
#if defined(RTS_PROFILE_TRACY)
    FrameMark;
#endif
}
void mark_frame(std::string_view name) {
#if defined(RTS_PROFILE_TRACY)
    const std::string label(name); FrameMarkNamed(label.c_str());
#else
    (void)name;
#endif
}
void plot(std::string_view name, std::int64_t value) {
#if defined(RTS_PROFILE_TRACY)
    const std::string label(name); TracyPlot(label.c_str(), static_cast<double>(value));
#else
    (void)name; (void)value;
#endif
}
void message(std::string_view text) {
#if defined(RTS_PROFILE_TRACY)
    TracyMessage(text.data(), text.size());
#else
    (void)text;
#endif
}
bool connected() noexcept {
#if defined(RTS_PROFILE_TRACY)
    return TracyIsConnected;
#else
    return false;
#endif
}
void capture_frame(const void* image, int width, int height, int offset, bool flip) {
#if defined(RTS_PROFILE_TRACY)
    FrameImage(image, width, height, offset, flip);
#else
    (void)image; (void)width; (void)height; (void)offset; (void)flip;
#endif
}
}
