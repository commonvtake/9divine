#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <mutex>

namespace eu {

enum class LogLevel { Trace, Info, Warn, Error };

struct LogEntry {
    LogLevel level;
    std::string message;
    double time;
};

// Tiny "{}"-style formatter so we don't need an extra dependency.
namespace detail {
inline void FormatTo(std::ostringstream& os, const char* fmt) { os << fmt; }
template <typename T, typename... Rest>
void FormatTo(std::ostringstream& os, const char* fmt, T&& v, Rest&&... rest) {
    while (*fmt) {
        if (fmt[0] == '{' && fmt[1] == '}') {
            os << v;
            FormatTo(os, fmt + 2, std::forward<Rest>(rest)...);
            return;
        }
        os << *fmt++;
    }
}
} // namespace detail

template <typename... Args>
std::string Format(const char* fmt, Args&&... args) {
    std::ostringstream os;
    detail::FormatTo(os, fmt, std::forward<Args>(args)...);
    return os.str();
}

class Log {
public:
    static void Write(LogLevel level, const std::string& msg);
    static const std::vector<LogEntry>& Entries() { return s_Entries; }
    static void Clear();
    static std::mutex& Mutex() { return s_Mutex; }

private:
    static std::vector<LogEntry> s_Entries;
    static std::mutex s_Mutex;
};

} // namespace eu

#define EU_TRACE(...) ::eu::Log::Write(::eu::LogLevel::Trace, ::eu::Format(__VA_ARGS__))
#define EU_INFO(...)  ::eu::Log::Write(::eu::LogLevel::Info,  ::eu::Format(__VA_ARGS__))
#define EU_WARN(...)  ::eu::Log::Write(::eu::LogLevel::Warn,  ::eu::Format(__VA_ARGS__))
#define EU_ERROR(...) ::eu::Log::Write(::eu::LogLevel::Error, ::eu::Format(__VA_ARGS__))
