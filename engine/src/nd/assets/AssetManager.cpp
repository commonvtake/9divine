#include "nd/assets/AssetManager.h"
#include "nd/core/FileSystem.h"
#include "nd/core/Json.h"
#include "nd/core/Log.h"
#include "nd/core/Profiler.h"
#include <cgltf.h>
#include <tiny_obj_loader.h>
#include <ufbx.h>
#include <chrono>
#include <set>
#include <cstring>

namespace nd {

std::map<std::string, AssetManager::ModelEntry> AssetManager::s_Models;
std::map<std::string, AssetManager::TextureEntry> AssetManager::s_Textures;
uint32_t AssetManager::s_Version = 1;

static std::string Lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static std::string Ext(const std::string& p) { return Lower(fs::path(p).extension().string()); }

bool AssetManager::IsModelFile(const std::string& p) { auto e = Ext(p); return e == ".gltf" || e == ".glb" || e == ".obj" || e == ".fbx"; }
bool AssetManager::IsTextureFile(const std::string& p) { auto e = Ext(p); return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp"; }
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
    } else {
        fs::path full = Project::Resolve(path);
        stamp = FileSystem::LastWriteTime(full);
        model = ModelLoader::Load(full.string());
        if (model) model->path = path;
    }
    if (!model) ND_WARN("Could not load model '{}'", path);
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
    bool changed = false;
    for (auto it = s_Models.begin(); it != s_Models.end();) {
        if (it->first.rfind("builtin:", 0) != 0 && FileSystem::LastWriteTime(Project::Resolve(it->first)) != it->second.stamp) {
            ND_INFO("Reloading model {}", it->first);
            it = s_Models.erase(it);
            changed = true;
        } else ++it;
    }
    for (auto it = s_Textures.begin(); it != s_Textures.end();) {
        if (FileSystem::LastWriteTime(Project::Resolve(it->first)) != it->second.stamp) {
            ND_INFO("Reloading texture {}", it->first);
            it = s_Textures.erase(it);
            changed = true;
        } else ++it;
    }
    if (changed) s_Version++;
}

void AssetManager::ClearCache() {
    s_Models.clear();
    s_Textures.clear();
    s_Version++;
}

// ============================================================ Model loading
namespace ModelLoader {

Ref<Model> Load(const std::string& path) {
    ND_PROFILE_SCOPE("LoadModel");
    std::string e = Ext(path);
    if (e == ".gltf" || e == ".glb") return LoadGLTF(path);
    if (e == ".obj") return LoadOBJ(path);
    if (e == ".fbx") return LoadFBX(path);
    ND_ERROR("Unsupported model format: {}", path);
    return nullptr;
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

static Ref<Texture2D> LoadGLTFImage(const cgltf_image* img, const fs::path& dir) {
    if (!img) return nullptr;
    if (img->buffer_view && img->buffer_view->buffer->data) {
        const uint8_t* data = (const uint8_t*)img->buffer_view->buffer->data + img->buffer_view->offset;
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
        auto bytes = FileSystem::ReadBinary(dir / decoded);
        if (bytes) return Texture2D::FromMemory(bytes->data(), bytes->size());
        ND_WARN("glTF texture not found: {}", (dir / decoded).string());
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
        ND_ERROR("Failed to parse glTF {}", path);
        return nullptr;
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        ND_ERROR("Failed to load glTF buffers for {}", path);
        cgltf_free(data);
        return nullptr;
    }
    fs::path dir = fs::path(path).parent_path();
    auto model = MakeRef<Model>();

    // Materials
    for (size_t i = 0; i < data->materials_count; i++) {
        const cgltf_material& m = data->materials[i];
        MaterialData md;
        md.name = m.name ? m.name : "Material";
        if (m.has_pbr_metallic_roughness) {
            auto& pbr = m.pbr_metallic_roughness;
            md.baseColor = glm::make_vec4(pbr.base_color_factor);
            if (pbr.base_color_texture.texture) md.albedo = LoadGLTFImage(pbr.base_color_texture.texture->image, dir);
            float rough = pbr.roughness_factor;
            md.shininess = glm::mix(96.0f, 8.0f, rough);
            md.specular = (1.0f - rough) * 0.5f;
        }
        md.emissive = glm::make_vec3(m.emissive_factor);
        md.doubleSided = m.double_sided;
        // glTF colors are linear; our shaders expect sRGB-space inputs.
        md.baseColor = vec4(glm::pow(vec3(md.baseColor), vec3(1.0f / 2.2f)), md.baseColor.a);
        model->materials.push_back(md);
    }

    // Skeleton (first skin): skin joints + all of their ancestors, ordered parents-first.
    std::unordered_map<const cgltf_node*, int> jointIndex;
    std::vector<int> skinToSkeleton;
    const cgltf_skin* skin = data->skins_count > 0 ? &data->skins[0] : nullptr;
    if (skin) {
        std::set<const cgltf_node*> needed;
        for (size_t i = 0; i < skin->joints_count; i++)
            for (const cgltf_node* n = skin->joints[i]; n; n = n->parent) needed.insert(n);
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
        skinToSkeleton.resize(skin->joints_count, 0);
        for (size_t i = 0; i < skin->joints_count; i++) {
            int idx = jointIndex[skin->joints[i]];
            skinToSkeleton[i] = idx;
            if (skin->inverse_bind_matrices) {
                float m[16];
                cgltf_accessor_read_float(skin->inverse_bind_matrices, i, m, 16);
                skel->joints[idx].inverseBind = glm::make_mat4(m);
            }
        }
        skel->rootJoint = skin->joints_count > 0 ? skinToSkeleton[0] : 0;
        if (skel->joints.size() > 100) ND_WARN("{} has {} joints; the shader supports 100", path, skel->joints.size());
        model->skeleton = skel;
    }

    // Meshes
    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        const cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        bool skinned = node->skin != nullptr && skin != nullptr;
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
            model->bounds.Expand(sm.mesh->Bounds());
            model->meshes.push_back(sm);
        }
    }

    // Animations (joint channels only)
    if (model->skeleton) {
        for (size_t ai = 0; ai < data->animations_count; ai++) {
            const cgltf_animation& anim = data->animations[ai];
            auto clip = MakeRef<AnimationClip>();
            clip->name = anim.name ? anim.name : ("Animation" + std::to_string(ai));
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
    ND_INFO("Loaded {} ({} meshes, {} materials, {} joints, {} clips)", fs::path(path).filename().string(), model->meshes.size(),
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
        ND_ERROR("OBJ error in {}: {}", path, reader.Error());
        return nullptr;
    }
    auto& attrib = reader.GetAttrib();
    auto& shapes = reader.GetShapes();
    auto& mats = reader.GetMaterials();
    auto model = MakeRef<Model>();
    fs::path dir = fs::path(path).parent_path();
    for (auto& m : mats) {
        MaterialData md;
        md.name = m.name;
        md.baseColor = vec4(m.diffuse[0], m.diffuse[1], m.diffuse[2], m.dissolve);
        md.shininess = std::max(2.0f, m.shininess);
        md.specular = (m.specular[0] + m.specular[1] + m.specular[2]) / 3.0f;
        if (!m.diffuse_texname.empty()) md.albedo = Texture2D::FromFile((dir / m.diffuse_texname).string());
        model->materials.push_back(md);
    }
    // One submesh per material
    std::map<int, std::pair<std::vector<Vertex>, std::vector<uint32_t>>> groups;
    bool missingNormals = false;
    for (auto& shape : shapes) {
        size_t offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int matId = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[f];
            auto& g = groups[matId];
            for (int v = 0; v < 3; v++) {
                tinyobj::index_t idx = shape.mesh.indices[offset + v];
                Vertex vert;
                vert.position = { attrib.vertices[3 * idx.vertex_index], attrib.vertices[3 * idx.vertex_index + 1], attrib.vertices[3 * idx.vertex_index + 2] };
                if (idx.normal_index >= 0)
                    vert.normal = { attrib.normals[3 * idx.normal_index], attrib.normals[3 * idx.normal_index + 1], attrib.normals[3 * idx.normal_index + 2] };
                else missingNormals = true;
                if (idx.texcoord_index >= 0)
                    vert.uv = { attrib.texcoords[2 * idx.texcoord_index], attrib.texcoords[2 * idx.texcoord_index + 1] };
                g.second.push_back((uint32_t)g.first.size());
                g.first.push_back(vert);
            }
            offset += 3;
        }
    }
    for (auto& [mat, g] : groups) {
        if (missingNormals) Mesh::ComputeNormals(g.first, g.second);
        SubMesh sm;
        sm.mesh = MakeRef<Mesh>(g.first, g.second);
        sm.material = mat;
        model->bounds.Expand(sm.mesh->Bounds());
        model->meshes.push_back(sm);
    }
    return model;
}

// ------------------------------------------------------------------ FBX
Ref<Model> LoadFBX(const std::string& path) {
    ufbx_load_opts opts{};
    opts.target_axes.right = UFBX_COORDINATE_AXIS_POSITIVE_X;
    opts.target_axes.up = UFBX_COORDINATE_AXIS_POSITIVE_Y;
    opts.target_axes.front = UFBX_COORDINATE_AXIS_POSITIVE_Z;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;
    ufbx_error err;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &err);
    if (!scene) {
        ND_ERROR("FBX error in {}: {}", path, std::string(err.description.data, err.description.length));
        return nullptr;
    }
    auto model = MakeRef<Model>();
    std::unordered_map<const ufbx_material*, int> matIndex;
    auto getMaterial = [&](const ufbx_material* m) -> int {
        if (!m) return -1;
        auto it = matIndex.find(m);
        if (it != matIndex.end()) return it->second;
        MaterialData md;
        md.name = std::string(m->name.data, m->name.length);
        const ufbx_material_map& base = m->pbr.base_color.has_value ? m->pbr.base_color : m->fbx.diffuse_color;
        md.baseColor = vec4((float)base.value_vec4.x, (float)base.value_vec4.y, (float)base.value_vec4.z, 1.0f);
        if (md.baseColor == vec4(0, 0, 0, 1)) md.baseColor = vec4(1.0f);
        const ufbx_texture* tex = base.texture;
        if (tex) {
            if (tex->content.size > 0) md.albedo = Texture2D::FromMemory(tex->content.data, tex->content.size);
            else {
                std::string file(tex->absolute_filename.data, tex->absolute_filename.length);
                if (!fs::exists(file)) file = (fs::path(path).parent_path() / std::string(tex->relative_filename.data, tex->relative_filename.length)).string();
                auto bytes = FileSystem::ReadBinary(file);
                if (bytes) md.albedo = Texture2D::FromMemory(bytes->data(), bytes->size());
            }
        }
        int idx = (int)model->materials.size();
        model->materials.push_back(md);
        matIndex[m] = idx;
        return idx;
    };

    for (size_t ni = 0; ni < scene->nodes.count; ni++) {
        ufbx_node* node = scene->nodes.data[ni];
        ufbx_mesh* mesh = node->mesh;
        if (!mesh) continue;
        std::map<int, std::pair<std::vector<Vertex>, std::vector<uint32_t>>> groups;
        std::vector<uint32_t> tri(mesh->max_face_triangles * 3);
        for (size_t fi = 0; fi < mesh->num_faces; fi++) {
            ufbx_face face = mesh->faces.data[fi];
            int mat = -1;
            if (mesh->face_material.count > fi && mesh->materials.count > 0) {
                uint32_t mi = mesh->face_material.data[fi];
                if (mi < mesh->materials.count) mat = getMaterial(mesh->materials.data[mi]);
            }
            auto& g = groups[mat];
            uint32_t n = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
            for (uint32_t k = 0; k < n * 3; k++) {
                uint32_t ix = tri[k];
                Vertex v;
                ufbx_vec3 p = ufbx_transform_position(&node->geometry_to_world, ufbx_get_vertex_vec3(&mesh->vertex_position, ix));
                v.position = { (float)p.x, (float)p.y, (float)p.z };
                if (mesh->vertex_normal.exists) {
                    ufbx_vec3 nn = ufbx_transform_direction(&node->geometry_to_world, ufbx_get_vertex_vec3(&mesh->vertex_normal, ix));
                    v.normal = glm::normalize(vec3((float)nn.x, (float)nn.y, (float)nn.z));
                }
                if (mesh->vertex_uv.exists) {
                    ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix);
                    v.uv = { (float)uv.x, (float)uv.y };
                }
                g.second.push_back((uint32_t)g.first.size());
                g.first.push_back(v);
            }
        }
        for (auto& [mat, g] : groups) {
            if (g.first.empty()) continue;
            SubMesh sm;
            sm.mesh = MakeRef<Mesh>(g.first, g.second);
            sm.material = mat;
            model->bounds.Expand(sm.mesh->Bounds());
            model->meshes.push_back(sm);
        }
    }
    ufbx_free_scene(scene);
    // FBX textures use bottom-left UV origin like OpenGL, but we decode without flipping;
    // flip V on the meshes to match.
    for (auto& sm : model->meshes) {
        auto verts = sm.mesh->Vertices();
        for (auto& v : verts) v.uv.y = 1.0f - v.uv.y;
        sm.mesh = MakeRef<Mesh>(verts, sm.mesh->Indices());
    }
    return model;
}

} // namespace ModelLoader
} // namespace nd
