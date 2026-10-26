#include "nd/core/Application.h"
#include "nd/core/Input.h"
#include "nd/core/Log.h"
#include "nd/core/Profiler.h"
#include "nd/core/FileSystem.h"
#include "nd/renderer/GL.h"
#include "nd/renderer/Renderer.h"
#include "nd/audio/Audio.h"
#include "nd/assets/AssetManager.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <stb_image_write.h>
#include <chrono>
#include <cstring>
#include <cstdlib>

namespace nd {

float Time::s_Delta = 0.0f, Time::s_Unscaled = 0.0f, Time::s_Scale = 1.0f;
double Time::s_Elapsed = 0.0;
uint64_t Time::s_Frame = 0;
Application* Application::s_Instance = nullptr;

static void GlfwError(int code, const char* desc) { ND_ERROR("GLFW error {}: {}", code, desc); }

#ifdef ND_GL_DEBUG
static void APIENTRY GLDebug(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* msg, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    ND_WARN("GL: {}", msg);
}
#endif

Application::Application(const AppConfig& config) : m_Config(config) {
    s_Instance = this;
    glfwSetErrorCallback(GlfwError);
#if GLFW_VERSION_MAJOR >= 3 && GLFW_VERSION_MINOR >= 4 && defined(__linux__)
    // Prefer X11 (we build GLFW without Wayland); XWayland handles Wayland desktops.
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    if (!glfwInit()) {
        ND_ERROR("Failed to initialize GLFW");
        std::exit(1);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 0);

    GLFWmonitor* monitor = nullptr;
    int w = config.width, h = config.height;
    if (config.fullscreen) {
        monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        w = mode->width;
        h = mode->height;
        m_Fullscreen = true;
    }
    m_Window = glfwCreateWindow(w, h, config.title.c_str(), monitor, nullptr);
    if (!m_Window) {
        ND_ERROR("Failed to create a window with an OpenGL 3.3 context. Update your graphics drivers.");
        glfwTerminate();
        std::exit(1);
    }
    m_WindowedSize = { config.width, config.height };
    glfwMakeContextCurrent(m_Window);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        ND_ERROR("Failed to load OpenGL functions");
        std::exit(1);
    }
    glfwSwapInterval(config.vsync ? 1 : 0);
    ND_INFO("OpenGL {} | {}", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
#ifdef ND_GL_DEBUG
    if (GLAD_GL_KHR_debug) { glEnable(GL_DEBUG_OUTPUT); glDebugMessageCallback(GLDebug, nullptr); }
#endif

    Input::Init(m_Window);
    Renderer::Init();
    Audio::Init();
    if (config.enableImGui) InitImGui();
}

Application::~Application() {
    if (m_Config.enableImGui) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    Audio::Shutdown();
    AssetManager::ClearCache();
    Renderer::Shutdown();
    g_GLContextAlive = false;
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}

void Application::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (m_Config.imguiDocking) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    static std::string iniPath;
    iniPath = m_Config.imguiIniPath;
    io.IniFilename = iniPath.empty() ? nullptr : iniPath.c_str();

    float xs = 1.0f, ys = 1.0f;
    glfwGetWindowContentScale(m_Window, &xs, &ys);
    float scale = std::max(1.0f, xs);
    auto font = Project::EngineAssetsDir() / "fonts" / "Roboto-Medium.ttf";
    if (fs::exists(font)) io.Fonts->AddFontFromFileTTF(font.string().c_str(), 16.0f * scale);
    ApplyEditorStyle();
    ImGui::GetStyle().ScaleAllSizes(scale);

    ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void Application::ApplyEditorStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 4.0f; s.FrameRounding = 3.0f; s.GrabRounding = 3.0f; s.TabRounding = 3.0f;
    s.WindowBorderSize = 1.0f; s.FrameBorderSize = 0.0f; s.PopupRounding = 4.0f;
    s.ItemSpacing = ImVec2(8, 5); s.FramePadding = ImVec2(6, 4);
    ImVec4* c = s.Colors;
    const ImVec4 bg(0.105f, 0.11f, 0.12f, 1.0f), bg2(0.14f, 0.15f, 0.16f, 1.0f), bg3(0.19f, 0.2f, 0.22f, 1.0f);
    const ImVec4 accent(0.27f, 0.62f, 0.48f, 1.0f), accentHi(0.33f, 0.72f, 0.56f, 1.0f);
    c[ImGuiCol_WindowBg] = bg; c[ImGuiCol_ChildBg] = bg; c[ImGuiCol_PopupBg] = bg2;
    c[ImGuiCol_MenuBarBg] = bg2; c[ImGuiCol_TitleBg] = bg2; c[ImGuiCol_TitleBgActive] = bg2;
    c[ImGuiCol_FrameBg] = bg3; c[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.25f, 0.28f, 1); c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.3f, 0.33f, 1);
    c[ImGuiCol_Header] = bg3; c[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.27f, 0.3f, 1); c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_Button] = bg3; c[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.28f, 0.31f, 1); c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Tab] = bg2; c[ImGuiCol_TabHovered] = accentHi; c[ImGuiCol_TabSelected] = bg3;
    c[ImGuiCol_TabDimmed] = bg2; c[ImGuiCol_TabDimmedSelected] = bg3;
    c[ImGuiCol_CheckMark] = accentHi; c[ImGuiCol_SliderGrab] = accent; c[ImGuiCol_SliderGrabActive] = accentHi;
    c[ImGuiCol_DockingPreview] = accent; c[ImGuiCol_SeparatorHovered] = accent; c[ImGuiCol_SeparatorActive] = accentHi;
    c[ImGuiCol_ResizeGripHovered] = accent; c[ImGuiCol_ResizeGripActive] = accentHi;
    c[ImGuiCol_PlotHistogram] = accent; c[ImGuiCol_PlotLines] = accentHi;
}

void Application::Run() {
    OnInit();
    auto last = std::chrono::steady_clock::now();
    while (m_Running) {
        if (glfwWindowShouldClose(m_Window)) {
            if (OnCloseRequested()) break;
            glfwSetWindowShouldClose(m_Window, GLFW_FALSE);
        }
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        dt = std::min(dt, 0.1f);               // avoid huge steps after a hitch
        Time::s_Unscaled = dt;
        Time::s_Delta = dt * Time::s_Scale;
        Time::s_Elapsed += dt;

        Profiler::BeginFrame();
        Input::BeginFrame();
        glfwPollEvents();
        Input::EndFrame();

        {
            ND_PROFILE_SCOPE("Update");
            OnUpdate(dt);
        }

        if (m_Config.enableImGui) {
            ND_PROFILE_SCOPE("ImGui");
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            OnImGui();
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        Audio::Update();

        Time::s_Frame++;
        if (!m_PendingScreenshot.empty()) {
            SaveScreenshot(m_PendingScreenshot);
            m_PendingScreenshot.clear();
        }
        if (m_Config.screenshotFrame >= 0 && (int)Time::s_Frame == m_Config.screenshotFrame) {
            SaveScreenshot(m_Config.screenshotPath.empty() ? "screenshot.png" : m_Config.screenshotPath);
            m_Running = false;
        }
        if (m_Config.maxFrames > 0 && (int)Time::s_Frame >= m_Config.maxFrames) m_Running = false;

        glfwSwapBuffers(m_Window);
        Profiler::EndFrame();
    }
    OnShutdown();
}

ivec2 Application::FramebufferSize() const { int w, h; glfwGetFramebufferSize(m_Window, &w, &h); return { w, h }; }
ivec2 Application::WindowSize() const { int w, h; glfwGetWindowSize(m_Window, &w, &h); return { w, h }; }
void Application::SetTitle(const std::string& title) { glfwSetWindowTitle(m_Window, title.c_str()); }
void Application::SetVSync(bool on) { glfwSwapInterval(on ? 1 : 0); }

void Application::SetFullscreen(bool on) {
    if (on == m_Fullscreen) return;
    if (on) {
        glfwGetWindowPos(m_Window, &m_WindowedPos.x, &m_WindowedPos.y);
        glfwGetWindowSize(m_Window, &m_WindowedSize.x, &m_WindowedSize.y);
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        glfwSetWindowMonitor(m_Window, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(m_Window, nullptr, m_WindowedPos.x, m_WindowedPos.y, m_WindowedSize.x, m_WindowedSize.y, 0);
    }
    m_Fullscreen = on;
}

bool Application::SaveScreenshot(const std::string& path) {
    ivec2 size = FramebufferSize();
    std::vector<uint8_t> pixels((size_t)size.x * size.y * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, size.x, size.y, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    stbi_flip_vertically_on_write(1);
    bool ok = stbi_write_png(path.c_str(), size.x, size.y, 4, pixels.data(), size.x * 4) != 0;
    if (ok) ND_INFO("Saved screenshot {}", path);
    else ND_ERROR("Could not write screenshot {}", path);
    return ok;
}

AppConfig ParseCommandLine(int argc, char** argv, AppConfig base, std::vector<std::string>* positional) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--screenshot") base.screenshotPath = next();
        else if (a == "--screenshot-frame") base.screenshotFrame = std::atoi(next().c_str());
        else if (a == "--frames") base.maxFrames = std::atoi(next().c_str());
        else if (a == "--width") base.width = std::atoi(next().c_str());
        else if (a == "--height") base.height = std::atoi(next().c_str());
        else if (a == "--fullscreen") base.fullscreen = true;
        else if (a == "--windowed") base.fullscreen = false;
        else if (a == "--no-vsync") base.vsync = false;
        else if (positional) positional->push_back(a);
    }
    if (!base.screenshotPath.empty() && base.screenshotFrame < 0) base.screenshotFrame = 60;
    return base;
}

} // namespace nd
