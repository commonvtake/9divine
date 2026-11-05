#pragma once
#include "euphoria/core/Base.h"
#include <string>

struct GLFWwindow;

namespace eu {

struct AppConfig {
    std::string title = "Euphoria Engine";
    int width = 1600, height = 900;
    bool vsync = true;
    bool fullscreen = false;
    bool enableImGui = true;
    bool imguiDocking = false;
    std::string imguiIniPath;            // empty = don't save layout
    // Automated testing: save a PNG of frame N, then quit.
    int screenshotFrame = -1;
    std::string screenshotPath;
    int maxFrames = -1;                   // quit after N frames (testing)
    float fixedDelta = 0.0f;              // > 0: every frame advances exactly this long (deterministic screenshots)
    int benchmarkFrames = 0;              // > 0: record N frames, print a frame-time report, quit
    std::string benchmarkReport = "benchmark.txt";
};

class Time {
public:
    static float Delta() { return s_Delta; }           // scaled
    static float UnscaledDelta() { return s_Unscaled; }
    static double Elapsed() { return s_Elapsed; }
    static float Scale() { return s_Scale; }
    static void SetScale(float s) { s_Scale = s < 0 ? 0 : s; }
    static uint64_t FrameCount() { return s_Frame; }

    // Hit-stop: run the game at `timeScale` (0 = frozen) for `seconds` of real time, the
    // brief freeze that gives a sword hit its weight. Overlapping calls keep the longer one.
    // Real-time things (UI, screen fades, camera shake) keep moving.
    static void HitStop(float seconds, float timeScale = 0.0f) {
        if (seconds > s_HitStop) { s_HitStop = seconds; s_HitStopScale = timeScale; }
    }
    static bool InHitStop() { return s_HitStop > 0.0f; }
    static void ClearHitStop() { s_HitStop = 0.0f; }
    // Advances one frame of real time (the main loop calls this; tests can too).
    static void Advance(float unscaledDt) {
        s_Unscaled = unscaledDt;
        float scale = s_Scale;
        if (s_HitStop > 0.0f) { scale *= s_HitStopScale; s_HitStop -= unscaledDt; }
        s_Delta = unscaledDt * scale;
        s_Elapsed += unscaledDt;
    }
private:
    static float s_Delta, s_Unscaled, s_Scale, s_HitStop, s_HitStopScale;
    static double s_Elapsed;
    static uint64_t s_Frame;
    friend class Application;
};

class Application {
public:
    explicit Application(const AppConfig& config);
    virtual ~Application();

    void Run();
    void Close() { m_Running = false; }

    GLFWwindow* NativeWindow() const { return m_Window; }
    ivec2 FramebufferSize() const;
    ivec2 WindowSize() const;
    void SetTitle(const std::string& title);
    void SetVSync(bool on);
    void SetFullscreen(bool on);
    bool IsFullscreen() const { return m_Fullscreen; }
    const AppConfig& Config() const { return m_Config; }

    // Saves the current back buffer to a PNG (call before the frame is presented).
    bool SaveScreenshot(const std::string& path);
    void RequestScreenshot(const std::string& path) { m_PendingScreenshot = path; }

    static Application& Get() { return *s_Instance; }

protected:
    virtual void OnInit() {}
    virtual void OnUpdate(float dt) {}
    virtual void OnImGui() {}
    virtual void OnShutdown() {}
    virtual bool OnCloseRequested() { return true; }
    // Files or folders dragged from the OS (Explorer / Finder) and dropped on the window.
    virtual void OnFilesDropped(const std::vector<std::string>& paths) {}

private:
    float SmoothFrameTime(float dt);
    bool m_VSync = true;
    float m_RefreshInterval = 0.0f;   // seconds per monitor refresh (0 = unknown)
    float m_TimeResidual = 0.0f;

    void InitImGui();
    void ApplyEditorStyle();

    AppConfig m_Config;
    GLFWwindow* m_Window = nullptr;
    bool m_Running = true;
    bool m_Fullscreen = false;
    ivec2 m_WindowedPos{ 100 }, m_WindowedSize{ 1280, 720 };
    std::string m_PendingScreenshot;
    static Application* s_Instance;
};

AppConfig ParseCommandLine(int argc, char** argv, AppConfig base, std::vector<std::string>* positional = nullptr);

} // namespace eu
