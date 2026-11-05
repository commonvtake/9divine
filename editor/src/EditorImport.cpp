// Importing files dragged in from Explorer / Finder, and the asset inspector (model info and
// import settings) shown when a model is clicked in the Assets panel.
#include "EditorApp.h"
#include <euphoria/assets/AssetManager.h>
#include <euphoria/assets/BlenderBridge.h>
#include <euphoria/assets/TextureFinder.h>
#include <euphoria/animation/Retarget.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <fstream>
#include <regex>
#include <set>

namespace eu::editor {

namespace {

std::string Lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// "Barrel" -> "Barrel (2)" when the name is taken
fs::path Unique(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return p;
    for (int i = 2;; i++) {
        fs::path c = p.parent_path() / (p.stem().string() + " (" + std::to_string(i) + ")" + p.extension().string());
        if (!fs::exists(c, ec)) return c;
    }
}

bool InsideProject(const fs::path& p) {
    std::string rel = fs::path(p).lexically_normal().lexically_relative(Project::Root().lexically_normal()).string();
    return !rel.empty() && rel.rfind("..", 0) != 0 && !fs::path(rel).is_absolute();
}

// Files a model needs beside it: glTF buffers and images, OBJ material libraries and their
// images, and every texture the loaded model actually used (FBX paths are often stale, so the
// textures are copied next to the model where the finder will look first).
std::set<std::string> Dependencies(const fs::path& model, const Model* loaded) {
    std::set<std::string> deps;
    std::string ext = Lower(model.extension().string());
    fs::path dir = model.parent_path();
    std::error_code ec;
    if (ext == ".gltf") {
        if (auto text = FileSystem::ReadText(model)) {
            try {
                json j = json::parse(*text);
                for (const char* key : { "buffers", "images" })
                    for (auto& b : j.value(key, json::array())) {
                        std::string uri = b.value("uri", "");
                        if (uri.empty() || uri.rfind("data:", 0) == 0) continue;
                        if (fs::exists(dir / uri, ec)) deps.insert((dir / uri).string());
                        else if (std::string f = TextureFinder::Find(uri, dir); !f.empty()) deps.insert(f);
                    }
            } catch (...) {}
        }
    } else if (ext == ".obj") {
        std::ifstream in(model);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("mtllib ", 0) != 0) continue;
            fs::path mtl = dir / line.substr(7);
            if (!fs::exists(mtl, ec)) continue;
            deps.insert(mtl.string());
            std::ifstream min(mtl);
            std::string ml;
            std::regex mapLine(R"(^\s*(map_\w+|bump|norm|disp)\s+(?:-\S+\s+\S+\s+)*(.+?)\s*$)");
            std::smatch m;
            while (std::getline(min, ml))
                if (std::regex_match(ml, m, mapLine))
                    if (std::string f = TextureFinder::Find(m[2].str(), dir); !f.empty()) deps.insert(f);
        }
    }
    if (loaded)
        for (auto& md : loaded->materials)
            for (auto& f : md.sourceFiles) deps.insert(f);
    deps.erase(model.string());
    return deps;
}

} // namespace

void EditorApp::OnFilesDropped(const std::vector<std::string>& paths) {
    m_PendingDrops.insert(m_PendingDrops.end(), paths.begin(), paths.end());
}

void EditorApp::ProcessDroppedFiles() {
    if (m_PendingDrops.empty() || !m_ProjectOpen) return;
    std::vector<std::string> drops;
    drops.swap(m_PendingDrops);
    fs::path root = Project::Root();
    fs::path target = m_AssetDir.empty() ? root : m_AssetDir;
    std::error_code ec;
    std::vector<std::string> models, report;
    int files = 0;

    for (auto& d : drops) {
        fs::path src = fs::path(d).lexically_normal();
        if (!fs::exists(src, ec)) continue;
        if (InsideProject(src)) {                      // already part of the project: just use it
            if (AssetManager::IsModelFile(src.string())) models.push_back(Project::MakeRelative(src));
            continue;
        }
        if (fs::is_directory(src, ec)) {
            // A whole pack: keep its layout (models find their Textures folder the same way)
            fs::path dst = Unique((target == root ? root / "Models" : target) / src.filename());
            fs::create_directories(dst.parent_path(), ec);
            fs::copy(src, dst, fs::copy_options::recursive, ec);
            if (ec) { EU_ERROR("Import of {} failed: {}", src.string(), ec.message()); continue; }
            for (auto& f : fs::recursive_directory_iterator(dst, ec)) {
                if (!f.is_regular_file(ec)) continue;
                files++;
                if (AssetManager::IsModelFile(f.path().string()) && models.size() < 2) models.push_back(Project::MakeRelative(f.path()));
            }
            report.push_back(src.filename().string() + "/ -> " + Project::MakeRelative(dst));
            continue;
        }
        std::string name = src.filename().string();
        if (AssetManager::IsModelFile(name)) {
            // A model: its own folder, with everything it refers to
            fs::path folder = Unique((target == root ? root / "Models" : target) / src.stem());
            fs::create_directories(folder, ec);
            Ref<Model> loaded = BlenderBridge::Handles(name) ? nullptr : ModelLoader::Load(src.string());
            auto deps = Dependencies(src, loaded.get());
            fs::copy_file(src, folder / name, fs::copy_options::overwrite_existing, ec);
            files++;
            for (auto& dep : deps) {
                fs::path dp(dep);
                // Keep relative sub-paths that point inside the model's folder (buffers in a
                // subfolder); everything else lands beside the model
                std::string rel = dp.lexically_relative(src.parent_path()).string();
                fs::path out = (!rel.empty() && rel.rfind("..", 0) != 0) ? folder / rel : folder / dp.filename();
                fs::create_directories(out.parent_path(), ec);
                fs::copy_file(dp, out, fs::copy_options::skip_existing, ec);
                files++;
            }
            if (fs::exists(fs::path(src.string() + ".meta"), ec)) fs::copy_file(src.string() + ".meta", (folder / name).string() + ".meta", ec);
            models.push_back(Project::MakeRelative(folder / name));
            report.push_back(name + (deps.empty() ? "" : " + " + std::to_string(deps.size()) + " file(s) it uses") + " -> " + Project::MakeRelative(folder));
            if (BlenderBridge::Handles(name))
                report.push_back("  (if its textures aren't packed into the .blend, drop its whole folder instead)");
        } else {
            // Textures, sounds, anything else: sorted into the usual folders at the project root
            fs::path folder = target;
            if (target == root) {
                if (AssetManager::IsTextureFile(name)) folder = root / "Textures";
                else if (AssetManager::IsAudioFile(name)) folder = root / "Audio";
            }
            fs::create_directories(folder, ec);
            fs::path dst = Unique(folder / name);
            fs::copy_file(src, dst, ec);
            files++;
            report.push_back(name + " -> " + Project::MakeRelative(folder));
        }
    }
    TextureFinder::ClearCache();   // new files on disk
    for (auto& r : report) EU_INFO("Imported {}", r);
    if (files > 0) EU_INFO("Imported {} file(s)", files);

    // One model dropped straight onto the viewport: place it where the mouse is
    if (models.size() == 1 && m_ViewportHovered && m_State == PlayState::Edit) {
        vec2 m(ImGui::GetMousePos().x - ImGui::GetMainViewport()->Pos.x - m_ViewportMin.x,
               ImGui::GetMousePos().y - ImGui::GetMainViewport()->Pos.y - m_ViewportMin.y);
        vec2 ndc(m.x / m_ViewportSize.x * 2.0f - 1.0f, 1.0f - m.y / m_ViewportSize.y * 2.0f);
        Ray ray = m_Camera.ScreenRay(ndc, m_ViewportSize.x / m_ViewportSize.y);
        RaycastHit hit;
        vec3 at = ActiveScene()->Physics().Raycast(ray, 200.0f, hit) ? hit.point : ray.origin + ray.direction * 10.0f;
        CreateFromAsset(models[0], at);
    }
    if (!models.empty()) {
        // Show it in the Assets panel and the inspector
        m_AssetDir = Project::Resolve(models[0]).parent_path();
        m_InspectedAsset = models[0];
        m_ShowAssets = true;
    }
}

// ------------------------------------------------------------------ asset inspector
void EditorApp::DrawAssetInspector() {
    const std::string path = m_InspectedAsset;
    ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
    ImGui::TextDisabled("%s", path.c_str());
    ImGui::Separator();
    if (!AssetManager::IsModelFile(path)) {
        if (AssetManager::IsTextureFile(path))
            if (auto tex = AssetManager::GetTexture(path)) {
                ImGui::Text("%d x %d", tex->Width(), tex->Height());
                float w = std::min(ImGui::GetContentRegionAvail().x, 256.0f);
                ImGui::Image((ImTextureID)(uintptr_t)tex->Id(), ImVec2(w, w * tex->Height() / std::max(1, tex->Width())), ImVec2(0, 1), ImVec2(1, 0));
            }
        return;
    }

    // Blender formats: conversion status and where Blender is
    std::string base = path.substr(0, path.find('#'));
    if (BlenderBridge::Handles(base)) {
        fs::path glb;
        std::string msg;
        auto status = BlenderBridge::Request(Project::Resolve(base), glb, &msg);
        const char* names[] = { "Converted", "Converting with Blender...", "Conversion failed", "Blender not found" };
        ImVec4 col = status == BlenderBridge::Status::Ready ? ImVec4(0.5f, 0.9f, 0.5f, 1) : status == BlenderBridge::Status::Converting
                         ? ImVec4(0.9f, 0.8f, 0.4f, 1) : ImVec4(1, 0.45f, 0.4f, 1);
        ImGui::TextColored(col, "%s", names[(int)status]);
        if (!msg.empty() && status != BlenderBridge::Status::Ready) ImGui::TextWrapped("%s", msg.c_str());
        static std::string blenderPath;
        if (blenderPath.empty()) blenderPath = BlenderBridge::BlenderPath();
        ImGui::SetNextItemWidth(-60);
        ImGui::InputTextWithHint("##blender", "path to blender.exe", &blenderPath);
        ImGui::SameLine();
        if (ImGui::Button("Use")) BlenderBridge::SetBlenderPath(blenderPath);
        ImGui::Separator();
    }

    Ref<Model> model = AssetManager::GetModel(path);
    if (!model) {
        ImGui::TextDisabled("Not loaded yet");
        return;
    }
    vec3 size = model->bounds.Valid() ? model->bounds.max - model->bounds.min : vec3(0.0f);
    size_t verts = 0, tris = 0;
    for (auto& sm : model->meshes) { verts += sm.mesh->Vertices().size(); tris += sm.mesh->Indices().size() / 3; }
    ImGui::Text("%s", model->sourceFormat.c_str());
    ImGui::Text("%.2f x %.2f x %.2f m", size.x, size.y, size.z);
    ImGui::Text("%zu meshes, %zu vertices, %zu triangles", model->meshes.size(), verts, tris);

    // Import settings (<file>.meta)
    if (ImGui::CollapsingHeader("Import settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (m_ImportSettingsFor != base) {
            m_ImportSettings = AssetManager::LoadImportSettings(base);
            m_ImportSettingsFor = base;
        }
        auto& s = m_ImportSettings;
        ImGui::DragFloat("Scale", &s.scale, 0.001f, 0.0001f, 1000.0f, "%.4f");
        ImGui::SameLine();
        if (ImGui::SmallButton("cm")) s.scale = 0.01f;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0.01: for files exported in centimetres without unit conversion");
        ImGui::DragFloat3("Rotation", &s.rotation.x, 1.0f, -360.0f, 360.0f, "%.0f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Z-up")) s.rotation = vec3(-90, 0, 0);
        ImGui::Checkbox("Turn characters to face forward", &s.faceForward);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Humanoid characters facing -Z are turned to face +Z, like glTF models.\nTurn off if a character ends up backwards.");
        if (ImGui::Button("Apply")) AssetManager::SaveImportSettings(base, s);
        ImGui::SameLine();
        ImGui::TextDisabled("saved as %s.meta", fs::path(base).filename().string().c_str());
    }

    // Objects: drag one out to place just that piece of a kit
    auto objects = model->ObjectNames();
    if (objects.size() > 1 && path.find('#') == std::string::npos &&
        ImGui::CollapsingHeader(("Objects (" + std::to_string(objects.size()) + ")").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Drag an object into the viewport to place just that piece.");
        static std::string filter;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##objfilter", "Filter", &filter);
        ImGui::BeginChild("##objects", ImVec2(0, std::min(220.0f, objects.size() * ImGui::GetTextLineHeightWithSpacing() + 8)), true);
        for (auto& o : objects) {
            if (!filter.empty() && Lower(o).find(Lower(filter)) == std::string::npos) continue;
            ImGui::Selectable(o.c_str());
            if (ImGui::BeginDragDropSource()) {
                std::string payload = base + "#" + o;
                ImGui::SetDragDropPayload("EU_ASSET", payload.data(), payload.size());
                ImGui::Text("%s", o.c_str());
                ImGui::EndDragDropSource();
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("Place every object separately") && m_State == PlayState::Edit) {
            // A parent with one child per object, where the file had them
            auto scene = ActiveScene();
            Entity parent = scene->CreateEntity(fs::path(base).stem().string());
            parent.Transform().position = m_Camera.pivot;
            for (auto& o : objects) {
                Entity c = scene->CreateEntity(o);
                c.Transform().parent = parent.GetUUID();
                for (auto& sm : model->meshes)
                    if (sm.node == o) { c.Transform().position = vec3(sm.nodeWorld[3]); break; }
                c.Add<MeshRendererComponent>().mesh = base + "#" + o;
            }
            Select(parent);
            Commit();
        }
    }

    if (ImGui::CollapsingHeader("Materials")) {
        for (auto& md : model->materials) {
            ImGui::TextUnformatted(md.name.empty() ? "(unnamed)" : md.name.c_str());
            ImGui::Indent();
            for (auto& n : md.textureNotes) {
                bool missing = n.find(": missing") != std::string::npos;
                if (missing) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", n.c_str());
                else ImGui::TextDisabled("%s", n.c_str());
            }
            if (md.textureNotes.empty()) ImGui::TextDisabled("no textures");
            ImGui::Unindent();
        }
    }

    if (model->skeleton && ImGui::CollapsingHeader("Skeleton")) {
        auto human = ClassifyHumanoid(*model->skeleton);
        int found = 0;
        for (auto h : human) if (h != HumanBone::None) found++;
        ImGui::Text("%zu joints, %d humanoid bones recognised", model->skeleton->joints.size(), found);
        if (found >= 14) ImGui::TextDisabled("Humanoid: can play animations from other humanoid rigs.");
        ImGui::BeginChild("##joints", ImVec2(0, 160), true);
        for (size_t i = 0; i < model->skeleton->joints.size(); i++) {
            const Joint& j = model->skeleton->joints[i];
            int depth = 0;
            for (int p = j.parent; p >= 0; p = model->skeleton->joints[p].parent) depth++;
            ImGui::Text("%*s%s%s%s", depth * 2, "", j.name.c_str(), human[i] != HumanBone::None ? "  - " : "",
                        human[i] != HumanBone::None ? HumanBoneName(human[i]) : "");
        }
        ImGui::EndChild();
    }

    if (!model->clips.empty() && ImGui::CollapsingHeader(("Animations (" + std::to_string(model->clips.size()) + ")").c_str())) {
        ImGui::TextDisabled("Drag this file onto a character's Animator > Animations from.");
        for (auto& c : model->clips) ImGui::Text("%s  %.1fs", c->name.c_str(), c->duration);
    }
}

} // namespace eu::editor
