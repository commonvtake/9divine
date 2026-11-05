// FBX import (ufbx): static and skinned meshes, skeletons, every animation take, and materials
// with color / normal / emissive / roughness / metallic maps (embedded or on disk).
#include "euphoria/assets/AssetManager.h"
#include "euphoria/assets/ModelImport.h"
#include "euphoria/core/FileSystem.h"
#include "euphoria/core/Log.h"
#include <ufbx.h>
#include <functional>
#include <unordered_map>
#include <set>

namespace eu::ModelLoader {

namespace {

mat4 ToMat4(const ufbx_matrix& m) {
    mat4 r(1.0f);
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 3; k++) r[c][k] = (float)m.cols[c].v[k];
    return r;
}

std::string Str(const ufbx_string& s) { return std::string(s.data, s.length); }

// A texture slot on an FBX material: embedded bytes, or a file found next to the model.
Ref<Texture2D> LoadFbxTexture(const ufbx_texture* tex, const fs::path& dir, MaterialData& md, const char* slot, std::string* used = nullptr) {
    if (!tex) return nullptr;
    // Layered / procedural textures: use the first file texture inside
    if (tex->type != UFBX_TEXTURE_FILE && tex->file_textures.count > 0) tex = tex->file_textures.data[0];
    if (tex->content.size > 0) {
        md.textureNotes.push_back(std::string(slot) + ": embedded");
        return Texture2D::FromMemory(tex->content.data, tex->content.size);
    }
    std::string last;
    for (const ufbx_string* name : { &tex->filename, &tex->relative_filename, &tex->absolute_filename }) {
        if (name->length == 0) continue;
        last = Str(*name);
        if (auto t = ModelImport::Resolve(last, dir, md, slot, used, false)) return t;
    }
    if (!last.empty()) md.textureNotes.push_back(std::string(slot) + ": missing " + last.substr(last.find_last_of("/\\") == std::string::npos ? 0 : last.find_last_of("/\\") + 1));
    return nullptr;
}

const ufbx_texture* MapTexture(const ufbx_material_map& a, const ufbx_material_map& b) {
    return a.texture ? a.texture : b.texture;
}

} // namespace

Ref<Model> LoadFBX(const std::string& path) {
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    // The unit / axis conversion lives in the root node, which becomes the skeleton's top
    // joint for skinned files, so bind poses, inverse binds and animation keys all agree.
    opts.space_conversion = UFBX_SPACE_CONVERSION_TRANSFORM_ROOT;
    opts.generate_missing_normals = true;
    opts.ignore_missing_external_files = true;
    ufbx_error err;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &err);
    if (!scene) {
        EU_ERROR("FBX error in {}: {}", path, Str(err.description));
        return nullptr;
    }
    fs::path dir = fs::path(path).parent_path();
    auto model = MakeRef<Model>();
    model->sourceFormat = "FBX";

    // ---- Materials
    std::unordered_map<const ufbx_material*, int> matIndex;
    auto getMaterial = [&](const ufbx_material* m) -> int {
        if (!m) return -1;
        auto it = matIndex.find(m);
        if (it != matIndex.end()) return it->second;
        MaterialData md;
        md.name = Str(m->name);
        const ufbx_material_map& base = m->pbr.base_color.has_value ? m->pbr.base_color : m->fbx.diffuse_color;
        md.baseColor = vec4((float)base.value_vec4.x, (float)base.value_vec4.y, (float)base.value_vec4.z, 1.0f);
        if (m->pbr.opacity.has_value && m->pbr.opacity.value_real < 1.0) md.baseColor.a = (float)m->pbr.opacity.value_real;
        if (vec3(md.baseColor) == vec3(0.0f)) md.baseColor = vec4(1.0f);
        // Roughness: PBR materials say it; classic Lambert / Phong get a sensible matte value
        if (m->shader_type == UFBX_SHADER_FBX_LAMBERT) md.roughness = 0.9f;
        else if (m->shader_type == UFBX_SHADER_FBX_PHONG) md.roughness = std::max(0.45f, RoughnessFromShininess((float)m->fbx.specular_exponent.value_real));
        else {
            if (m->pbr.roughness.has_value) md.roughness = (float)m->pbr.roughness.value_real;
            if (m->pbr.metalness.has_value) md.metallic = (float)m->pbr.metalness.value_real;
        }

        std::string colorPath;
        md.albedo = LoadFbxTexture(MapTexture(m->pbr.base_color, m->fbx.diffuse_color), dir, md, "albedo", &colorPath);
        if (md.albedo) md.baseColor = vec4(1.0f, 1.0f, 1.0f, md.baseColor.a);   // the texture is the color (FBX's 0.8 grey would just darken it)
        md.normalMap = LoadFbxTexture(m->pbr.normal_map.texture ? m->pbr.normal_map.texture
                                      : m->fbx.normal_map.texture ? m->fbx.normal_map.texture : m->fbx.bump.texture, dir, md, "normal");
        md.emissiveMap = LoadFbxTexture(MapTexture(m->pbr.emission_color, m->fbx.emission_color), dir, md, "emissive");
        if (md.emissiveMap) md.emissive = vec3(1.0f);
        else if (m->pbr.emission_color.has_value) {
            float f = m->pbr.emission_factor.has_value ? (float)m->pbr.emission_factor.value_real : 1.0f;
            md.emissive = vec3((float)m->pbr.emission_color.value_vec3.x, (float)m->pbr.emission_color.value_vec3.y,
                               (float)m->pbr.emission_color.value_vec3.z) * f;
        }
        if (const ufbx_texture* rt = m->pbr.roughness.texture) {
            // A grayscale roughness map works directly (its G channel is the value)
            if ((md.metallicRoughnessMap = LoadFbxTexture(rt, dir, md, "roughness"))) md.roughness = 1.0f;
        }
        ModelImport::AddCompanionMaps(md, colorPath);
        int idx = (int)model->materials.size();
        model->materials.push_back(md);
        matIndex[m] = idx;
        return idx;
    };

    // ---- Skeleton: every bone used by a skin, plus their ancestors (including the root node,
    // which carries the unit / axis conversion), ordered parents-first.
    std::set<const ufbx_node*> needed;
    for (size_t si = 0; si < scene->skin_deformers.count; si++) {
        const ufbx_skin_deformer* skin = scene->skin_deformers.data[si];
        for (size_t ci = 0; ci < skin->clusters.count; ci++)
            for (const ufbx_node* n = skin->clusters.data[ci]->bone_node; n; n = n->parent) needed.insert(n);
    }
    std::unordered_map<const ufbx_node*, int> jointIndex;
    if (!needed.empty()) {
        auto skel = MakeRef<Skeleton>();
        std::function<void(const ufbx_node*, int)> visit = [&](const ufbx_node* n, int parent) {
            int self = parent;
            if (needed.count(n)) {
                Joint j;
                j.name = n->is_root ? "FBXRoot" : Str(n->name);
                j.parent = parent;
                DecomposeTRS(ToMat4(n->node_to_parent), j.bindT, j.bindR, j.bindS);
                j.inverseBind = glm::inverse(ToMat4(n->node_to_world));   // replaced below for real bones
                self = (int)skel->joints.size();
                jointIndex[n] = self;
                skel->joints.push_back(j);
            }
            for (size_t c = 0; c < n->children.count; c++) visit(n->children.data[c], self);
        };
        visit(scene->root_node, -1);
        // Inverse bind matrices from the skin clusters (the pose the mesh was bound in)
        for (size_t si = 0; si < scene->skin_deformers.count; si++) {
            const ufbx_skin_deformer* skin = scene->skin_deformers.data[si];
            for (size_t ci = 0; ci < skin->clusters.count; ci++) {
                const ufbx_skin_cluster* cl = skin->clusters.data[ci];
                if (cl->bone_node && jointIndex.count(cl->bone_node)) {
                    Joint& j = skel->joints[jointIndex[cl->bone_node]];
                    j.inverseBind = glm::inverse(ToMat4(cl->bind_to_world));
                    j.skinned = true;
                }
            }
        }
        // Root motion follows the hips-level joint: the first joint below the conversion root
        // that has children (usually "Armature" -> "Hips", or "root")
        skel->rootJoint = skel->joints.size() > 1 ? 1 : 0;
        if (skel->joints.size() > 100) EU_WARN("{} has {} joints; the shader supports 100", path, skel->joints.size());
        model->skeleton = skel;
    }
    // Nearest ancestor that is a joint (for meshes parented to a bone, like a sword in a hand)
    auto jointAbove = [&](const ufbx_node* n) -> int {
        for (const ufbx_node* p = n ? n->parent : nullptr; p; p = p->parent) {
            auto it = jointIndex.find(p);
            if (it != jointIndex.end() && !p->is_root) return it->second;
        }
        return -1;
    };

    // ---- Meshes
    for (size_t ni = 0; ni < scene->nodes.count; ni++) {
        ufbx_node* node = scene->nodes.data[ni];
        ufbx_mesh* mesh = node->mesh;
        if (!mesh) continue;
        const ufbx_skin_deformer* skin = mesh->skin_deformers.count > 0 ? mesh->skin_deformers.data[0] : nullptr;
        if (skin && skin->clusters.count == 0) skin = nullptr;
        int rigidJoint = skin ? -1 : jointAbove(node);
        std::vector<int> clusterJoint;
        if (skin)
            for (size_t ci = 0; ci < skin->clusters.count; ci++) {
                auto it = jointIndex.find(skin->clusters.data[ci]->bone_node);
                clusterJoint.push_back(it == jointIndex.end() ? 0 : it->second);
            }
        // Skinned meshes are placed where they were bound, through the same matrices skinning
        // uses (bone bind pose * geometry-to-bone), so the mesh and its inverse binds agree even
        // when a file's stored mesh transform doesn't; everything else where it is now.
        mat4 toWorld = ToMat4(node->geometry_to_world);
        if (skin) {
            const ufbx_skin_cluster* c0 = skin->clusters.data[0];
            toWorld = ToMat4(c0->bind_to_world) * ToMat4(c0->geometry_to_bone);
        }
        mat3 normalMat = glm::transpose(glm::inverse(mat3(toWorld)));
        bool mirrored = glm::determinant(mat3(toWorld)) < 0.0f;

        std::map<int, std::pair<std::vector<Vertex>, std::vector<uint32_t>>> groups;
        std::vector<uint32_t> tri(mesh->max_face_triangles * 3);
        for (size_t fi = 0; fi < mesh->num_faces; fi++) {
            ufbx_face face = mesh->faces.data[fi];
            int mat = -1;
            if (mesh->face_material.count > fi && mesh->materials.count > 0) {
                uint32_t mi = mesh->face_material.data[fi];
                if (mi < mesh->materials.count) mat = getMaterial(mesh->materials.data[mi]);
            } else if (mesh->materials.count > 0) mat = getMaterial(mesh->materials.data[0]);
            auto& g = groups[mat];
            uint32_t n = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
            for (uint32_t k = 0; k < n * 3; k++) {
                uint32_t ix = tri[mirrored ? (k - k % 3) + (2 - k % 3) : k];
                Vertex v;
                ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
                v.position = vec3(toWorld * vec4((float)p.x, (float)p.y, (float)p.z, 1.0f));
                if (mesh->vertex_normal.exists) {
                    ufbx_vec3 nn = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
                    vec3 nv = normalMat * vec3((float)nn.x, (float)nn.y, (float)nn.z);
                    v.normal = glm::length(nv) > 1e-8f ? glm::normalize(nv) : vec3(0, 1, 0);
                }
                if (mesh->vertex_uv.exists) {
                    ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix);
                    v.uv = { (float)uv.x, 1.0f - (float)uv.y };   // FBX UVs start bottom-left; our images top-left
                }
                if (skin) {
                    uint32_t vi = mesh->vertex_indices.data[ix];
                    if (vi < skin->vertices.count) {
                        const ufbx_skin_vertex& sv = skin->vertices.data[vi];
                        float total = 0.0f;
                        for (uint32_t w = 0; w < sv.num_weights && w < 4; w++) {
                            const ufbx_skin_weight& sw = skin->weights.data[sv.weight_begin + w];
                            v.joints[w] = sw.cluster_index < clusterJoint.size() ? clusterJoint[sw.cluster_index] : 0;
                            v.weights[w] = (float)sw.weight;
                            total += (float)sw.weight;
                        }
                        if (total > 1e-6f) v.weights /= total;
                    }
                } else if (rigidJoint >= 0) {
                    // Rigidly attached to a bone: skin it with one full weight, in bind space
                    v.joints = ivec4(rigidJoint, 0, 0, 0);
                    v.weights = vec4(1, 0, 0, 0);
                }
                g.second.push_back((uint32_t)g.first.size());
                g.first.push_back(v);
            }
        }
        for (auto& [mat, g] : groups) {
            if (g.first.empty()) continue;
            if (!mesh->vertex_normal.exists) Mesh::ComputeNormals(g.first, g.second);
            ModelImport::Weld(g.first, g.second);
            SubMesh sm;
            sm.mesh = MakeRef<Mesh>(g.first, g.second);
            sm.material = mat;
            sm.node = Str(node->name);
            sm.nodeWorld = ToMat4(node->node_to_world);
            model->bounds.Expand(sm.mesh->Bounds());
            model->meshes.push_back(sm);
        }
    }

    // ---- Animations: every take, baked to keyframes on the joints
    if (model->skeleton) {
        for (size_t ai = 0; ai < scene->anim_stacks.count; ai++) {
            const ufbx_anim_stack* stack = scene->anim_stacks.data[ai];
            ufbx_bake_opts bo{};
            bo.trim_start_time = true;
            bo.resample_rate = 30.0;
            bo.minimum_sample_rate = 30.0;
            ufbx_error berr;
            ufbx_baked_anim* bake = ufbx_bake_anim(scene, stack->anim, &bo, &berr);
            if (!bake) {
                EU_WARN("FBX {}: could not bake take '{}': {}", path, Str(stack->name), Str(berr.description));
                continue;
            }
            auto clip = MakeRef<AnimationClip>();
            clip->name = ModelImport::CleanClipName(Str(stack->name), path, scene->anim_stacks.count);
            clip->duration = (float)bake->playback_duration;
            for (size_t bi = 0; bi < bake->nodes.count; bi++) {
                const ufbx_baked_node& bn = bake->nodes.data[bi];
                const ufbx_node* n = scene->nodes.data[bn.typed_id];
                auto it = jointIndex.find(n);
                if (it == jointIndex.end() || n->is_root) continue;
                JointChannel ch;
                ch.joint = it->second;
                for (size_t k = 0; k < bn.translation_keys.count; k++) {
                    const auto& key = bn.translation_keys.data[k];
                    ch.translation.times.push_back((float)key.time);
                    ch.translation.values.push_back(vec3((float)key.value.x, (float)key.value.y, (float)key.value.z));
                }
                for (size_t k = 0; k < bn.rotation_keys.count; k++) {
                    const auto& key = bn.rotation_keys.data[k];
                    ch.rotation.times.push_back((float)key.time);
                    ch.rotation.values.push_back(quat((float)key.value.w, (float)key.value.x, (float)key.value.y, (float)key.value.z));
                }
                for (size_t k = 0; k < bn.scale_keys.count; k++) {
                    const auto& key = bn.scale_keys.data[k];
                    ch.scale.times.push_back((float)key.time);
                    ch.scale.values.push_back(vec3((float)key.value.x, (float)key.value.y, (float)key.value.z));
                }
                if (!ch.translation.times.empty()) clip->duration = std::max(clip->duration, ch.translation.times.back());
                if (!ch.rotation.times.empty()) clip->duration = std::max(clip->duration, ch.rotation.times.back());
                clip->channels.push_back(std::move(ch));
            }
            ufbx_free_baked_anim(bake);
            // Exporters often add an empty "take" holding the bind pose; skip those
            if (!clip->channels.empty() && clip->duration > 0.04f) model->clips.push_back(clip);
        }
    }

    ufbx_free_scene(scene);
    EU_INFO("Loaded {} ({} meshes, {} materials, {} joints, {} clips)", fs::path(path).filename().string(), model->meshes.size(),
            model->materials.size(), model->skeleton ? model->skeleton->joints.size() : 0, model->clips.size());
    return model;
}

} // namespace eu::ModelLoader
