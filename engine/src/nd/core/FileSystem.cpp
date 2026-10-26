#include "nd/core/FileSystem.h"
#include "nd/core/Log.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#else
#include <unistd.h>
#include <limits.h>
#include <cstdio>
#endif

namespace nd {

// ---------------------------------------------------------------- FileSystem
std::optional<std::string> FileSystem::ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool FileSystem::WriteText(const fs::path& path, const std::string& text) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << text;
    return true;
}

std::optional<std::vector<uint8_t>> FileSystem::ReadBinary(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return std::nullopt;
    auto size = in.tellg();
    std::vector<uint8_t> data((size_t)size);
    in.seekg(0);
    in.read((char*)data.data(), size);
    return data;
}

bool FileSystem::WriteBinary(const fs::path& path, const void* data, size_t size) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write((const char*)data, (std::streamsize)size);
    return true;
}

int64_t FileSystem::LastWriteTime(const fs::path& path) {
    std::error_code ec;
    auto t = fs::last_write_time(path, ec);
    if (ec) return 0;
    return (int64_t)t.time_since_epoch().count();
}

// ------------------------------------------------------------------ Platform
fs::path Platform::ExecutablePath() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf);
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return fs::current_path();
    buf[n] = 0;
    return fs::path(buf);
#endif
}

fs::path Platform::ExecutableDir() { return ExecutablePath().parent_path(); }

fs::path Platform::UserDataDir(const std::string& appName) {
    fs::path base;
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) base = appdata;
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) base = xdg;
    else if (const char* home = std::getenv("HOME")) base = fs::path(home) / ".local" / "share";
#endif
    if (base.empty()) base = ExecutableDir();
    fs::path dir = base / "9divine" / appName;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

size_t Platform::ProcessMemoryBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return pmc.WorkingSetSize;
    return 0;
#else
    long pages = 0, resident = 0;
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    if (std::fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
    std::fclose(f);
    return (size_t)resident * (size_t)sysconf(_SC_PAGESIZE);
#endif
}

bool Platform::OpenInFileBrowser(const fs::path& path) {
#ifdef _WIN32
    return (INT_PTR)ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
#elif defined(__APPLE__)
    return std::system(("open \"" + path.string() + "\"").c_str()) == 0;
#else
    return std::system(("xdg-open \"" + path.string() + "\" &").c_str()) == 0;
#endif
}

// ------------------------------------------------------------------- Project
bool Project::s_Loaded = false;
fs::path Project::s_Root;
ProjectInfo Project::s_Info;

fs::path Project::EngineAssetsDir() {
    static fs::path cached;
    if (!cached.empty()) return cached;
    fs::path local = Platform::ExecutableDir() / "EngineAssets";
    if (fs::exists(local / "shaders")) cached = local;
    else cached = fs::path(ND_ENGINE_ASSETS_DIR);
    return cached;
}

bool Project::Load(const fs::path& projectDir) {
    fs::path file = projectDir / "project.json";
    auto text = FileSystem::ReadText(file);
    if (!text) {
        ND_ERROR("No project.json found in {}", projectDir.string());
        return false;
    }
    try {
        auto j = nlohmann::json::parse(*text);
        ProjectInfo info;
        info.name = j.value("name", info.name);
        info.startScene = j.value("startScene", info.startScene);
        info.scenes = j.value("scenes", std::vector<std::string>{});
        info.inputMap = j.value("inputMap", info.inputMap);
        info.windowWidth = j.value("windowWidth", info.windowWidth);
        info.windowHeight = j.value("windowHeight", info.windowHeight);
        info.fullscreen = j.value("fullscreen", info.fullscreen);
        info.vsync = j.value("vsync", info.vsync);
        s_Info = info;
    } catch (const std::exception& e) {
        ND_ERROR("Failed to parse project.json: {}", e.what());
        return false;
    }
    s_Root = fs::absolute(projectDir).lexically_normal();
    s_Loaded = true;
    ND_INFO("Loaded project '{}' from {}", s_Info.name, s_Root.string());
    return true;
}

bool Project::Save() {
    if (!s_Loaded) return false;
    nlohmann::json j;
    j["name"] = s_Info.name;
    j["startScene"] = s_Info.startScene;
    j["scenes"] = s_Info.scenes;
    j["inputMap"] = s_Info.inputMap;
    j["windowWidth"] = s_Info.windowWidth;
    j["windowHeight"] = s_Info.windowHeight;
    j["fullscreen"] = s_Info.fullscreen;
    j["vsync"] = s_Info.vsync;
    return FileSystem::WriteText(s_Root / "project.json", j.dump(2));
}

fs::path Project::Resolve(const std::string& path) {
    if (path.rfind("engine:", 0) == 0) return EngineAssetsDir() / path.substr(7);
    fs::path p(path);
    if (p.is_absolute()) return p;
    return s_Root / p;
}

std::string Project::MakeRelative(const fs::path& absolute) {
    std::error_code ec;
    auto rel = fs::relative(absolute, s_Root, ec);
    if (ec || rel.empty() || rel.string().rfind("..", 0) == 0) return absolute.generic_string();
    return rel.generic_string();
}

} // namespace nd
