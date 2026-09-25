module;
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
import engine.debug;

module engine.debug;

namespace engine::debug
{
namespace
{
std::mutex logger_mutex;
bool headless = false;
std::string current_log_path;

spdlog::level::level_enum to_spdlog_level(int level)
{
    return static_cast<spdlog::level::level_enum>(level);
}

std::string format_message(const char* format, va_list args)
{
    if (format == nullptr) return {};
    std::size_t capacity = 256;
    for (;;) {
        std::string message(capacity, '\0');
        va_list copy;
        va_copy(copy, args);
        const int written = std::vsnprintf(message.data(), message.size(), format, copy);
        va_end(copy);
        if (written < 0) {
            capacity *= 2;
            continue;
        }
        if (static_cast<std::size_t>(written) < capacity) {
            message.resize(static_cast<std::size_t>(written));
            return message;
        }
        capacity = static_cast<std::size_t>(written) + 1;
    }
}

void write(int level, const char* format, va_list args)
{
    auto logger = spdlog::get("engine");
    if (!logger) {
        engine::debug::initialize();
        logger = spdlog::get("engine");
    }
    logger->log(to_spdlog_level(level), "{}", format_message(format, args));
}

void failed_assertion(const char* expression, const char* file, int line, const char* format, va_list args)
{
    auto logger = spdlog::get("engine");
    if (!logger) {
        engine::debug::initialize();
        logger = spdlog::get("engine");
    }
    logger->critical("Assertion failed: {} ({}:{}): {}", expression, file, line, format_message(format, args));
    logger->flush();
    (void)headless;
    std::abort();
}
}

struct FileLog::State { std::shared_ptr<spdlog::logger> logger; };

void initialize(std::string_view file_path)
{
    std::scoped_lock lock(logger_mutex);
    if (auto existing = spdlog::get("engine")) {
        // A message logged before startup created a console-only logger; attach the file sink now.
        if (!file_path.empty() && current_log_path.empty()) {
            current_log_path = std::string(file_path);
            existing->sinks().emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(current_log_path, true));
        }
        return;
    }
    std::vector<spdlog::sink_ptr> sinks;
    sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    if (!file_path.empty()) {
        current_log_path = std::string(file_path);
        sinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(current_log_path, true));
    }
    auto logger = std::make_shared<spdlog::logger>("engine", sinks.begin(), sinks.end());
    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    logger->set_level(spdlog::level::trace);
    spdlog::register_logger(logger);
    spdlog::set_default_logger(std::move(logger));
}

void shutdown() noexcept { spdlog::shutdown(); }
void set_headless(bool value) noexcept { headless = value; }
std::string log_file_path() { return current_log_path; }

FileLog::FileLog(std::string_view path) : m_state(new State)
{
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(std::string(path), true);
    m_state->logger = std::make_shared<spdlog::logger>(std::string(path), sink);
    m_state->logger->set_pattern("%v");
}
FileLog::~FileLog() { delete m_state; }
void FileLog::write(const char* format, ...)
{
    va_list args; va_start(args, format);
    m_state->logger->info("{}", format_message(format, args));
    va_end(args);
}

void log_trace(const char* format, ...) { va_list args; va_start(args, format); write(spdlog::level::trace, format, args); va_end(args); }
void log_info(const char* format, ...) { va_list args; va_start(args, format); write(spdlog::level::info, format, args); va_end(args); }
void log_warning(const char* format, ...) { va_list args; va_start(args, format); write(spdlog::level::warn, format, args); va_end(args); }
void log_error(const char* format, ...) { va_list args; va_start(args, format); write(spdlog::level::err, format, args); va_end(args); }
void log_critical(const char* format, ...) { va_list args; va_start(args, format); write(spdlog::level::critical, format, args); va_end(args); }

[[noreturn]] void panic(const char* format, ...)
{
    va_list args; va_start(args, format); write(spdlog::level::critical, format, args); va_end(args); std::abort();
}
void assert_condition(bool condition, const char* expression, const char* file, int line, const char* format, ...)
{
    if (condition) return;
    va_list args; va_start(args, format); failed_assertion(expression, file, line, format, args); va_end(args);
}
void invariant(bool condition, const char* expression, const char* file, int line, const char* format, ...)
{
#if defined(RTS_DEBUG)
    if (!condition) { va_list args; va_start(args, format); failed_assertion(expression, file, line, format, args); va_end(args); }
#else
    (void)condition; (void)expression; (void)file; (void)line; (void)format;
#endif
}
}
