module;
#include <cstdint>
#include <string_view>

export module engine.profiling;

export namespace engine::profiling
{
    inline constexpr int frame_capture_size = 256;
    inline constexpr int frame_capture_interval_ms = 500;

    class Scope
    {
    public:
        explicit Scope(std::string_view name, std::uint32_t color = 0);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&&) = delete;
        Scope& operator=(Scope&&) = delete;
    private:
        void* m_context{};
    };

    void mark_frame();
    void mark_frame(std::string_view name);
    void plot(std::string_view name, std::int64_t value);
    void message(std::string_view text);
    bool connected() noexcept;
    void capture_frame(const void* image, int width, int height, int offset, bool flip);
}
