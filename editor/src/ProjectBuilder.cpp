// "Build Game": packages a standalone, shareable copy of the project.
//
//   <output>/
//     <ProjectName>(.exe)   the 9divine Player runtime, renamed
//     game.json             tells the player where the project lives
//     EngineAssets/         shaders + fonts
//     Game/                 the project (scenes, models, audio, input map...)
#include "EditorApp.h"

namespace nd::editor {

static std::string SafeName(std::string n) {
    for (auto& c : n) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
    return n.empty() ? "Game" : n;
}

bool EditorApp::BuildGame(const fs::path& out, std::string& log) {
    log.clear();
    auto say = [&](const std::string& s) { log += s + "\n"; ND_INFO("[Build] {}", s); };
    std::error_code ec;
#ifdef _WIN32
    const std::string exeExt = ".exe";
#else
    const std::string exeExt = "";
#endif
    fs::path player = Platform::ExecutableDir() / ("9divine-player" + exeExt);
    if (!fs::exists(player)) {
        say("ERROR: 9divine-player" + exeExt + " not found next to the editor. Build the NineDivinePlayer target first.");
        return false;
    }
    if (fs::exists(out / "Game", ec)) fs::remove_all(out / "Game", ec);
    fs::create_directories(out, ec);
    if (ec) { say("ERROR: cannot create " + out.string() + ": " + ec.message()); return false; }

    // 1. Runtime
    fs::path exe = out / (SafeName(Project::Info().name) + exeExt);
    fs::copy_file(player, exe, fs::copy_options::overwrite_existing, ec);
    if (ec) { say("ERROR copying player: " + ec.message()); return false; }
#ifndef _WIN32
    fs::permissions(exe, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#endif
    say("Runtime     -> " + exe.filename().string());

    // 2. Engine assets
    for (const char* sub : { "shaders", "fonts" }) {
        fs::path dst = out / "EngineAssets" / sub;
        fs::create_directories(dst, ec);
        fs::copy(Project::EngineAssetsDir() / sub, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) { say(std::string("ERROR copying engine ") + sub + ": " + ec.message()); return false; }
    }
    say("Engine data -> EngineAssets/");

    // 3. Project (everything except build outputs and editor files)
    size_t files = 0;
    fs::path root = Project::Root();
    fs::path outAbs = fs::absolute(out).lexically_normal();
    for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        fs::path p = it->path();
        fs::path rel = fs::relative(p, root, ec);
        std::string first = rel.begin() != rel.end() ? rel.begin()->string() : "";
        if (first == "Builds" || first == ".git" || (!first.empty() && first[0] == '.')) { if (it->is_directory()) it.disable_recursion_pending(); continue; }
        if (fs::absolute(p).lexically_normal() == outAbs) { it.disable_recursion_pending(); continue; }
        fs::path dst = out / "Game" / rel;
        if (it->is_directory()) { fs::create_directories(dst, ec); continue; }
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(p, dst, fs::copy_options::overwrite_existing, ec);
        if (!ec) files++;
    }
    say("Project     -> Game/ (" + std::to_string(files) + " files)");

    // 4. Launcher config
    json cfg = { { "project", "Game" } };
    FileSystem::WriteText(out / "game.json", cfg.dump(2));
    say("Done! Run " + exe.filename().string() + " (zip the folder to share it).");
    return true;
}

} // namespace nd::editor
