#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace eu {

struct FrameStats {
    float frameMs = 0.0f;
    float fps = 0.0f;
    uint32_t drawCalls = 0;
    uint64_t triangles = 0;
    uint32_t entities = 0;
    uint32_t physicsBodies = 0;
    uint32_t particles = 0;
    uint32_t batchedObjects = 0;    // objects drawn through automatic instancing this frame
    int rtSamples = 0;              // ray tracing: samples accumulated per pixel (0 = rasterized)
    float gpuMs = 0.0f;             // GPU time of the scene render (timer query, a frame or two old)
    int physicsSteps = 0;           // fixed physics steps run this frame
    size_t memoryBytes = 0;
};

struct ProfileSection {
    std::string name;
    float ms = 0.0f;         // last frame
    float avgMs = 0.0f;      // smoothed
};

// Lightweight CPU profiler. Wrap code in EU_PROFILE_SCOPE("Name") and the timing
// shows up in the editor's Profiler panel and the F3 overlay in games.
class Profiler {
public:
    static constexpr int kHistory = 240;

    static void BeginFrame();
    static void EndFrame();
    static void Record(const char* name, float ms);

    static FrameStats& Stats() { return s_Stats; }
    static const std::vector<ProfileSection>& Sections() { return s_Sections; }
    static const float* FrameHistory() { return s_History; }
    static int HistoryOffset() { return s_HistoryOffset; }

    // Renderer counters
    static void AddDrawCall(uint64_t triangles) { s_Stats.drawCalls++; s_Stats.triangles += triangles; }

    // Benchmark recording (--benchmark N): every frame's interval and section times, then a
    // report with percentiles, 1% lows, jitter and the sections behind the worst frames.
    static void BeginBenchmark(int frames);
    static bool BenchmarkDone();
    static bool BenchmarkActive();      // recording: gameplay code may drive a scripted test path
    static void RecordBenchmarkFrame(float intervalMs);
    static std::string BenchmarkReport(const std::string& title);

private:
    static FrameStats s_Stats;
    static std::vector<ProfileSection> s_Sections;
    static float s_History[kHistory];
    static int s_HistoryOffset;
    static std::chrono::steady_clock::time_point s_FrameStart;
};

class ProfileScope {
public:
    explicit ProfileScope(const char* name) : m_Name(name), m_Start(std::chrono::steady_clock::now()) {}
    ~ProfileScope() {
        float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - m_Start).count();
        Profiler::Record(m_Name, ms);
    }
private:
    const char* m_Name;
    std::chrono::steady_clock::time_point m_Start;
};

} // namespace eu

#define EU_CONCAT_INNER(a, b) a##b
#define EU_CONCAT(a, b) EU_CONCAT_INNER(a, b)
#define EU_PROFILE_SCOPE(name) ::eu::ProfileScope EU_CONCAT(_ndProfile, __LINE__)(name)
