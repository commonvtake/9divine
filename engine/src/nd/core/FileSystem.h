#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace nd {

namespace fs = std::filesystem;

class FileSystem {
public:
    static std::optional<std::string> ReadText(const fs::path& path);
    static bool WriteText(const fs::path& path, const std::string& text);
    static std::optional<std::vector<uint8_t>> ReadBinary(const fs::path& path);
    static bool WriteBinary(const fs::path& path, const void* data, size_t size);
    static int64_t LastWriteTime(const fs::path& path);
};

// Platform queries.
class Platform {
public:
    static fs::path ExecutableDir();
    static fs::path ExecutablePath();
    static fs::path UserDataDir(const std::string& appName); // e.g. %APPDATA%/9divine/<app>
    static size_t ProcessMemoryBytes();
    static bool OpenInFileBrowser(const fs::path& path);
};

// A game project: a folder containing project.json, scenes and assets.
struct ProjectInfo {
    std::string name = "Untitled";
    std::string startScene = "Scenes/Main.scene";
    std::vector<std::string> scenes;  // scenes included in builds (relative paths)
    std::string inputMap = "input.json";
    int windowWidth = 1280, windowHeight = 720;
    bool fullscreen = false;
    bool vsync = true;
};

class Project {
public:
    static bool Load(const fs::path& projectDir);
    static bool Save();
    static bool IsLoaded() { return s_Loaded; }
    static const fs::path& Root() { return s_Root; }
    static ProjectInfo& Info() { return s_Info; }

    // "Scenes/Main.scene" -> <root>/Scenes/Main.scene
    // "engine:shaders/lit.glsl" -> <engine assets>/shaders/lit.glsl
    static fs::path Resolve(const std::string& path);
    // Absolute path -> project-relative (generic separators) when possible.
    static std::string MakeRelative(const fs::path& absolute);

    static fs::path EngineAssetsDir();

private:
    static bool s_Loaded;
    static fs::path s_Root;
    static ProjectInfo s_Info;
};

} // namespace nd
