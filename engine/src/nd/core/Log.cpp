#include "nd/core/Log.h"
#include <cstdio>
#include <chrono>

namespace nd {

std::vector<LogEntry> Log::s_Entries;
std::mutex Log::s_Mutex;

static double SecondsSinceStart() {
    static auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void Log::Write(LogLevel level, const std::string& msg) {
    static const char* names[] = { "TRACE", "INFO", "WARN", "ERROR" };
    std::lock_guard<std::mutex> lock(s_Mutex);
    double t = SecondsSinceStart();
    std::fprintf(level >= LogLevel::Warn ? stderr : stdout, "[%8.3f] [%s] %s\n", t, names[(int)level], msg.c_str());
    std::fflush(level >= LogLevel::Warn ? stderr : stdout);
    s_Entries.push_back({ level, msg, t });
    if (s_Entries.size() > 2000) s_Entries.erase(s_Entries.begin(), s_Entries.begin() + 500);
}

void Log::Clear() {
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_Entries.clear();
}

} // namespace nd
