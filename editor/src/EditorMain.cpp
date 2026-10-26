// 9divine Editor entry point.
//
//   9divine-editor                                   open the Project Hub
//   9divine-editor <projectDir>                      open a project (an empty folder becomes a new project)
//   9divine-editor --new-project                     open the hub's New Project dialog
//   9divine-editor --create-project <dir> [--template empty|starter|sample] [--name "My Game"]
//   9divine-editor <projectDir> --export <outDir>    build a standalone game without opening a window
//   9divine-editor <projectDir> --play               open and enter play mode
#include "EditorApp.h"
#include "ProjectTemplates.h"

using namespace nd;

static std::string NameFromFolder(const fs::path& dir) {
    std::string n = fs::absolute(dir).lexically_normal().filename().string();
    for (auto& c : n) if (!(std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_')) c = '_';
    while (!n.empty() && n.back() == ' ') n.pop_back();
    while (!n.empty() && n.front() == ' ') n.erase(n.begin());
    return n.empty() ? "MyGame" : n;
}

int main(int argc, char** argv) {
    AppConfig base;
    base.title = "9divine Editor";
    base.width = 1600;
    base.height = 900;
    base.enableImGui = true;
    base.imguiDocking = true;
    AppConfig cfg = ParseCommandLine(argc, argv, base, nullptr);
    cfg.imguiIniPath = (Platform::UserDataDir("Editor") / "layout.ini").string();

    std::vector<std::string> positional;
    std::string exportDir, createDir, templateKey = "starter", projectName;
    bool play = false, newProject = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--export") exportDir = next();
        else if (a == "--create-project") createDir = next();
        else if (a == "--template") templateKey = next();
        else if (a == "--name") projectName = next();
        else if (a == "--play") play = true;
        else if (a == "--new-project") newProject = true;
        else if (a == "--screenshot" || a == "--screenshot-frame" || a == "--frames" || a == "--width" || a == "--height") next();
        else if (a.rfind("--", 0) == 0) continue;
        else positional.push_back(a);
    }

    fs::path project = positional.empty() ? fs::path() : fs::path(positional[0]);

    // --create-project: make it from a template, then open it
    if (!createDir.empty()) {
        templates::Template t;
        if (!templates::FromKey(templateKey, t)) {
            std::fprintf(stderr, "Unknown template '%s' (use empty, starter or sample)\n", templateKey.c_str());
            return 1;
        }
        std::string error;
        if (!templates::CreateProject(createDir, projectName.empty() ? NameFromFolder(createDir) : projectName, t, error)) {
            std::fprintf(stderr, "Could not create project: %s\n", error.c_str());
            return 1;
        }
        project = createDir;
    }

    // A folder without a project.json: turn it into a new (empty) project if it's empty/missing.
    if (!project.empty() && !fs::exists(project / "project.json")) {
        std::string error;
        if (!templates::CreateProject(project, NameFromFolder(project), templates::Template::Empty, error)) {
            std::fprintf(stderr, "'%s' is not a 9divine project: %s\n", project.string().c_str(), error.c_str());
            return 1;
        }
    }

    if (!exportDir.empty()) {
        if (project.empty()) project = fs::path(ND_SOURCE_DIR) / "projects" / "Demo";
        if (!Project::Load(project)) return 1;
        std::string log;
        bool ok = editor::EditorApp::BuildGame(exportDir, log);
        std::printf("%s", log.c_str());
        return ok ? 0 : 1;
    }

    editor::EditorApp app(cfg, project);
    app.playOnStart = play;
    app.newProjectOnStart = newProject;
    app.Run();
    return 0;
}
