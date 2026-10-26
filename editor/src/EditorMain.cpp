// 9divine Editor entry point.
//   9divine-editor [projectDir]   (defaults to the bundled demo project)
#include "EditorApp.h"

using namespace nd;

int main(int argc, char** argv) {
    std::vector<std::string> positional;
    AppConfig base;
    base.title = "9divine Editor";
    base.width = 1600;
    base.height = 900;
    base.enableImGui = true;
    base.imguiDocking = true;
    AppConfig cfg = ParseCommandLine(argc, argv, base, nullptr);
    // Positional args = anything that isn't a flag or a flag's value
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--export" || a == "--screenshot" || a == "--screenshot-frame" || a == "--frames" || a == "--width" || a == "--height") { i++; continue; }
        if (a.rfind("--", 0) == 0) continue;
        positional.push_back(a);
    }
    cfg.imguiIniPath = (Platform::UserDataDir("Editor") / "layout.ini").string();

    fs::path project = positional.empty() ? fs::path(ND_SOURCE_DIR) / "projects" / "Demo" : fs::path(positional[0]);
    if (!fs::exists(project / "project.json")) {
        // Create an empty project on first use of a new folder
        fs::create_directories(project / "Scenes");
        json j = { { "name", project.filename().string() }, { "startScene", "" }, { "scenes", json::array() } };
        FileSystem::WriteText(project / "project.json", j.dump(2));
    }
    if (!Project::Load(project)) return 1;

    // Headless export: 9divine-editor [project] --export <outputDir>
    for (int i = 1; i + 1 < argc; i++)
        if (std::string(argv[i]) == "--export") {
            std::string log;
            bool ok = editor::EditorApp::BuildGame(argv[i + 1], log);
            std::printf("%s", log.c_str());
            return ok ? 0 : 1;
        }

    editor::EditorApp app(cfg, project);
    for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--play") app.playOnStart = true;
    app.Run();
    return 0;
}
