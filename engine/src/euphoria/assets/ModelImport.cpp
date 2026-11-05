#include "euphoria/assets/ModelImport.h"
#include "euphoria/assets/AssetManager.h"
#include "euphoria/assets/TextureFinder.h"
#include "euphoria/animation/Retarget.h"
#include "euphoria/core/FileSystem.h"
#include "euphoria/core/Json.h"
#include "euphoria/core/Log.h"
#include <stb_image.h>
#include <mutex>
#include <unordered_map>
#include <cstring>

namespace eu::ModelImport {

namespace {
std::mutex s_TexMutex;
std::unordered_map<std::string, std::weak_ptr<Texture2D>> s_Textures;

std::string Pretty(const std::string& absolute) {
    std::string rel = Project::IsLoaded() ? Project::MakeRelative(absolute) : absolute;
    return rel.empty() ? absolute : rel;
}
} // namespace

Ref<Texture2D> TextureFromFile(const std::string& path) {
    std::string key = fs::path(path).lexically_normal().string();
    {
        std::lock_guard lock(s_TexMutex);
        auto it = s_Textures.find(key);
        if (it != s_Textures.end())
            if (auto tex = it->second.lock()) return tex;
    }
    auto bytes = FileSystem::ReadBinary(path);
    if (!bytes) return nullptr;
    auto tex = Texture2D::FromMemory(bytes->data(), bytes->size());
    if (tex) {
        std::lock_guard lock(s_TexMutex);
        s_Textures[key] = tex;
    }
    return tex;
}

void ClearTextureCache() {
    std::lock_guard lock(s_TexMutex);
    s_Textures.clear();
    TextureFinder::ClearCache();
}

Ref<Texture2D> Resolve(const std::string& reference, const fs::path& modelDir, MaterialData& md, const char* slot, std::string* usedPath,
                       bool noteMissing) {
    if (reference.empty()) return nullptr;
    std::string found = TextureFinder::Find(reference, modelDir);
    if (found.empty()) {
        if (!noteMissing) return nullptr;
        std::string name = reference.substr(reference.find_last_of("/\\") == std::string::npos ? 0 : reference.find_last_of("/\\") + 1);
        md.textureNotes.push_back(std::string(slot) + ": missing " + name);
        return nullptr;
    }
    auto tex = TextureFromFile(found);
    if (tex) {
        md.sourceFiles.push_back(found);
        md.textureNotes.push_back(std::string(slot) + ": " + Pretty(found));
        if (usedPath) *usedPath = found;
    }
    return tex;
}

void AddCompanionMaps(MaterialData& md, const std::string& colorPath) {
    if (colorPath.empty()) return;
    auto c = TextureFinder::FindCompanions(colorPath);
    auto note = [&](const char* slot, const std::string& p) {
        md.sourceFiles.push_back(p);
        md.textureNotes.push_back(std::string(slot) + ": " + Pretty(p) + " (by name)");
    };
    if (!md.normalMap && !c.normal.empty())
        if ((md.normalMap = TextureFromFile(c.normal))) note("normal", c.normal);
    if (!md.emissiveMap && !c.emissive.empty())
        if ((md.emissiveMap = TextureFromFile(c.emissive))) {
            if (md.emissive == vec3(0.0f)) md.emissive = vec3(1.0f);   // the map alone says where it glows
            note("emissive", c.emissive);
        }
    if (!md.metallicRoughnessMap) {
        if (!c.orm.empty()) {
            // glTF packing: R = occlusion, G = roughness, B = metallic
            if ((md.metallicRoughnessMap = TextureFromFile(c.orm))) {
                if (!md.occlusionMap) md.occlusionMap = md.metallicRoughnessMap;
                md.roughness = 1.0f;
                md.metallic = 1.0f;
                note("ORM", c.orm);
            }
        } else if (!c.roughness.empty() || !c.metallic.empty()) {
            // Separate grayscale maps: pack them into one texture (G = roughness, B = metallic)
            int w = 0, h = 0, n = 0, w2 = 0, h2 = 0, n2 = 0;
            stbi_set_flip_vertically_on_load(0);
            unsigned char* r = c.roughness.empty() ? nullptr : stbi_load(c.roughness.c_str(), &w, &h, &n, 1);
            unsigned char* m = c.metallic.empty() ? nullptr : stbi_load(c.metallic.c_str(), &w2, &h2, &n2, 1);
            if (r && m && (w != w2 || h != h2)) { stbi_image_free(m); m = nullptr; }   // different sizes: roughness wins
            int W = r ? w : w2, H = r ? h : h2;
            if (W > 0 && H > 0) {
                std::vector<uint8_t> px((size_t)W * H * 4);
                for (size_t i = 0; i < (size_t)W * H; i++) {
                    px[i * 4 + 0] = 255;
                    px[i * 4 + 1] = r ? r[i] : 255;
                    px[i * 4 + 2] = m ? m[i] : 0;
                    px[i * 4 + 3] = 255;
                }
                md.metallicRoughnessMap = Texture2D::FromPixels(W, H, 4, px.data());
                if (r) { md.roughness = 1.0f; note("roughness", c.roughness); }
                if (m) { md.metallic = 1.0f; note("metallic", c.metallic); }
            }
            if (r) stbi_image_free(r);
            if (m) stbi_image_free(m);
        }
    }
    if (!md.occlusionMap && !c.occlusion.empty())
        if ((md.occlusionMap = TextureFromFile(c.occlusion))) note("occlusion", c.occlusion);
}

void Weld(std::vector<Vertex>& verts, std::vector<uint32_t>& indices) {
    struct Key {
        const Vertex* v;
        bool operator==(const Key& o) const { return std::memcmp(v, o.v, sizeof(Vertex)) == 0; }
    };
    struct Hash {
        size_t operator()(const Key& k) const {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(k.v);
            size_t h = 1469598103934665603ull;
            for (size_t i = 0; i < sizeof(Vertex); i++) { h ^= p[i]; h *= 1099511628211ull; }
            return h;
        }
    };
    std::vector<Vertex> out;
    out.reserve(verts.size());
    std::vector<uint32_t> remap(verts.size());
    std::unordered_map<Key, uint32_t, Hash> seen;
    seen.reserve(verts.size());
    for (size_t i = 0; i < verts.size(); i++) {
        auto it = seen.find({ &verts[i] });
        if (it != seen.end()) { remap[i] = it->second; continue; }
        remap[i] = (uint32_t)out.size();
        seen.emplace(Key{ &verts[i] }, remap[i]);
        out.push_back(verts[i]);
    }
    for (auto& idx : indices) idx = remap[idx];
    verts.swap(out);
}

std::string CleanClipName(const std::string& name, const std::string& modelPath, size_t clipCount) {
    std::string n = name;
    size_t bar = n.find_last_of('|');
    if (bar != std::string::npos && bar + 1 < n.size()) n = n.substr(bar + 1);   // Blender: "Armature|Run"
    std::string low = n;
    for (auto& c : low) c = (char)std::tolower((unsigned char)c);
    bool generic = low.empty() || low == "mixamo.com" || low.rfind("take 0", 0) == 0 || low == "default take" || low == "unreal take" ||
                   low == "animation" || low == "scene";
    if (generic && clipCount <= 1) n = fs::path(modelPath).stem().string();
    return n;
}

// A humanoid facing +Z has its left limbs on +X. FBX exports in particular come either way
// round; turning the ones that face -Z makes every character (and every animation retargeted
// between them) agree.
static bool FacesBackwards(const Model& model) {
    if (!model.skeleton) return false;
    const Skeleton& s = *model.skeleton;
    auto human = ClassifyHumanoid(s);
    // The scene pose: the way the character stands when you place it
    Pose pose;
    BindPose(s, pose);
    std::vector<mat4> g(s.joints.size());
    for (size_t i = 0; i < s.joints.size(); i++) {
        mat4 local = ComposeTRS(pose[i].t, pose[i].r, pose[i].s);
        g[i] = s.joints[i].parent >= 0 ? g[s.joints[i].parent] * local : local;
    }
    float left = 0.0f, right = 0.0f;
    int found = 0;
    for (size_t i = 0; i < human.size(); i++) {
        HumanBone b = human[i];
        if (b == HumanBone::LeftUpperArm || b == HumanBone::LeftUpperLeg) { left += g[i][3].x; found++; }
        if (b == HumanBone::RightUpperArm || b == HumanBone::RightUpperLeg) { right += g[i][3].x; found++; }
    }
    return found >= 4 && left < right - 1e-3f;
}

void ApplyImportSettings(Model& model, const std::string& modelPath) {
    auto settings = AssetManager::LoadImportSettings(modelPath);
    if (settings.faceForward && FacesBackwards(model)) settings.rotation.y += 180.0f;
    if (settings.scale == 1.0f && settings.rotation == vec3(0.0f)) return;
    quat R = quat(glm::radians(settings.rotation));
    float s = settings.scale;
    mat4 M = glm::mat4_cast(R) * glm::scale(mat4(1.0f), vec3(s));
    mat3 rot = glm::mat3_cast(R);

    model.bounds = AABB();
    for (auto& sm : model.meshes) {
        auto verts = sm.mesh->Vertices();
        for (auto& v : verts) {
            v.position = vec3(M * vec4(v.position, 1.0f));
            v.normal = rot * v.normal;
            v.tangent = vec4(rot * vec3(v.tangent), v.tangent.w);
        }
        sm.mesh = MakeRef<Mesh>(verts, sm.mesh->Indices());
        sm.nodeWorld = M * sm.nodeWorld;
        model.bounds.Expand(sm.mesh->Bounds());
    }
    if (model.skeleton) {
        mat4 inv = glm::inverse(M);
        for (auto& j : model.skeleton->joints) {
            j.inverseBind = j.inverseBind * inv;
            if (j.parent < 0) {
                j.bindT = vec3(M * vec4(j.bindT, 1.0f));
                j.bindR = R * j.bindR;
                j.bindS *= s;
            }
        }
        for (auto& clip : model.clips)
            for (auto& ch : clip->channels) {
                if (ch.joint < 0 || model.skeleton->joints[ch.joint].parent >= 0) continue;
                for (auto& t : ch.translation.values) t = vec3(M * vec4(t, 1.0f));
                for (auto& r : ch.rotation.values) r = R * r;
                for (auto& sc : ch.scale.values) sc *= s;
            }
    }
}

} // namespace eu::ModelImport
