#include "nd/core/Profiler.h"
#include "nd/core/FileSystem.h"
#include <cstring>

namespace nd {

FrameStats Profiler::s_Stats;
std::vector<ProfileSection> Profiler::s_Sections;
float Profiler::s_History[Profiler::kHistory] = {};
int Profiler::s_HistoryOffset = 0;
std::chrono::steady_clock::time_point Profiler::s_FrameStart = std::chrono::steady_clock::now();

void Profiler::BeginFrame() {
    s_FrameStart = std::chrono::steady_clock::now();
    s_Stats.drawCalls = 0;
    s_Stats.triangles = 0;
    for (auto& s : s_Sections) s.ms = 0.0f;
}

void Profiler::EndFrame() {
    float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - s_FrameStart).count();
    s_Stats.frameMs = ms;
    s_History[s_HistoryOffset] = ms;
    s_HistoryOffset = (s_HistoryOffset + 1) % kHistory;
    // FPS from the averaged history
    float sum = 0.0f;
    int n = 0;
    for (float h : s_History) if (h > 0.0f) { sum += h; n++; }
    s_Stats.fps = n > 0 && sum > 0.0f ? 1000.0f / (sum / n) : 0.0f;
    static int memCounter = 0;
    if (memCounter++ % 30 == 0) s_Stats.memoryBytes = Platform::ProcessMemoryBytes();
    for (auto& s : s_Sections) s.avgMs = s.avgMs * 0.9f + s.ms * 0.1f;
}

void Profiler::Record(const char* name, float ms) {
    for (auto& s : s_Sections) {
        if (s.name == name) { s.ms += ms; return; }
    }
    s_Sections.push_back({ name, ms, ms });
}

} // namespace nd
