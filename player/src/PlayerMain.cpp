// 9divine Player - the runtime used by exported games.
//
//   9divine-player                       runs the game described by game.json next to the exe
//   9divine-player <projectDir>          runs a project folder directly
//   9divine-player --build-demo <dir>    regenerates the sample project
//   9divine-player --host [port]         start hosting a multiplayer session
//   9divine-player --join <ip> [port]    join a session
//   F3 = stats overlay, F11 = fullscreen, F12 = screenshot
#include <nd/9divine.h>
#include <nd/core/FileSystem.h>
#include <nd/renderer/Renderer.h>
#include <nd/renderer/Framebuffer.h>
#include <nd/assets/AssetManager.h>
#include <imgui.h>
#include "DemoBuilder.h"
#include <cstring>

using namespace nd;

class GamePlayer : public Application {
public:
    GamePlayer(const AppConfig& cfg, fs::path project) : Application(cfg), m_ProjectDir(std::move(project)) {}

protected:
    void OnInit() override {
        Input::LoadMap(Project::Info().inputMap);
        FramebufferSpec spec;
        ivec2 size = FramebufferSize();
        spec.width = size.x;
        spec.height = size.y;
        m_Framebuffer = MakeScope<Framebuffer>(spec);

        auto scene = SceneManager::LoadNow(Project::Info().startScene);
        if (!scene) {
            ND_ERROR("Start scene '{}' could not be loaded", Project::Info().startScene);
            scene = MakeRef<Scene>();
        }
        SceneManager::SetActive(scene);
        SceneManager::onQuitRequested = [this] { Close(); };
        scene->OnRuntimeStart();
        if (hostPort > 0) Network::Host((uint16_t)hostPort);
        if (!joinAddress.empty()) Network::Connect(joinAddress, (uint16_t)joinPort);
    }

    void OnUpdate(float) override {
        if (Input::RawKeyPressed(Key::F3)) m_Overlay = !m_Overlay;
        if (Input::RawKeyPressed(Key::F11)) SetFullscreen(!IsFullscreen());
        if (Input::RawKeyPressed(Key::F12)) RequestScreenshot("screenshot_" + std::to_string(Time::FrameCount()) + ".png");

        SceneManager::Update();
        Ref<Scene> scene = SceneManager::Active();
        if (!scene) return;
        scene->OnUpdateRuntime(Time::Delta());
        SceneManager::Update();   // a script may have requested a switch this frame
        scene = SceneManager::Active();

        ivec2 size = FramebufferSize();
        if (size.x <= 0 || size.y <= 0) return;   // minimized
        m_Framebuffer->Resize(size.x, size.y);
        float aspect = (float)size.x / size.y;
        RenderCamera cam;
        if (!scene->GetRuntimeCamera(aspect, cam.view, cam.projection, cam.position)) {
            cam = Renderer::MakeCamera(glm::translate(mat4(1.0f), vec3(0, 5, 15)), 60.0f, aspect, 0.1f, 500.0f);
        }
        Renderer::RenderScene(*scene, cam, *m_Framebuffer);

        m_Framebuffer->Bind();
        ivec2 win = WindowSize();
        vec2 mouse = Input::MousePosition() * vec2((float)size.x / std::max(1, win.x), (float)size.y / std::max(1, win.y));
        scene->RenderGameUI(size.x, size.y, mouse, Input::RawMouseDown(Mouse::Left), Input::IsMousePressed(Mouse::Left));
        m_Framebuffer->BlitToScreen(size.x, size.y);
    }

    void OnImGui() override {
        if (!m_Overlay) return;
        auto& st = Profiler::Stats();
        ImGui::SetNextWindowPos({ 10, 10 });
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("##overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
        ImGui::Text("%.0f FPS  (%.2f ms)", st.fps, st.frameMs);
        ImGui::PlotLines("##ft", Profiler::FrameHistory(), Profiler::kHistory, Profiler::HistoryOffset(), nullptr, 0.0f, 40.0f, ImVec2(240, 50));
        ImGui::Text("Draw calls: %u   Triangles: %llu", st.drawCalls, (unsigned long long)st.triangles);
        ImGui::Text("Entities: %u   Bodies: %u   Particles: %u", st.entities, st.physicsBodies, st.particles);
        ImGui::Text("Memory: %.1f MB", st.memoryBytes / (1024.0 * 1024.0));
        for (auto& s : Profiler::Sections()) ImGui::Text("  %-12s %.2f ms", s.name.c_str(), s.avgMs);
        if (Network::IsActive()) ImGui::Text("Net: %s  ping %.0f ms", Network::Status().c_str(), Network::PingMs());
        ImGui::End();
    }

    void OnShutdown() override {
        Network::Disconnect();
        if (auto scene = SceneManager::Active()) if (scene->IsRunning()) scene->OnRuntimeStop();
        SceneManager::SetActive(nullptr);
        UI::Shutdown();
    }

public:
    int hostPort = 0;
    std::string joinAddress;
    int joinPort = 7777;

private:
    fs::path m_ProjectDir;
    Scope<Framebuffer> m_Framebuffer;
    bool m_Overlay = false;
};

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--build-demo") == 0) {
            fs::path dir = i + 1 < argc ? fs::path(argv[i + 1]) : fs::path(ND_SOURCE_DIR) / "projects" / "Demo";
            return demo::BuildDemoProject(dir) ? 0 : 1;
        }
    }

    AppConfig base;
    base.title = "9divine Game";
    base.enableImGui = true;
    AppConfig cfg = ParseCommandLine(argc, argv, base, nullptr);

    // Find the project: argument > game.json next to the exe > bundled demo
    fs::path project;
    std::vector<std::string> paths;
    auto isNum = [](const char* s) { return s && *s && std::strspn(s, "0123456789") == std::strlen(s); };
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--screenshot" || a == "--screenshot-frame" || a == "--frames" || a == "--width" || a == "--height") { i++; continue; }
        if (a == "--host") { if (i + 1 < argc && isNum(argv[i + 1])) i++; continue; }
        if (a == "--join") { i++; if (i + 1 < argc && isNum(argv[i + 1])) i++; continue; }
        if (a.rfind("--", 0) == 0) continue;
        paths.push_back(a);
    }
    if (!paths.empty()) project = paths[0];
    else if (auto text = FileSystem::ReadText(Platform::ExecutableDir() / "game.json")) {
        try { project = Platform::ExecutableDir() / json::parse(*text).value("project", "Game"); } catch (...) {}
    }
    if (project.empty()) project = fs::path(ND_SOURCE_DIR) / "projects" / "Demo";
    if (!Project::Load(project)) return 1;

    auto& info = Project::Info();
    cfg.title = info.name;
    bool sizeFromArgs = false;
    for (int i = 1; i < argc; i++) if (!std::strcmp(argv[i], "--width") || !std::strcmp(argv[i], "--height")) sizeFromArgs = true;
    if (!sizeFromArgs) { cfg.width = info.windowWidth; cfg.height = info.windowHeight; }
    bool fsArg = false;
    for (int i = 1; i < argc; i++) if (!std::strcmp(argv[i], "--windowed") || !std::strcmp(argv[i], "--fullscreen")) fsArg = true;
    if (!fsArg) cfg.fullscreen = info.fullscreen;
    cfg.vsync = cfg.vsync && info.vsync;

    GamePlayer app(cfg, project);
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--host") app.hostPort = (i + 1 < argc && isNum(argv[i + 1])) ? std::atoi(argv[++i]) : 7777;
        if (a == "--join" && i + 1 < argc) {
            app.joinAddress = argv[++i];
            if (i + 1 < argc && isNum(argv[i + 1])) app.joinPort = std::atoi(argv[++i]);
        }
    }
    app.Run();
    return 0;
}
