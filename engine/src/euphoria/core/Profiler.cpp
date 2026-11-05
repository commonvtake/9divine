#include "euphoria/core/Profiler.h"
#include "euphoria/core/FileSystem.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <iomanip>

namespace eu {

FrameStats Profiler::s_Stats;
std::vector<ProfileSection> Profiler::s_Sections;
float Profiler::s_History[Profiler::kHistory] = {};
int Profiler::s_HistoryOffset = 0;
std::chrono::steady_clock::time_point Profiler::s_FrameStart = std::chrono::steady_clock::now();

void Profiler::BeginFrame() {
    s_FrameStart = std::chrono::steady_clock::now();
    s_Stats.drawCalls = 0;
    s_Stats.batchedObjects = 0;
    s_Stats.rtSamples = 0;
    s_Stats.physicsSteps = 0;
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

// ------------------------------------------------------------------ benchmark
namespace {
struct BenchFrame {
    float interval, cpu, gpu;
    int drawCalls, physicsSteps;
    std::vector<std::pair<std::string, float>> sections;
};
std::vector<BenchFrame> s_Bench;
int s_BenchTarget = 0;
}

void Profiler::BeginBenchmark(int frames) { s_Bench.clear(); s_Bench.reserve(frames); s_BenchTarget = frames; }
bool Profiler::BenchmarkActive() { return s_BenchTarget > 0 && (int)s_Bench.size() < s_BenchTarget; }
bool Profiler::BenchmarkDone() { return s_BenchTarget > 0 && (int)s_Bench.size() >= s_BenchTarget; }

void Profiler::RecordBenchmarkFrame(float intervalMs) {
    if (s_BenchTarget <= 0 || (int)s_Bench.size() >= s_BenchTarget) return;
    BenchFrame f{ intervalMs, s_Stats.frameMs, s_Stats.gpuMs, (int)s_Stats.drawCalls, s_Stats.physicsSteps, {} };
    for (auto& sec : s_Sections) if (sec.ms > 0.0f) f.sections.push_back({ sec.name, sec.ms });
    s_Bench.push_back(std::move(f));
}

std::string Profiler::BenchmarkReport(const std::string& title) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    // Skip the first 10% (loading, shader compiles) for the steady-state numbers, but report
    // the warm-up's worst frame separately: that's where load hitches show.
    size_t warm = std::min<size_t>(s_Bench.size() / 10, 60);
    std::vector<float> iv;
    for (size_t i = warm; i < s_Bench.size(); i++) iv.push_back(s_Bench[i].interval);
    if (iv.empty()) return "No frames recorded.\n";
    std::vector<float> sorted = iv;
    std::sort(sorted.begin(), sorted.end());
    auto pct = [&](float p) { return sorted[std::min(sorted.size() - 1, (size_t)(p * (sorted.size() - 1)))]; };
    double sum = 0; for (float v : iv) sum += v;
    double mean = sum / iv.size();
    double var = 0; for (float v : iv) var += (v - mean) * (v - mean);
    double sd = std::sqrt(var / iv.size());
    // "1% low": average of the slowest 1% of frames, as FPS
    size_t n1 = std::max<size_t>(1, sorted.size() / 100);
    double slow = 0; for (size_t i = sorted.size() - n1; i < sorted.size(); i++) slow += sorted[i];
    slow /= n1;
    // Frame-to-frame change: what the eye reads as stutter
    double delta = 0; for (size_t i = 1; i < iv.size(); i++) delta += std::abs(iv[i] - iv[i - 1]);
    delta /= std::max<size_t>(1, iv.size() - 1);
    double median = pct(0.5f);
    int spikes = 0; for (float v : iv) if (v > median * 1.5) spikes++;
    double gpu = 0, cpu = 0, draws = 0, steps = 0;
    std::map<int, int> stepHist;
    for (size_t i = warm; i < s_Bench.size(); i++) {
        gpu += s_Bench[i].gpu; cpu += s_Bench[i].cpu; draws += s_Bench[i].drawCalls; steps += s_Bench[i].physicsSteps;
        stepHist[s_Bench[i].physicsSteps]++;
    }
    size_t n = iv.size();
    o << "=== Euphoria benchmark: " << title << " ===\n";
    o << "frames: " << n << " measured (+" << warm << " warm-up)\n";
    o << "average: " << mean << " ms (" << 1000.0 / mean << " FPS)\n";
    o << "median:  " << median << " ms   p95: " << pct(0.95f) << " ms   p99: " << pct(0.99f) << " ms   max: " << sorted.back() << " ms\n";
    o << "1% low:  " << 1000.0 / slow << " FPS\n";
    o << "jitter:  stddev " << sd << " ms, mean frame-to-frame change " << delta << " ms, " << spikes << " spikes (>1.5x median)\n";
    o << "cpu: " << cpu / n << " ms   gpu (scene): " << gpu / n << " ms   draw calls: " << draws / n << "\n";
    o << "physics steps/frame:";
    for (auto& [k, v] : stepHist) o << "  " << k << ":" << v;
    o << "\n\naverage section times (ms):\n";
    std::map<std::string, double> secSum;
    for (size_t i = warm; i < s_Bench.size(); i++) for (auto& [k, v] : s_Bench[i].sections) secSum[k] += v;
    std::vector<std::pair<std::string, double>> secs(secSum.begin(), secSum.end());
    std::sort(secs.begin(), secs.end(), [](auto& a, auto& b) { return a.second > b.second; });
    for (auto& [k, v] : secs) o << "  " << std::setw(16) << std::left << k << std::right << std::setw(8) << v / n << "\n";
    // Worst frames, including warm-up
    std::vector<size_t> idx(s_Bench.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [](size_t a, size_t b) { return s_Bench[a].interval > s_Bench[b].interval; });
    o << "\nworst frames:\n";
    for (size_t k = 0; k < std::min<size_t>(5, idx.size()); k++) {
        auto& f = s_Bench[idx[k]];
        auto top = f.sections;
        std::sort(top.begin(), top.end(), [](auto& a, auto& b) { return a.second > b.second; });
        o << "  frame " << idx[k] << (idx[k] < warm ? " (warm-up)" : "") << ": " << f.interval << " ms  [";
        for (size_t t = 0; t < std::min<size_t>(4, top.size()); t++) o << (t ? ", " : "") << top[t].first << " " << top[t].second;
        o << "]\n";
    }
    return o.str();
}

} // namespace eu
