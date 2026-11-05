#include "euphoria/assets/AssetManager.h"
#include "euphoria/core/FileSystem.h"
#include "euphoria/core/Json.h"
#include "euphoria/core/Log.h"
#include "euphoria/core/Profiler.h"
#include <cgltf.h>
#include <tiny_obj_loader.h>
#include "euphoria/assets/ModelImport.h"
#include "euphoria/assets/TextureFinder.h"
#include "euphoria/assets/BlenderBridge.h"
#include "euphoria/animation/Retarget.h"
#include <chrono>
#include <set>
#include <cstring>

namespace eu {

std::map<std::string, AssetManager::ModelEntry> AssetManager::s_Models;
std::map<std::string, AssetManager::TextureEntry> AssetManager::s_Textures;
std::map<std::string, std::vector<Ref<AnimationClip>>> AssetManager::s_Retargeted;
uint32_t AssetManager::s_Version = 1;

static std::string Lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static std::string Ext(const std::string& p) { return Lower(fs::path(p).extension().string()); }

bool AssetManager::IsModelFile(const std::string& p) {
    std::string base = p.substr(0, p.find('#'));
    auto e = Ext(base);
    return e == ".gltf" || e == ".glb" || e == ".obj" || e == ".fbx" || BlenderBridge::Handles(base);
}
bool AssetManager::IsTextureFile(const std::string& p) { auto e = Ext(p); return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp" || e == ".psd" || e == ".gif"; }
bool AssetManager::IsAudioFile(const std::string& p) { auto e = Ext(p); return e == ".wav" || e == ".mp3" || e == ".flac"; }
bool AssetManager::IsSceneFile(const std::string& p) { return Ext(p) == ".scene"; }

Ref<Model> AssetManager::GetModel(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = s_Models.find(path);
    if (it != s_Models.end()) return it->second.model;

    Ref<Model> model;
    int64_t stamp = 0;
    if (path.rfind("builtin:", 0) == 0) {
        auto mesh = Primitives::ByName(path.substr(8));
        if (mesh) {
            model = MakeRef<Model>();
            model->path = path;
            model->meshes.push_back({ mesh, -1 });
            model->bounds = mesh->Bounds();
        }
    } else if (size_t hash = path.find('#'); hash != std::string::npos) {
        // One object out of a multi-object file
        std::string base = path.substr(0, hash);
        Ref<Model> whole = GetModel(base);
        if (!whole && BlenderBridge::Handles(base)) return nullptr;   // still converting: ask again later
        stamp = s_Models.count(base) ? s_Models[base].stamp : 0;
        if (whole) model = ModelLoader::ExtractObject(*whole, path.substr(hash + 1));
        if (model) model->path = path;
    } else {
        fs::path full = Project::Resolve(path);
        stamp = FileSystem::LastWriteTime(full);
        if (BlenderBridge::Handles(path)) {
            fs::path glb;
            std::string message;
            auto status = BlenderBridge::Request(full, glb, &message);
            if (status == BlenderBridge::Status::Converting) return nullptr;   // not cached: retried until it's ready
            if (status == BlenderBridge::Status::Ready) {
                model = ModelLoader::LoadGLTF(glb.string());
                if (model) {
                    model->sourceFormat = "Blender (" + Ext(path).substr(1) + ", converted to glTF)";
                    ModelImport::ApplyImportSettings(*model, full.string());
                }
            } else {
                EU_WARN("Could not load model '{}': {}", path, message);
            }
        } else {
            model = ModelLoader::Load(full.string());
        }
        if (model) model->path = path;
    }
    if (!model && !BlenderBridge::Handles(path.substr(0, path.find('#')))) EU_WARN("Could not load model '{}'", path);
    s_Models[path] = { model, stamp };   // cache failures too (avoids retrying every frame)
    return model;
}

Ref<Texture2D> AssetManager::GetTexture(const std::string& path, bool pixelArt) {
    if (path.empty()) return nullptr;
    auto it = s_Textures.find(path);
    if (it != s_Textures.end()) return it->second.texture;
    fs::path full = Project::Resolve(path);
    TextureSpec spec;
    if (pixelArt) { spec.linear = false; spec.mipmaps = false; }
    auto tex = Texture2D::FromFile(full.string(), spec);
    s_Textures[path] = { tex, FileSystem::LastWriteTime(full), pixelArt };
    return tex;
}

void AssetManager::PollChanges() {
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<float>(now - last).count() < 1.0f) return;
    last = now;
    bool changed = BlenderBridge::PollFinished();   // a background Blender conversion finished
    for (auto it = s_Models.begin(); it != s_Models.end();) {
        std::string file = it->first.substr(0, it->first.find('#'));
        if (it->first.rfind("builtin:", 0) != 0 && FileSystem::LastWriteTime(Project::Resolve(file)) != it->second.stamp) {
            EU_INFO("Reloading model {}", it->first);
            it = s_Models.erase(it);
            changed = true;
        } else ++it;
    }
    for (auto it = s_Textures.begin(); it != s_Textures.end();) {
        if (FileSystem::LastWriteTime(Project::Resolve(it->first)) != it->second.stamp) {
            EU_INFO("Reloading texture {}", it->first);
            it = s_Textures.erase(it);
            changed = true;
        } else ++it;
    }
    if (changed) { s_Version++; s_Retargeted.clear(); }
}

const std::vector<Ref<AnimationClip>>& AssetManager::RetargetedClips(const Model& target, const std::string& source) {
    static const std::vector<Ref<AnimationClip>> none;
    if (!target.skeleton || source.empty() || source == target.path) return none;
    std::string key = target.path + "|" + source + "|" + std::to_string(s_Version);
    auto it = s_Retargeted.find(key);
    if (it != s_Retargeted.end()) return it->second;
    Ref<Model> src = GetModel(source);
    if (!src || !src->skeleton) {
        if (!src && BlenderBridge::Handles(source)) return none;   // still converting: try again later
        return s_Retargeted[key];
    }
    auto& out = s_Retargeted[key];
    RetargetMap map = MapSkeletons(*src->skeleton, *target.skeleton);
    if (map.mapped == 0) {
        EU_WARN("{}: no joints in common with {} - can't use its animations", target.path, source);
        return out;
    }
    for (auto& clip : src->clips)
        if (auto c = RetargetClip(*clip, *src->skeleton, *target.skeleton, map)) out.push_back(c);
    EU_INFO("{} animations from {} on {} ({}, {} joints)", out.size(), source, target.path, map.sameRig ? "same rig" : "retargeted",
            map.mapped);
    return out;
}

void AssetManager::ClearCache() {
    s_Retargeted.clear();
    s_Models.clear();
    s_Textures.clear();
    ModelImport::ClearTextureCache();
    s_Version++;
}

// ------------------------------------------------------------ import settings (<model>.meta)
static fs::path MetaPath(const std::string& modelPath) {
    fs::path p = fs::path(modelPath).is_absolute() || !Project::IsLoaded() ? fs::path(modelPath) : Project::Resolve(modelPath);
    p += ".meta";
    return p;
}

ModelImportSettings AssetManager::LoadImportSettings(const std::string& modelPath) {
    ModelImportSettings s;
    if (auto text = FileSystem::ReadText(MetaPath(modelPath))) {
        try {
            json j = json::parse(*text);
            s.scale = JGet(j, "scale", 1.0f);
            s.rotation = JGet(j, "rotation", vec3(0.0f));
            s.faceForward = JGet(j, "faceForward", true);
        } catch (...) {}
    }
    return s;
}

void AssetManager::SaveImportSettings(const std::string& modelPath, const ModelImportSettings& s) {
    fs::path meta = MetaPath(modelPath);
    std::error_code ec;
    if (s.scale == 1.0f && s.rotation == vec3(0.0f) && s.faceForward) fs::remove(meta, ec);
    else FileSystem::WriteText(meta, json{ { "scale", s.scale }, { "rotation", s.rotation }, { "faceForward", s.faceForward } }.dump(2));
    // Reload the model and everything cut out of it
    std::string base = modelPath.substr(0, modelPath.find('#'));
    for (auto it = s_Models.begin(); it != s_Models.end();)
        it = it->first.substr(0, it->first.find('#')) == base ? s_Models.erase(it) : std::next(it);
    s_Version++;
}

// ============================================================ Model loading
namespace ModelLoader {

Ref<Model> Load(const std::string& path) {
    EU_PROFILE_SCOPE("LoadModel");
    std::string e = Ext(path);
    Ref<Model> model;
    if (e == ".gltf" || e == ".glb") model = LoadGLTF(path);
    else if (e == ".obj") model = LoadOBJ(path);
    else if (e == ".fbx") model = LoadFBX(path);
    else if (BlenderBridge::Handles(path)) {
        // Synchronous conversion (tools); the editor and games go through AssetManager instead
        fs::path glb;
        std::string message;
        if (BlenderBridge::ConvertNow(path, glb, &message) == BlenderBridge::Status::Ready) {
            model = LoadGLTF(glb.string());
            if (model) model->sourceFormat = "Blender (" + e.substr(1) + ", converted to glTF)";
        } else {
            EU_ERROR("Can't open {}: {}", path, message);
        }
    } else {
        EU_ERROR("Unsupported model format: {}", path);
        return nullptr;
    }
    if (model) ModelImport::ApplyImportSettings(*model, path);
    return model;
}

std::string Describe(const Model& m) {
    std::string out;
    auto line = [&](const std::string& s) { out += s + "\n"; };
    vec3 size = m.bounds.Valid() ? m.bounds.max - m.bounds.min : vec3(0.0f);
    size_t verts = 0, tris = 0;
    for (auto& sm : m.meshes) { verts += sm.mesh->Vertices().size(); tris += sm.mesh->Indices().size() / 3; }
    line(m.path + "  [" + (m.sourceFormat.empty() ? "?" : m.sourceFormat) + "]");
    line(Format("  size {} x {} x {} m, {} meshes, {} vertices, {} triangles", (int)(size.x * 100) / 100.0f, (int)(size.y * 100) / 100.0f,
                (int)(size.z * 100) / 100.0f, m.meshes.size(), verts, tris));
    auto objects = m.ObjectNames();
    if (objects.size() > 1) {
        std::string list;
        for (size_t i = 0; i < objects.size() && i < 40; i++) list += (i ? ", " : "") + objects[i];
        if (objects.size() > 40) list += Format(", ... ({} more)", objects.size() - 40);
        line(Format("  {} objects: {}", objects.size(), list));
    }
    line(Format("  {} materials:", m.materials.size()));
    for (auto& md : m.materials) {
        line("    " + (md.name.empty() ? std::string("(unnamed)") : md.name) + Format("  roughness {} metallic {}", md.roughness, md.metallic));
        for (auto& n : md.textureNotes) line("      " + n);
        if (md.textureNotes.empty() && !md.albedo) line("      (no textures)");
    }
    if (m.skeleton) {
        auto human = ClassifyHumanoid(*m.skeleton);
        int found = 0;
        std::string missing;
        for (int b = 0; b < (int)HumanBone::Count; b++) {
            if (std::find(human.begin(), human.end(), (HumanBone)b) != human.end()) found++;
            else if (b != (int)HumanBone::UpperChest && b != (int)HumanBone::LeftToes && b != (int)HumanBone::RightToes &&
                     b != (int)HumanBone::LeftShoulder && b != (int)HumanBone::RightShoulder) missing += std::string(" ") + HumanBoneName((HumanBone)b);
        }
        line(Format("  skeleton: {} joints, humanoid bones {}/{}{}", m.skeleton->joints.size(), found, (int)HumanBone::Count,
                    missing.empty() ? "" : " (missing:" + missing + ")"));
    }
    if (!m.clips.empty()) {
        std::string list;
        for (size_t i = 0; i < m.clips.size(); i++) list += (i ? ", " : "") + m.clips[i]->name + Format(" ({}s)", (int)(m.clips[i]->duration * 10) / 10.0f);
        line(Format("  {} animations: {}", m.clips.size(), list));
    }
    return out;
}

Ref<Model> ExtractObject(const Model& whole, const std::string& object) {
    auto model = MakeRef<Model>();
    model->sourceFormat = whole.sourceFormat;
    model->materials = whole.materials;
    for (auto& sm : whole.meshes) {
        if (sm.node != object) continue;
        bool skinned = false;
        for (auto& v : sm.mesh->Vertices()) if (v.weights != vec4(0.0f)) { skinned = true; break; }
        if (skinned) {
            // A character: keep it as it is, with its skeleton and animations
            model->meshes.push_back(sm);
            model->skeleton = whole.skeleton;
            model->clips = whole.clips;
            model->bounds.Expand(sm.mesh->Bounds());
            continue;
        }
        // Move the object to its own pivot (keeping its rotation and scale out of it too would
        // make "place it as authored" impossible, so only the position is removed).
        mat4 toLocal = glm::translate(mat4(1.0f), -vec3(sm.nodeWorld[3]));
        auto verts = sm.mesh->Vertices();
        for (auto& v : verts) v.position = vec3(toLocal * vec4(v.position, 1.0f));
        SubMesh out = sm;
        out.mesh = MakeRef<Mesh>(verts, sm.mesh->Indices());
        out.nodeWorld = toLocal * sm.nodeWorld;
        model->bounds.Expand(out.mesh->Bounds());
        model->meshes.push_back(out);
    }
    if (model->meshes.empty()) {
        EU_WARN("No object named '{}' in {}", object, whole.path);
        return nullptr;
    }
    return model;
}

// ------------------------------------------------------------------ glTF
static mat4 NodeLocal(const cgltf_node* n) {
    float m[16];
    cgltf_node_transform_local(n, m);
    return glm::make_mat4(m);
}
static mat4 NodeWorld(const cgltf_node* n) {
    float m[16];
    cgltf_node_transform_world(n, m);
    return glm::make_mat4(m);
}

static Ref<Texture2D> LoadGLTFImage(const cgltf_image* img, const fs::path& dir, MaterialData& md, const char* slot) {
    if (!img) return nullptr;
    if (img->buffer_view && img->buffer_view->buffer->data) {
        const uint8_t* data = (const uint8_t*)img->buffer_view->buffer->data + img->buffer_view->offset;
        md.textureNotes.push_back(std::string(slot) + ": embedded");
        return Texture2D::FromMemory(data, img->buffer_view->size);
    }
    if (img->uri) {
        std::string uri = img->uri;
        if (uri.rfind("data:", 0) == 0) {
            auto comma = uri.find(',');
            if (comma == std::string::npos) return nullptr;
            auto bytes = Base64Decode(uri.substr(comma + 1));
            return Texture2D::FromMemory(bytes.data(), bytes.size());
        }
        std::string decoded = uri;
        cgltf_decode_uri(decoded.data());
        decoded = decoded.c_str();
        // Usually right next to the .gltf; asset packs often keep them in a Textures folder instead
        if (auto tex = ModelImport::Resolve(decoded, dir, md, slot)) return tex;
        EU_WARN("glTF texture not found: {}", (dir / decoded).string());
    }
    return nullptr;
}

template <typename T, int N>
static void ReadKeys(const cgltf_accessor* acc, std::vector<T>& out, bool cubic) {
    size_t count = acc->count;
    std::vector<float> tmp(count * N);
    for (size_t i = 0; i < count; i++) cgltf_accessor_read_float(acc, i, &tmp[i * N], N);
    size_t stride = cubic ? 3 : 1, offset = cubic ? 1 : 0;
    for (size_t i = offset; i < count; i += stride) {
        T v;
        if constexpr (N == 3) v = T(tmp[i * 3], tmp[i * 3 + 1], tmp[i * 3 + 2]);
        else v = T(tmp[i * 4 + 3], tmp[i * 4], tmp[i * 4 + 1], tmp[i * 4 + 2]); // quat(w, x, y, z)
        out.push_back(v);
    }
}

Ref<Model> LoadGLTF(const std::string& path) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
        EU_ERROR("Failed to parse glTF {}", path);
        return nullptr;
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        EU_ERROR("Failed to load glTF buffers for {}", path);
        cgltf_free(data);
        return nullptr;
    }
    fs::path dir = fs::path(path).parent_path();
    auto model = MakeRef<Model>();
    model->sourceFormat = "glTF";

    // Materials
    for (size_t i = 0; i < data->materials_count; i++) {
        const cgltf_material& m = data->materials[i];
        MaterialData md;
        md.name = m.name ? m.name : "Material";
        if (m.has_pbr_metallic_roughness) {
            auto& pbr = m.pbr_metallic_roughness;
            md.baseColor = glm::make_vec4(pbr.base_color_factor);
            if (pbr.base_color_texture.texture) md.albedo = LoadGLTFImage(pbr.base_color_texture.texture->image, dir, md, "albedo");
            md.metallic = pbr.metallic_factor;
            md.roughness = pbr.roughness_factor;
            if (pbr.metallic_roughness_texture.texture)
                md.metallicRoughnessMap = LoadGLTFImage(pbr.metallic_roughness_texture.texture->image, dir, md, "metalRough");
        }
        if (m.normal_texture.texture) {
            md.normalMap = LoadGLTFImage(m.normal_texture.texture->image, dir, md, "normal");
            md.normalScale = m.normal_texture.scale;
        }
        if (m.occlusion_texture.texture) md.occlusionMap = LoadGLTFImage(m.occlusion_texture.texture->image, dir, md, "occlusion");
        md.emissive = glm::make_vec3(m.emissive_factor);
        if (m.has_emissive_strength) md.emissive *= m.emissive_strength.emissive_strength;
        if (m.emissive_texture.texture) md.emissiveMap = LoadGLTFImage(m.emissive_texture.texture->image, dir, md, "emissive");
        md.doubleSided = m.double_sided;
        // glTF colors are linear; our shaders expect sRGB-space inputs.
        md.baseColor = vec4(glm::pow(vec3(md.baseColor), vec3(1.0f / 2.2f)), md.baseColor.a);
        model->materials.push_back(md);
    }

    // Skeleton: the joints of every skin (modular characters have one skin per outfit piece,
    // all on the same bones) + all of their ancestors, ordered parents-first.
    std::unordered_map<const cgltf_node*, int> jointIndex;
    std::unordered_map<const cgltf_skin*, std::vector<int>> skinMaps;
    const cgltf_skin* skin = data->skins_count > 0 ? &data->skins[0] : nullptr;
    if (skin) {
        std::set<const cgltf_node*> needed;
        for (size_t si = 0; si < data->skins_count; si++)
            for (size_t i = 0; i < data->skins[si].joints_count; i++)
                for (const cgltf_node* n = data->skins[si].joints[i]; n; n = n->parent) needed.insert(n);
        auto skel = MakeRef<Skeleton>();
        std::function<void(const cgltf_node*, int)> visit = [&](const cgltf_node* n, int parent) {
            int self = parent;
            if (needed.count(n)) {
                Joint j;
                j.name = n->name ? n->name : ("joint" + std::to_string(skel->joints.size()));
                j.parent = parent;
                DecomposeTRS(NodeLocal(n), j.bindT, j.bindR, j.bindS);
                self = (int)skel->joints.size();
                jointIndex[n] = self;
                skel->joints.push_back(j);
            }
            for (size_t c = 0; c < n->children_count; c++) visit(n->children[c], self);
        };
        for (size_t i = 0; i < data->nodes_count; i++)
            if (!data->nodes[i].parent) visit(&data->nodes[i], -1);
        for (size_t si = 0; si < data->skins_count; si++) {
            const cgltf_skin& sk = data->skins[si];
            auto& map = skinMaps[&sk];
            map.resize(sk.joints_count, 0);
            for (size_t i = 0; i < sk.joints_count; i++) {
                int idx = jointIndex[sk.joints[i]];
                map[i] = idx;
                if (sk.inverse_bind_matrices) {
                    float m[16];
                    cgltf_accessor_read_float(sk.inverse_bind_matrices, i, m, 16);
                    skel->joints[idx].inverseBind = glm::make_mat4(m);
                    skel->joints[idx].skinned = true;
                }
            }
        }
        skel->rootJoint = skin->joints_count > 0 ? skinMaps[skin][0] : 0;
        if (skel->joints.size() > 100) EU_WARN("{} has {} joints; the shader supports 100", path, skel->joints.size());
        model->skeleton = skel;
    }

    // Meshes
    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        const cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        bool skinned = node->skin != nullptr && skin != nullptr;
        const std::vector<int>& skinToSkeleton = skinned ? skinMaps[node->skin] : skinMaps[nullptr];
        mat4 world = skinned ? mat4(1.0f) : NodeWorld(node);
        mat3 normalMat = glm::transpose(glm::inverse(mat3(world)));
        for (size_t pi = 0; pi < node->mesh->primitives_count; pi++) {
            const cgltf_primitive& prim = node->mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor* posAcc = nullptr;
            for (size_t a = 0; a < prim.attributes_count; a++)
                if (prim.attributes[a].type == cgltf_attribute_type_position) posAcc = prim.attributes[a].data;
            if (!posAcc) continue;
            std::vector<Vertex> verts(posAcc->count);
            bool hasNormals = false;
            for (size_t a = 0; a < prim.attributes_count; a++) {
                const cgltf_attribute& attr = prim.attributes[a];
                const cgltf_accessor* acc = attr.data;
                for (size_t v = 0; v < acc->count && v < verts.size(); v++) {
                    float f[4] = { 0, 0, 0, 0 };
                    switch (attr.type) {
                    case cgltf_attribute_type_position:
                        cgltf_accessor_read_float(acc, v, f, 3);
                        verts[v].position = vec3(world * vec4(f[0], f[1], f[2], 1.0f));
                        break;
                    case cgltf_attribute_type_normal:
                        if (attr.index != 0) break;
                        cgltf_accessor_read_float(acc, v, f, 3);
                        verts[v].normal = glm::normalize(normalMat * vec3(f[0], f[1], f[2]));
                        hasNormals = true;
                        break;
                    case cgltf_attribute_type_texcoord:
                        if (attr.index != 0) break;
                        cgltf_accessor_read_float(acc, v, f, 2);
                        verts[v].uv = { f[0], f[1] };
                        break;
                    case cgltf_attribute_type_tangent:
                        if (attr.index != 0) break;
                        cgltf_accessor_read_float(acc, v, f, 4);
                        verts[v].tangent = vec4(glm::normalize(mat3(world) * vec3(f[0], f[1], f[2])), f[3] < 0.0f ? -1.0f : 1.0f);
                        break;
                    case cgltf_attribute_type_joints:
                        if (attr.index != 0 || !skinned) break;
                        {
                            cgltf_uint j[4] = { 0, 0, 0, 0 };
                            cgltf_accessor_read_uint(acc, v, j, 4);
                            for (int k = 0; k < 4; k++)
                                verts[v].joints[k] = j[k] < skinToSkeleton.size() ? skinToSkeleton[j[k]] : 0;
                        }
                        break;
                    case cgltf_attribute_type_weights:
                        if (attr.index != 0 || !skinned) break;
                        cgltf_accessor_read_float(acc, v, f, 4);
                        verts[v].weights = { f[0], f[1], f[2], f[3] };
                        break;
                    default: break;
                    }
                }
            }
            std::vector<uint32_t> indices;
            if (prim.indices) {
                indices.resize(prim.indices->count);
                for (size_t i = 0; i < prim.indices->count; i++) indices[i] = (uint32_t)cgltf_accessor_read_index(prim.indices, i);
            } else {
                indices.resize(verts.size());
                for (size_t i = 0; i < verts.size(); i++) indices[i] = (uint32_t)i;
            }
            if (!hasNormals) Mesh::ComputeNormals(verts, indices);
            // Mirrored node transforms flip winding
            if (glm::determinant(mat3(world)) < 0.0f)
                for (size_t i = 0; i + 2 < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);
            SubMesh sm;
            sm.mesh = MakeRef<Mesh>(verts, indices);
            sm.material = prim.material ? (int)(prim.material - data->materials) : -1;
            sm.node = node->name ? node->name : (node->mesh->name ? node->mesh->name : "");
            sm.nodeWorld = NodeWorld(node);
            model->bounds.Expand(sm.mesh->Bounds());
            model->meshes.push_back(sm);
        }
    }

    // Animations (joint channels only)
    if (model->skeleton) {
        for (size_t ai = 0; ai < data->animations_count; ai++) {
            const cgltf_animation& anim = data->animations[ai];
            auto clip = MakeRef<AnimationClip>();
            clip->name = ModelImport::CleanClipName(anim.name ? anim.name : ("Animation" + std::to_string(ai)), path, data->animations_count);
            std::unordered_map<int, size_t> channelOf;
            for (size_t ci = 0; ci < anim.channels_count; ci++) {
                const cgltf_animation_channel& ch = anim.channels[ci];
                auto it = jointIndex.find(ch.target_node);
                if (it == jointIndex.end() || !ch.sampler) continue;
                int j = it->second;
                if (!channelOf.count(j)) {
                    channelOf[j] = clip->channels.size();
                    JointChannel jc;
                    jc.joint = j;
                    clip->channels.push_back(jc);
                }
                JointChannel& jc = clip->channels[channelOf[j]];
                const cgltf_animation_sampler* s = ch.sampler;
                bool cubic = s->interpolation == cgltf_interpolation_type_cubic_spline;
                std::vector<float> times(s->input->count);
                for (size_t k = 0; k < times.size(); k++) cgltf_accessor_read_float(s->input, k, &times[k], 1);
                if (!times.empty()) clip->duration = std::max(clip->duration, times.back());
                switch (ch.target_path) {
                case cgltf_animation_path_type_translation: jc.translation.times = times; ReadKeys<vec3, 3>(s->output, jc.translation.values, cubic); break;
                case cgltf_animation_path_type_rotation: jc.rotation.times = times; ReadKeys<quat, 4>(s->output, jc.rotation.values, cubic); break;
                case cgltf_animation_path_type_scale: jc.scale.times = times; ReadKeys<vec3, 3>(s->output, jc.scale.values, cubic); break;
                default: break;
                }
            }
            if (!clip->channels.empty()) model->clips.push_back(clip);
        }
    }

    cgltf_free(data);
    EU_INFO("Loaded {} ({} meshes, {} materials, {} joints, {} clips)", fs::path(path).filename().string(), model->meshes.size(),
            model->materials.size(), model->skeleton ? model->skeleton->joints.size() : 0, model->clips.size());
    return model;
}

// ------------------------------------------------------------------ OBJ
Ref<Model> LoadOBJ(const std::string& path) {
    tinyobj::ObjReaderConfig cfg;
    cfg.triangulate = true;
    cfg.mtl_search_path = fs::path(path).parent_path().string();
    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, cfg)) {
        EU_ERROR("OBJ error in {}: {}", path, reader.Error());
        return nullptr;
    }
    auto& attrib = reader.GetAttrib();
    auto& shapes = reader.GetShapes();
    auto& mats = reader.GetMaterials();
    auto model = MakeRef<Model>();
    model->sourceFormat = "OBJ";
    fs::path dir = fs::path(path).parent_path();
    for (auto& m : mats) {
        MaterialData md;
        md.name = m.name;
        md.baseColor = vec4(m.diffuse[0], m.diffuse[1], m.diffuse[2], m.dissolve);
        md.roughness = std::max(0.4f, RoughnessFromShininess(std::max(2.0f, m.shininess)));
        if (m.roughness > 0.0f) md.roughness = m.roughness;      // PBR extension (Pr)
        md.metallic = m.metallic;                                // (Pm)
        std::string colorPath;
        if (!m.diffuse_texname.empty()) md.albedo = ModelImport::Resolve(m.diffuse_texname, dir, md, "albedo", &colorPath);
        if (md.albedo) md.baseColor = vec4(1.0f, 1.0f, 1.0f, md.baseColor.a);
        if (!m.normal_texname.empty()) md.normalMap = ModelImport::Resolve(m.normal_texname, dir, md, "normal");
        else if (!m.bump_texname.empty()) md.normalMap = ModelImport::Resolve(m.bump_texname, dir, md, "normal");
        if (!m.emissive_texname.empty()) {
            md.emissiveMap = ModelImport::Resolve(m.emissive_texname, dir, md, "emissive");
            if (md.emissiveMap) md.emissive = vec3(1.0f);
        }
        ModelImport::AddCompanionMaps(md, colorPath);
        model->materials.push_back(md);
    }
    // One submesh per object and material ("o"/"g" groups become separate objects)
    std::map<std::pair<size_t, int>, std::pair<std::vector<Vertex>, std::vector<uint32_t>>> groups;
    bool missingNormals = false;
    for (size_t si = 0; si < shapes.size(); si++) {
        auto& shape = shapes[si];
        size_t offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int matId = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[f];
            auto& g = groups[{ si, matId }];
            for (int v = 0; v < 3; v++) {
                tinyobj::index_t idx = shape.mesh.indices[offset + v];
                Vertex vert;
                vert.position = { attrib.vertices[3 * idx.vertex_index], attrib.vertices[3 * idx.vertex_index + 1], attrib.vertices[3 * idx.vertex_index + 2] };
                if (idx.normal_index >= 0)
                    vert.normal = { attrib.normals[3 * idx.normal_index], attrib.normals[3 * idx.normal_index + 1], attrib.normals[3 * idx.normal_index + 2] };
                else missingNormals = true;
                if (idx.texcoord_index >= 0)   // OBJ UVs start bottom-left; our images top-left
                    vert.uv = { attrib.texcoords[2 * idx.texcoord_index], 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1] };
                g.second.push_back((uint32_t)g.first.size());
                g.first.push_back(vert);
            }
            offset += 3;
        }
    }
    for (auto& [key, g] : groups) {
        if (missingNormals) Mesh::ComputeNormals(g.first, g.second);
        ModelImport::Weld(g.first, g.second);
        SubMesh sm;
        sm.mesh = MakeRef<Mesh>(g.first, g.second);
        sm.material = key.second;
        sm.node = shapes[key.first].name;
        // OBJ has no pivots: use the bottom centre of the object
        AABB b = sm.mesh->Bounds();
        sm.nodeWorld = glm::translate(mat4(1.0f), vec3(b.Center().x, b.min.y, b.Center().z));
        model->bounds.Expand(sm.mesh->Bounds());
        model->meshes.push_back(sm);
    }
    return model;
}

} // namespace ModelLoader
} // namespace eu
