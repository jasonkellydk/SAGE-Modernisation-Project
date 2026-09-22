module;
#include <cstdarg>
#include <string>
#include <string_view>

export module engine.debug;

export namespace engine::debug
{
    void initialize(std::string_view file_path = {});
    void shutdown() noexcept;
    void set_headless(bool headless) noexcept;
    std::string log_file_path();

    class FileLog
    {
    public:
        explicit FileLog(std::string_view file_path);
        ~FileLog();
        FileLog(const FileLog&) = delete;
        FileLog& operator=(const FileLog&) = delete;
        void write(const char* format, ...);
    private:
        struct State;
        State* m_state{};
    };

    void log_trace(const char* format, ...);
    void log_info(const char* format, ...);
    void log_warning(const char* format, ...);
    void log_error(const char* format, ...);
    void log_critical(const char* format, ...);
    [[noreturn]] void panic(const char* format, ...);
    void assert_condition(bool condition, const char* expression, const char* file, int line, const char* format, ...);
    void invariant(bool condition, const char* expression, const char* file, int line, const char* format, ...);
}
