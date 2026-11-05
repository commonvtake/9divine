#include "euphoria/assets/BlenderBridge.h"
#include "euphoria/core/FileSystem.h"
#include "euphoria/core/Json.h"
#include "euphoria/core/Log.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <thread>

namespace eu {

namespace {

// Runs inside Blender: open/import the source, then export everything as one .glb.
const char* kScript = R"PY(
import bpy, sys
argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = argv[0], argv[1]
ext = src.lower().rsplit(".", 1)[-1]
def need(op, what):
    if op is None:
        print("EUPHORIA_ERROR: this Blender can't import " + what)
        sys.exit(2)
    return op
if ext != "blend":
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ops = {
        "dae": (getattr(bpy.ops.wm, "collada_import", None), "Collada (.dae) - Blender 5 removed it; export the model as .fbx or .glb"),
        "3ds": (getattr(bpy.ops.import_scene, "max3ds", None), ".3ds (enable the 3DS add-on in Blender)"),
        "stl": (getattr(bpy.ops.wm, "stl_import", None) or getattr(bpy.ops.import_mesh, "stl", None), ".stl"),
        "ply": (getattr(bpy.ops.wm, "ply_import", None) or getattr(bpy.ops.import_mesh, "ply", None), ".ply"),
        "abc": (getattr(bpy.ops.wm, "alembic_import", None), "Alembic"),
    }
    if ext in ("usd", "usda", "usdc", "usdz"):
        ops[ext] = (getattr(bpy.ops.wm, "usd_import", None), "USD")
    op, what = ops.get(ext, (None, ext))
    need(op, what)(filepath=src)
props = bpy.ops.export_scene.gltf.get_rna_type().properties.keys()
args = {"filepath": dst, "export_format": "GLB"}
for key, value in (("export_apply", True), ("export_animations", True), ("export_yup", True),
                   ("export_animation_mode", "ACTIONS"), ("export_image_format", "AUTO"), ("export_skins", True)):
    if key in props:
        args[key] = value
bpy.ops.export_scene.gltf(**args)
print("EUPHORIA_OK")
)PY";

std::string Lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

fs::path ConfigFile() { return Platform::UserDataDir("Editor") / "blender.json"; }

struct Job { BlenderBridge::Status status = BlenderBridge::Status::Converting; std::string message; int64_t sourceStamp = 0; };
std::mutex s_Mutex;
std::map<std::string, Job> s_Jobs;
std::atomic<bool> s_Finished{ false };
std::atomic<int> s_Running{ 0 };
std::string s_Blender;
bool s_Searched = false;

std::string Quote(const std::string& s) { return "\"" + s + "\""; }

std::string Search() {
    if (const char* env = std::getenv("EUPHORIA_BLENDER"); env && *env && fs::exists(env)) return env;
    if (auto text = FileSystem::ReadText(ConfigFile())) {
        try {
            std::string p = json::parse(*text).value("blender", "");
            if (!p.empty() && fs::exists(p)) return p;
        } catch (...) {}
    }
    std::error_code ec;
    std::vector<fs::path> candidates;
#ifdef _WIN32
    for (const char* base : { "C:/Program Files/Blender Foundation", "C:/Program Files (x86)/Blender Foundation" }) {
        // Newest version first ("Blender 4.2" sorts below "Blender 4.10" as text, so compare numbers)
        std::vector<std::pair<std::vector<int>, fs::path>> found;
        for (auto& d : fs::directory_iterator(base, ec)) {
            fs::path exe = d.path() / "blender.exe";
            if (!fs::exists(exe, ec)) continue;
            std::vector<int> ver;
            int cur = -1;
            for (char c : d.path().filename().string()) {
                if (std::isdigit((unsigned char)c)) cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
                else if (cur >= 0) { ver.push_back(cur); cur = -1; }
            }
            if (cur >= 0) ver.push_back(cur);
            found.push_back({ ver, exe });
        }
        std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (auto& f : found) candidates.push_back(f.second);
    }
    candidates.push_back("C:/Program Files (x86)/Steam/steamapps/common/Blender/blender.exe");
    candidates.push_back("C:/Program Files/Steam/steamapps/common/Blender/blender.exe");
    if (const char* local = std::getenv("LOCALAPPDATA"))
        candidates.push_back(fs::path(local) / "Microsoft/WindowsApps/blender.exe");
#elif defined(__APPLE__)
    candidates.push_back("/Applications/Blender.app/Contents/MacOS/Blender");
#else
    candidates.push_back("/usr/bin/blender");
    candidates.push_back("/usr/local/bin/blender");
    candidates.push_back("/snap/bin/blender");
    candidates.push_back("/var/lib/flatpak/exports/bin/org.blender.Blender");
#endif
    // On PATH
    if (const char* pathEnv = std::getenv("PATH")) {
        std::string paths = pathEnv;
#ifdef _WIN32
        const char sep = ';';
        const char* exeName = "blender.exe";
#else
        const char sep = ':';
        const char* exeName = "blender";
#endif
        size_t start = 0;
        while (start <= paths.size()) {
            size_t end = paths.find(sep, start);
            std::string dir = paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!dir.empty()) candidates.push_back(fs::path(dir) / exeName);
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    for (auto& c : candidates)
        if (fs::is_regular_file(c, ec)) return c.string();
    return {};
}

// Runs Blender; returns true and fills `message` with its error on failure.
bool RunBlender(const fs::path& source, const fs::path& glb, std::string& message) {
    std::string blender = BlenderBridge::BlenderPath();
    if (blender.empty()) { message = "Blender wasn't found"; return false; }
    std::error_code ec;
    fs::create_directories(glb.parent_path(), ec);
    fs::path script = glb.parent_path() / "euphoria_export.py";
    if (!FileSystem::WriteText(script, kScript)) { message = "can't write " + script.string(); return false; }
    fs::path tmp = glb;
    tmp += ".tmp.glb";
    fs::path log = glb;
    log += ".log";
    fs::remove(tmp, ec);
    bool isBlend = Lower(source.extension().string()) == ".blend";
    std::string cmd = Quote(blender) + " --background --factory-startup " + (isBlend ? Quote(source.string()) + " " : std::string()) +
                      "--python " + Quote(script.string()) + " -- " + Quote(source.string()) + " " + Quote(tmp.string()) +
                      " > " + Quote(log.string()) + " 2>&1";
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";   // cmd.exe strips one pair of outer quotes
#endif
    int rc = std::system(cmd.c_str());
    std::string output = FileSystem::ReadText(log).value_or("");
    bool ok = rc == 0 && output.find("EUPHORIA_OK") != std::string::npos && fs::exists(tmp, ec);
    if (!ok) {
        size_t e = output.find("EUPHORIA_ERROR: ");
        if (e != std::string::npos) message = output.substr(e + 16, output.find('\n', e) - e - 16);
        else {
            // Last meaningful line of Blender's output
            std::string last;
            size_t pos = 0;
            while (pos < output.size()) {
                size_t nl = output.find('\n', pos);
                std::string line = output.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                if (line.find("Error") != std::string::npos || line.find("error") != std::string::npos) last = line;
                if (nl == std::string::npos) break;
                pos = nl + 1;
            }
            message = last.empty() ? "Blender exited with code " + std::to_string(rc) + " (see " + log.string() + ")" : last;
        }
        fs::remove(tmp, ec);
        return false;
    }
    fs::rename(tmp, glb, ec);
    if (ec) { fs::copy_file(tmp, glb, fs::copy_options::overwrite_existing, ec); fs::remove(tmp, ec); }
    fs::remove(log, ec);
    return true;
}

} // namespace

bool BlenderBridge::Handles(const std::string& path) {
    std::string e = Lower(fs::path(path).extension().string());
    return e == ".blend" || e == ".dae" || e == ".3ds" || e == ".stl" || e == ".ply" || e == ".abc" || e == ".usd" || e == ".usda" ||
           e == ".usdc" || e == ".usdz";
}

std::string BlenderBridge::BlenderPath() {
    std::lock_guard lock(s_Mutex);
    if (!s_Searched) {
        s_Blender = Search();
        s_Searched = true;
        if (!s_Blender.empty()) EU_INFO("Blender found: {}", s_Blender);
    }
    return s_Blender;
}

void BlenderBridge::SetBlenderPath(const std::string& path) {
    {
        std::lock_guard lock(s_Mutex);
        s_Blender = path;
        s_Searched = true;
        for (auto it = s_Jobs.begin(); it != s_Jobs.end();)   // retry what failed for lack of Blender
            it = it->second.status == Status::Converting ? std::next(it) : s_Jobs.erase(it);
    }
    std::error_code ec;
    fs::create_directories(ConfigFile().parent_path(), ec);
    FileSystem::WriteText(ConfigFile(), json{ { "blender", path } }.dump(2));
}

fs::path BlenderBridge::CachePathFor(const fs::path& source) {
    fs::path src = source.lexically_normal();
    if (Project::IsLoaded()) {
        std::string rel = Project::MakeRelative(src);
        if (!rel.empty() && fs::path(rel).is_relative() && rel.rfind("..", 0) != 0)
            return Project::Root() / ".euphoria" / "converted" / (rel + ".glb");
    }
    return src.parent_path() / ".euphoria_converted" / (src.filename().string() + ".glb");
}

BlenderBridge::Status BlenderBridge::Request(const fs::path& source, fs::path& glb, std::string* message) {
    glb = CachePathFor(source);
    std::error_code ec;
    int64_t srcStamp = FileSystem::LastWriteTime(source);
    bool cached = fs::exists(glb, ec);
    bool fresh = cached && FileSystem::LastWriteTime(glb) >= srcStamp;
    std::string key = source.lexically_normal().string();
    {
        std::lock_guard lock(s_Mutex);
        auto it = s_Jobs.find(key);
        if (it != s_Jobs.end() && it->second.sourceStamp == srcStamp) {
            if (message) *message = it->second.message;
            if (it->second.status == Status::Converting) return Status::Converting;
            if (it->second.status != Status::Ready) return cached ? Status::Ready : it->second.status;   // stale but usable
            if (cached) return Status::Ready;
        }
    }
    if (fresh) return Status::Ready;
    if (BlenderPath().empty()) {
        if (cached) return Status::Ready;   // an exported game: use what was converted when it was built
        if (message) *message = "Install Blender (or set its path) to open " + source.filename().string();
        return Status::NoBlender;
    }
    {
        std::lock_guard lock(s_Mutex);
        s_Jobs[key] = { Status::Converting, "Converting with Blender...", srcStamp };
    }
    s_Running++;
    EU_INFO("Converting {} with Blender...", source.filename().string());
    std::thread([source, glb, key, srcStamp] {
        std::string msg;
        bool ok = RunBlender(source, glb, msg);
        {
            std::lock_guard lock(s_Mutex);
            s_Jobs[key] = { ok ? Status::Ready : Status::Failed, ok ? "" : msg, srcStamp };
        }
        if (ok) EU_INFO("Converted {}", source.filename().string());
        else EU_ERROR("Blender couldn't convert {}: {}", source.filename().string(), msg);
        s_Running--;
        s_Finished = true;
    }).detach();
    if (message) *message = "Converting with Blender...";
    return Status::Converting;
}

BlenderBridge::Status BlenderBridge::ConvertNow(const fs::path& source, fs::path& glb, std::string* message) {
    glb = CachePathFor(source);
    std::error_code ec;
    if (fs::exists(glb, ec) && FileSystem::LastWriteTime(glb) >= FileSystem::LastWriteTime(source)) return Status::Ready;
    if (BlenderPath().empty()) {
        if (message) *message = "Blender wasn't found (set EUPHORIA_BLENDER to blender's path)";
        return fs::exists(glb, ec) ? Status::Ready : Status::NoBlender;
    }
    std::string msg;
    bool ok = RunBlender(source, glb, msg);
    if (message) *message = msg;
    return ok ? Status::Ready : Status::Failed;
}

bool BlenderBridge::PollFinished() { return s_Finished.exchange(false); }
bool BlenderBridge::Busy() { return s_Running > 0; }

} // namespace eu
