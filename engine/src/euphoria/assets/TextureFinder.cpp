#include "euphoria/assets/TextureFinder.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace eu::TextureFinder {

namespace {

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// The last path component, accepting both separators (Windows paths inside files read on Linux).
std::string BaseName(const std::string& p) {
    size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

std::string StemOf(const std::string& name) {
    size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

const char* kImageExts[] = { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".psd", ".gif" };

std::mutex s_Mutex;
// folder -> (lower-case file name -> actual path)
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> s_Listings;
// scan root -> (lower-case file name -> first match below it)
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> s_Trees;

const std::unordered_map<std::string, std::string>& Listing(const fs::path& dir) {
    std::string key = dir.lexically_normal().string();
    auto it = s_Listings.find(key);
    if (it != s_Listings.end()) return it->second;
    auto& files = s_Listings[key];
    std::error_code ec;
    if (fs::is_directory(dir, ec))
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec)) files.emplace(Lower(e.path().filename().string()), e.path().string());
    return files;
}

const std::unordered_map<std::string, std::string>& Tree(const fs::path& root) {
    std::string key = root.lexically_normal().string();
    auto it = s_Trees.find(key);
    if (it != s_Trees.end()) return it->second;
    auto& files = s_Trees[key];
    std::error_code ec;
    int visited = 0;
    auto opts = fs::directory_options::skip_permission_denied;
    for (auto i = fs::recursive_directory_iterator(root, opts, ec); i != fs::recursive_directory_iterator() && !ec; i.increment(ec)) {
        if (++visited > 20000) break;                        // don't crawl a whole drive
        if (i.depth() > 5) { i.disable_recursion_pending(); continue; }
        if (i->is_regular_file(ec) && IsLoadableImage(i->path().string()))
            files.emplace(Lower(i->path().filename().string()), i->path().string());
    }
    return files;
}

// Names to look for: the name as written plus the usual exporter mangling fixed.
std::vector<std::string> Candidates(const std::string& reference) {
    std::string name = BaseName(reference);
    std::vector<std::string> out;
    auto add = [&](const std::string& n) { if (!n.empty() && std::find(out.begin(), out.end(), Lower(n)) == out.end()) out.push_back(Lower(n)); };
    add(name);
    std::string stem = StemOf(name);
    // "T_Hair_Normal_png.png" / "T_Hair_Normal.png.png" -> "T_Hair_Normal"
    for (const char* junk : { "_png", "_jpg", "_jpeg", "_tga", ".png", ".jpg", ".jpeg", ".tga" }) {
        std::string j = junk;
        if (stem.size() > j.size() && Lower(stem.substr(stem.size() - j.size())) == j) { stem = stem.substr(0, stem.size() - j.size()); break; }
    }
    for (const char* ext : kImageExts) add(stem + ext);       // .tif / .dds sources -> a .png beside them
    return out;
}

std::string LookIn(const fs::path& dir, const std::vector<std::string>& names) {
    const auto& files = Listing(dir);
    for (auto& n : names) {
        auto it = files.find(n);
        if (it != files.end() && IsLoadableImage(it->second)) return it->second;
    }
    return {};
}

} // namespace

bool IsLoadableImage(const std::string& path) {
    std::string ext = Lower(fs::path(path).extension().string());
    for (const char* e : kImageExts) if (ext == e) return true;
    return false;
}

void ClearCache() {
    std::lock_guard lock(s_Mutex);
    s_Listings.clear();
    s_Trees.clear();
}

std::string Find(const std::string& reference, const fs::path& modelDir) {
    if (reference.empty()) return {};
    std::lock_guard lock(s_Mutex);
    std::error_code ec;
    // 1. As written
    fs::path asWritten(reference);
    if (asWritten.is_absolute() && fs::is_regular_file(asWritten, ec) && IsLoadableImage(asWritten.string())) return asWritten.string();
    fs::path rel = modelDir / reference;
    if (fs::is_regular_file(rel, ec) && IsLoadableImage(rel.string())) return rel.lexically_normal().string();

    // 2. By name in the usual places
    std::vector<std::string> names = Candidates(reference);
    std::vector<fs::path> dirs;
    fs::path parent = modelDir.parent_path(), grand = parent.parent_path();
    for (const fs::path& base : { modelDir, parent, grand }) {
        if (base.empty()) continue;
        dirs.push_back(base);
        for (const char* sub : { "Textures", "textures", "Texture", "texture", "Materials", "materials", "Images", "images" }) dirs.push_back(base / sub);
    }
    for (auto& d : dirs) {
        std::string hit = LookIn(d, names);
        if (!hit.empty()) return hit;
    }

    // 3. Anywhere below the grandparent (bounded)
    fs::path root = grand.empty() ? (parent.empty() ? modelDir : parent) : grand;
    if (std::distance(root.begin(), root.end()) >= 3) {      // never scan a drive root or home folder
        const auto& tree = Tree(root);
        for (auto& n : names) {
            auto it = tree.find(n);
            if (it != tree.end()) return it->second;
        }
    }
    return {};
}

Companions FindCompanions(const std::string& colorPath) {
    Companions c;
    if (colorPath.empty()) return c;
    std::lock_guard lock(s_Mutex);
    fs::path p(colorPath);
    const auto& files = Listing(p.parent_path());
    std::string stem = StemOf(p.filename().string());
    std::string lowStem = Lower(stem);

    // Prefixes the map names are built from: the full name, the name without its color suffix
    // (T_Brick_BaseColor -> T_Brick), and without its last _part (T_Hero_Dark -> T_Hero).
    std::vector<std::string> prefixes = { stem };
    for (const char* suffix : { "_basecolor", "_base_color", "_albedo", "_diffuse", "_diff", "_color", "_col", "_d", "_c" }) {
        std::string s = suffix;
        if (lowStem.size() > s.size() && lowStem.substr(lowStem.size() - s.size()) == s) { prefixes.push_back(stem.substr(0, stem.size() - s.size())); break; }
    }
    size_t us = stem.find_last_of('_');
    if (us != std::string::npos && us > 0) prefixes.push_back(stem.substr(0, us));

    auto find = [&](std::initializer_list<const char*> suffixes) -> std::string {
        for (auto& pre : prefixes)
            for (const char* suf : suffixes)
                for (const char* ext : kImageExts) {
                    auto it = files.find(Lower(pre + suf + ext));
                    if (it != files.end() && Lower(it->second) != Lower(colorPath)) return it->second;
                }
        return {};
    };
    c.normal = find({ "_n", "_normal", "_normalgl", "_nrm", "_nor", "_norm", "_normalmap", "_bump" });
    c.emissive = find({ "_emissor", "_emissive", "_emission", "_emit", "_glow", "_e" });
    c.orm = find({ "_orm", "_occlusionroughnessmetallic", "_arm" });
    c.roughness = find({ "_roughness", "_rough", "_rgh" });
    c.metallic = find({ "_metallic", "_metalness", "_metal" });
    c.occlusion = find({ "_ao", "_occlusion", "_ambientocclusion" });
    return c;
}

} // namespace eu::TextureFinder
