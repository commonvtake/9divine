#include "EditorApp.h"
#include <nd/scene/SceneSerializer.h>
#include <nd/assets/AssetManager.h>
#include "DemoBuilder.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace nd::editor {

void EditorApp::SetupDefaultLayout(unsigned int dockspaceId) {
    ImGuiID id = dockspaceId;
    if (ImGui::DockBuilderGetNode(id) && ImGui::DockBuilderGetNode(id)->IsSplitNode()) return;   // layout loaded from ini
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id, ImGui::GetMainViewport()->WorkSize);
    ImGuiID center = id;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
    ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.4f, nullptr, &left);
    ImGui::DockBuilderDockWindow("Viewport", center);
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Scene Settings", leftBottom);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Assets", bottom);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderDockWindow("Profiler", bottom);
    ImGui::DockBuilderDockWindow("Input Map", bottom);
    ImGui::DockBuilderDockWindow("Multiplayer", leftBottom);
    ImGui::DockBuilderDockWindow("Project Settings", right);
    ImGui::DockBuilderFinish(id);
}

// ------------------------------------------------------------------ menu
void EditorApp::DrawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    bool playing = m_State == PlayState::Play;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Project...", "Ctrl+Shift+N")) BeginNewProject();
        if (ImGui::MenuItem("Open Project...")) BeginOpenProject();
        if (ImGui::BeginMenu("Recent Projects")) {
            int shown = 0;
            for (auto& r : m_Recent.List()) {
                if (fs::path(r.path) == m_ProjectDir || !IsProjectFolder(r.path)) continue;
                if (ImGui::MenuItem(r.name.c_str(), r.path.c_str())) { fs::path dir = r.path; RequestProjectChange([this, dir] { OpenProject(dir); }); }
                if (++shown >= 10) break;
            }
            if (shown == 0) ImGui::TextDisabled("No other recent projects");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Close Project")) RequestProjectChange([this] { CloseProject(); });
        ImGui::Separator();
        if (ImGui::MenuItem("New Scene", "Ctrl+N")) NewScene();
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) m_OpenScenePopup = true;
        if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !playing)) SaveScene();
        if (ImGui::MenuItem("Save Scene As...", nullptr, false, !playing)) { m_SaveAsPath = m_EditorScene->path.empty() ? "Scenes/NewScene.scene" : m_EditorScene->path; m_SaveAsPopup = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("Build Game...", nullptr, false, !playing)) { m_BuildPopup = true; m_BuildLog.clear(); }
        if (ImGui::MenuItem("Open Project Folder")) Platform::OpenInFileBrowser(Project::Root());
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) RequestProjectChange([this] { Close(); });
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !playing && !m_UndoStack.empty())) Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !playing && !m_RedoStack.empty())) Redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, (bool)Selected())) DuplicateSelected();
        if (ImGui::MenuItem("Delete", "Del", false, (bool)Selected())) DeleteSelected();
        ImGui::Separator();
        if (ImGui::MenuItem("Play / Stop", "Ctrl+P")) { if (playing) StopPlay(); else StartPlay(); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("GameObject")) {
        DrawCreateMenu(Entity());
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchy);
        ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
        ImGui::MenuItem("Assets", nullptr, &m_ShowAssets);
        ImGui::MenuItem("Console", nullptr, &m_ShowConsole);
        ImGui::MenuItem("Profiler", nullptr, &m_ShowProfiler);
        ImGui::MenuItem("Scene Settings", nullptr, &m_ShowSceneSettings);
        ImGui::MenuItem("Input Map", nullptr, &m_ShowInput);
        ImGui::MenuItem("Multiplayer", nullptr, &m_ShowNetwork);
        ImGui::MenuItem("Project Settings", nullptr, &m_ShowProject);
        ImGui::Separator();
        ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemo);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Reload Shaders")) Renderer::ReloadShaders();
        if (ImGui::MenuItem("Reload All Assets")) AssetManager::ClearCache();
        if (ImGui::MenuItem("Bake Navigation", nullptr, false, !playing)) ActiveScene()->Nav().Bake();
        ImGui::Separator();
        std::error_code sameEc;
        bool isSample = fs::equivalent(Project::Root(), fs::path(ND_SOURCE_DIR) / "projects" / "Demo", sameEc) ||
                        Project::Info().name == "Ember Isle";
        if (ImGui::MenuItem("Rebuild Demo Scenes", nullptr, false, !playing && isSample)) {
            demo::BuildDemoProject(Project::Root());
            OpenScene(Project::Info().startScene);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About 9divine")) m_AboutPopup = true;
        ImGui::EndMenu();
    }
    // Status on the right
    std::string status = (m_EditorScene ? m_EditorScene->name : "") + (m_Dirty ? " *" : "") + (playing ? "   PLAYING" : "");
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(status.c_str()).x - 20);
    ImGui::TextDisabled("%s", status.c_str());
    ImGui::EndMainMenuBar();
}

void EditorApp::DrawCreateMenu(Entity parent) {
    auto scene = ActiveScene();
    auto made = [&](Entity e) { if (parent && e) { e.Transform().parent = parent.GetUUID(); e.Transform().position = vec3(0.0f); } Commit(); };
    if (ImGui::MenuItem("Empty")) made(CreateEntity("Entity"));
    if (ImGui::BeginMenu("3D Object")) {
        for (auto& n : Primitives::Names()) if (ImGui::MenuItem(n.c_str())) made(CreatePrimitive(n));
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Light")) {
        const char* names[] = { "Directional Light", "Point Light", "Spot Light" };
        for (int i = 0; i < 3; i++)
            if (ImGui::MenuItem(names[i])) {
                Entity e = CreateEntity(names[i]);
                auto& l = e.Add<LightComponent>();
                l.type = (LightType)i;
                e.Transform().position.y += 3.0f;
                if (i != 1) e.Transform().SetEulerDegrees({ -50, 30, 0 });
                made(e);
            }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Camera")) { Entity e = CreateEntity("Camera"); e.Add<CameraComponent>().primary = false; e.Transform().position.y += 2.0f; made(e); }
    if (ImGui::BeginMenu("Environment")) {
        if (ImGui::MenuItem("Terrain")) { Entity e = CreateEntity("Terrain"); e.Transform().position = vec3(0.0f); e.Add<TerrainComponent>(); made(e); }
        if (ImGui::MenuItem("Water")) { Entity e = CreateEntity("Water"); e.Transform().position = vec3(0, 0.5f, 0); e.Add<WaterComponent>(); made(e); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Effects")) {
        struct P { const char* name; void (*fn)(ParticleEmitterComponent&); };
        P presets[] = { { "Fire", ParticlePresets::Fire }, { "Smoke", ParticlePresets::Smoke }, { "Sparks", ParticlePresets::Sparks },
                        { "Magic", ParticlePresets::Magic }, { "Rain", ParticlePresets::Rain }, { "Dust", ParticlePresets::Dust } };
        for (auto& p : presets)
            if (ImGui::MenuItem(p.name)) { Entity e = CreateEntity(p.name); p.fn(e.Add<ParticleEmitterComponent>()); made(e); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("UI")) {
        const char* names[] = { "Panel", "Text", "Button", "Progress Bar", "Image" };
        for (int i = 0; i < 5; i++)
            if (ImGui::MenuItem(names[i])) {
                Entity e = CreateEntity(names[i]);
                auto& u = e.Add<UIElementComponent>();
                u.type = (UIType)i;
                if (u.type == UIType::Text) { u.text = "New Text"; u.color = vec4(0.0f); }
                if (u.type == UIType::Button) { u.text = "Button"; u.size = { 260, 64 }; }
                if (u.type == UIType::ProgressBar) { u.color = { 0.3f, 0.8f, 0.5f, 1 }; u.size = { 360, 26 }; }
                if (u.type == UIType::Image) u.color = vec4(1.0f);
                made(e);
            }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Audio Source")) { Entity e = CreateEntity("Audio Source"); e.Add<AudioSourceComponent>(); made(e); }
    (void)scene;
}

// ------------------------------------------------------------------ hierarchy
void EditorApp::DrawEntityNode(Entity e) {
    auto scene = ActiveScene();
    auto children = scene->GetChildren(e);
    auto& tag = e.Get<TagComponent>();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (Selected() == e) flags |= ImGuiTreeNodeFlags_Selected;
    if (!tag.active) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    bool open = ImGui::TreeNodeEx((void*)(uintptr_t)(uint64_t)e.GetUUID(), flags, "%s", tag.name.c_str());
    if (!tag.active) ImGui::PopStyleColor();
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) Select(e);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) m_Camera.Focus(scene->WorldPosition(e), 6.0f);

    // Drag to re-parent
    if (ImGui::BeginDragDropSource()) {
        uint64_t id = e.GetUUID();
        ImGui::SetDragDropPayload("ND_ENTITY", &id, sizeof(id));
        ImGui::Text("%s", tag.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ND_ENTITY")) {
            Entity child = scene->FindByUUID(*(const uint64_t*)p->Data);
            if (child && child != e) { scene->SetParent(child, e); Commit(); }
        }
        ImGui::EndDragDropTarget();
    }
    bool deleted = false;
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::BeginMenu("Create Child")) { DrawCreateMenu(e); ImGui::EndMenu(); }
        if (ImGui::MenuItem("Duplicate")) { Select(e); DuplicateSelected(); }
        if (ImGui::MenuItem("Unparent", nullptr, false, (bool)scene->GetParent(e))) { scene->SetParent(e, Entity()); Commit(); }
        if (ImGui::MenuItem(tag.active ? "Deactivate" : "Activate")) { tag.active = !tag.active; Commit(); }
        if (ImGui::MenuItem("Delete")) deleted = true;
        ImGui::EndPopup();
    }
    if (open) {
        for (auto& c : children) DrawEntityNode(c);
        ImGui::TreePop();
    }
    if (deleted) {
        if (Selected() == e) m_SelectedID = 0;
        scene->DestroyEntity(e);
        Commit();
    }
}

void EditorApp::DrawHierarchy() {
    ImGui::Begin("Hierarchy", &m_ShowHierarchy);
    auto scene = ActiveScene();
    if (!scene) { ImGui::End(); return; }
    if (ImGui::Button("+ Create")) ImGui::OpenPopup("CreateEntity");
    if (ImGui::BeginPopup("CreateEntity")) { DrawCreateMenu(Entity()); ImGui::EndPopup(); }
    ImGui::SameLine();
    static std::string filter;
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Search...", &filter);
    ImGui::Separator();

    ImGui::BeginChild("##tree");
    if (filter.empty()) {
        auto roots = scene->RootEntities();
        for (auto& e : roots) DrawEntityNode(e);
    } else {
        std::string f = filter;
        for (auto& c : f) c = (char)std::tolower((unsigned char)c);
        for (EntityId id : scene->Reg().AllEntities()) {
            Entity e(id, scene.get());
            std::string n = e.Name();
            for (auto& c : n) c = (char)std::tolower((unsigned char)c);
            if (n.find(f) == std::string::npos) continue;
            if (ImGui::Selectable((e.Name() + "##" + std::to_string((uint64_t)e.GetUUID())).c_str(), Selected() == e)) Select(e);
        }
    }
    // Drop on empty space = unparent
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ND_ENTITY")) {
            if (Entity child = scene->FindByUUID(*(const uint64_t*)p->Data)) { scene->SetParent(child, Entity()); Commit(); }
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ND_ASSET")) {
            CreateFromAsset(std::string((const char*)p->Data, p->DataSize), SpawnPoint());
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemClicked()) Select(Entity());
    if (ImGui::BeginPopupContextItem("HierarchyBlank")) { DrawCreateMenu(Entity()); ImGui::EndPopup(); }
    ImGui::EndChild();
    ImGui::End();
}

void EditorApp::DrawInspector() {
    ImGui::Begin("Inspector", &m_ShowInspector);
    Entity e = Selected();
    if (e) DrawComponents(e);
    else ImGui::TextDisabled("Select an entity in the Hierarchy or Viewport.");
    ImGui::End();
}

// ------------------------------------------------------------------ assets
void EditorApp::DrawAssetBrowser() {
    ImGui::Begin("Assets", &m_ShowAssets);
    fs::path root = Project::Root();
    if (m_AssetDir.empty() || !fs::exists(m_AssetDir)) m_AssetDir = root;
    if (m_AssetDir != root) {
        if (ImGui::Button("<- Back")) m_AssetDir = m_AssetDir.parent_path();
        ImGui::SameLine();
    }
    ImGui::TextDisabled("%s", Project::MakeRelative(m_AssetDir).c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 230);
    ImGui::SetNextItemWidth(140);
    ImGui::InputTextWithHint("##assetfilter", "Filter", &m_AssetFilter);
    ImGui::SameLine();
    if (ImGui::Button("Open")) Platform::OpenInFileBrowser(m_AssetDir);
    ImGui::Separator();

    std::vector<fs::directory_entry> entries;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(m_AssetDir, ec)) {
        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (!m_AssetFilter.empty() && name.find(m_AssetFilter) == std::string::npos) continue;
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) {
        if (a.is_directory() != b.is_directory()) return a.is_directory();
        return a.path().filename() < b.path().filename();
    });

    float cell = 110.0f;
    int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cell));
    if (ImGui::BeginTable("##assets", columns)) {
        for (auto& entry : entries) {
            ImGui::TableNextColumn();
            std::string name = entry.path().filename().string();
            std::string rel = Project::MakeRelative(entry.path());
            bool dir = entry.is_directory();
            const char* kind = dir ? "DIR" : AssetManager::IsModelFile(rel) ? "MODEL" : AssetManager::IsTextureFile(rel) ? "IMAGE"
                             : AssetManager::IsAudioFile(rel) ? "AUDIO" : AssetManager::IsSceneFile(rel) ? "SCENE" : "FILE";
            ImVec4 col = dir ? ImVec4(0.85f, 0.7f, 0.3f, 1) : AssetManager::IsSceneFile(rel) ? ImVec4(0.4f, 0.8f, 0.6f, 1)
                       : AssetManager::IsModelFile(rel) ? ImVec4(0.5f, 0.7f, 1.0f, 1) : AssetManager::IsAudioFile(rel) ? ImVec4(0.9f, 0.55f, 0.8f, 1)
                       : ImVec4(0.75f, 0.75f, 0.75f, 1);
            ImGui::PushID(rel.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            bool thumb = false;
            if (AssetManager::IsTextureFile(rel)) {
                if (auto tex = AssetManager::GetTexture(rel)) {
                    ImGui::ImageButton("##thumb", (ImTextureID)(uintptr_t)tex->Id(), ImVec2(cell - 22, 64), ImVec2(0, 1), ImVec2(1, 0));
                    thumb = true;
                }
            }
            if (!thumb) ImGui::Button(kind, ImVec2(cell - 14, 70));
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (dir) m_AssetDir = entry.path();
                else if (AssetManager::IsSceneFile(rel)) OpenScene(rel);
                else if (AssetManager::IsAudioFile(rel)) Audio::PlayOneShot(rel);
            }
            if (!dir && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ND_ASSET", rel.data(), rel.size());
                ImGui::Text("%s", rel.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", rel.c_str(), dir ? "Double-click to open" :
                AssetManager::IsModelFile(rel) ? "Drag into the viewport to place" : AssetManager::IsSceneFile(rel) ? "Double-click to open" :
                AssetManager::IsAudioFile(rel) ? "Double-click to preview, drag onto an Audio Source" : "Drag onto a component field");
            ImGui::TextWrapped("%s", name.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// ------------------------------------------------------------------ console / profiler
void EditorApp::DrawConsole() {
    ImGui::Begin("Console", &m_ShowConsole);
    if (ImGui::Button("Clear")) Log::Clear();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_ConsoleAutoScroll);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    const char* levels[] = { "All", "Info+", "Warnings+", "Errors" };
    ImGui::Combo("##level", &m_ConsoleLevel, levels, 4);
    ImGui::Separator();
    ImGui::BeginChild("##log", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lock(Log::Mutex());
        for (auto& e : Log::Entries()) {
            if ((int)e.level < m_ConsoleLevel) continue;
            ImVec4 c = e.level == LogLevel::Error ? ImVec4(1.0f, 0.42f, 0.4f, 1) : e.level == LogLevel::Warn ? ImVec4(1.0f, 0.8f, 0.35f, 1)
                     : e.level == LogLevel::Trace ? ImVec4(0.6f, 0.6f, 0.6f, 1) : ImVec4(0.86f, 0.88f, 0.9f, 1);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1), "[%7.2f]", e.time);
            ImGui::SameLine();
            ImGui::TextColored(c, "%s", e.message.c_str());
        }
    }
    if (m_ConsoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

void EditorApp::DrawProfiler() {
    ImGui::Begin("Profiler", &m_ShowProfiler);
    auto& st = Profiler::Stats();
    ImGui::Text("%.1f FPS   %.2f ms/frame", st.fps, st.frameMs);
    ImGui::PlotLines("##frametimes", Profiler::FrameHistory(), Profiler::kHistory, Profiler::HistoryOffset(), "frame time (ms)", 0.0f, 33.3f,
                     ImVec2(ImGui::GetContentRegionAvail().x, 70));
    if (ImGui::BeginTable("##stats", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto row = [](const char* k, const std::string& v) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k); ImGui::TableNextColumn(); ImGui::Text("%s", v.c_str()); };
        row("Draw calls", std::to_string(st.drawCalls));
        row("Triangles", std::to_string(st.triangles));
        row("Entities", std::to_string(st.entities));
        row("Physics bodies", std::to_string(st.physicsBodies));
        row("Particles", std::to_string(st.particles));
        char mem[32];
        snprintf(mem, sizeof(mem), "%.1f MB", st.memoryBytes / (1024.0 * 1024.0));
        row("Process memory", mem);
        auto as = AssetManager::GetStats();
        row("Cached models / textures", std::to_string(as.models) + " / " + std::to_string(as.textures));
        ImGui::EndTable();
    }
    ImGui::SeparatorText("CPU time per system (avg ms)");
    float maxMs = 0.1f;
    for (auto& s : Profiler::Sections()) maxMs = std::max(maxMs, s.avgMs);
    for (auto& s : Profiler::Sections()) {
        ImGui::Text("%-12s", s.name.c_str());
        ImGui::SameLine(120);
        char label[32];
        snprintf(label, sizeof(label), "%.2f ms", s.avgMs);
        ImGui::ProgressBar(s.avgMs / maxMs, ImVec2(-1, 0), label);
    }
    ImGui::End();
}

// ------------------------------------------------------------------ scene settings
void EditorApp::DrawSceneSettings() {
    ImGui::Begin("Scene Settings", &m_ShowSceneSettings);
    auto scene = ActiveScene();
    if (!scene) { ImGui::End(); return; }
    auto& env = scene->Environment();
    auto mark = [&](bool changed) { if (changed) m_EditedThisInteraction = true; };
    mark(ImGui::InputText("Scene name", &scene->name));
    if (ImGui::CollapsingHeader("Sky & Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        mark(ImGui::ColorEdit3("Sky top", glm::value_ptr(env.skyTop)));
        mark(ImGui::ColorEdit3("Horizon", glm::value_ptr(env.skyHorizon)));
        mark(ImGui::ColorEdit3("Ground", glm::value_ptr(env.groundColor)));
        mark(ImGui::SliderFloat("Ambient", &env.ambientIntensity, 0.0f, 2.0f));
        mark(ImGui::SliderFloat("Exposure", &env.exposure, 0.1f, 4.0f));
        mark(ImGui::Checkbox("Fog", &env.fog));
        if (env.fog) {
            mark(ImGui::ColorEdit3("Fog color", glm::value_ptr(env.fogColor)));
            mark(ImGui::DragFloat("Fog density", &env.fogDensity, 0.0002f, 0.0f, 0.1f, "%.4f"));
        }
    }
    if (ImGui::CollapsingHeader("Physics")) mark(ImGui::DragFloat3("Gravity", glm::value_ptr(env.gravity), 0.1f));
    if (ImGui::CollapsingHeader("Navigation")) {
        auto& ns = scene->Nav().settings;
        mark(ImGui::DragFloat("Cell size", &ns.cellSize, 0.05f, 0.1f, 4.0f));
        mark(ImGui::DragFloat("Agent radius", &ns.agentRadius, 0.05f, 0.05f, 5.0f));
        mark(ImGui::DragFloat("Max slope", &ns.maxSlope, 0.5f, 0.0f, 89.0f));
        mark(ImGui::DragFloat("Max step", &ns.maxStep, 0.05f, 0.0f, 5.0f));
        mark(ImGui::DragFloat("Min walkable height", &ns.minWalkableHeight, 0.1f));
        if (ImGui::Button("Bake")) { scene->Nav().Bake(); m_RenderOptions.showNavGrid = true; }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) scene->Nav().Clear();
        ImGui::SameLine();
        ImGui::Checkbox("Show", &m_RenderOptions.showNavGrid);
        if (scene->Nav().IsBaked()) ImGui::TextDisabled("%d x %d cells", scene->Nav().Width(), scene->Nav().Depth());
    }
    if (ImGui::CollapsingHeader("Rendering")) {
        auto& rs = Renderer::Settings();
        ImGui::Checkbox("Shadows", &rs.shadows);
        ImGui::SliderFloat("Shadow distance", &rs.shadowDistance, 10.0f, 250.0f);
        const char* sizes[] = { "1024", "2048", "4096" };
        int idx = rs.shadowMapSize >= 4096 ? 2 : rs.shadowMapSize >= 2048 ? 1 : 0;
        if (ImGui::Combo("Shadow map", &idx, sizes, 3)) rs.shadowMapSize = 1024 << idx;
        ImGui::Checkbox("Grass", &rs.grass);
        ImGui::SliderFloat("Grass distance", &rs.grassDistance, 10.0f, 200.0f);
    }
    if (ImGui::CollapsingHeader("Audio")) {
        float m = Audio::GetBusVolume(AudioBus::Master), mu = Audio::GetBusVolume(AudioBus::Music), sf = Audio::GetBusVolume(AudioBus::SFX);
        if (ImGui::SliderFloat("Master", &m, 0, 1)) Audio::SetBusVolume(AudioBus::Master, m);
        if (ImGui::SliderFloat("Music", &mu, 0, 1)) Audio::SetBusVolume(AudioBus::Music, mu);
        if (ImGui::SliderFloat("Effects", &sf, 0, 1)) Audio::SetBusVolume(AudioBus::SFX, sf);
        if (!Audio::Available()) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "No audio device found");
    }
    ImGui::End();
}

// ------------------------------------------------------------------ input map
void EditorApp::DrawInputSettings() {
    ImGui::Begin("Input Map", &m_ShowInput);
    auto& map = Input::Map();
    if (ImGui::Button("Save")) Input::SaveMap(Project::Info().inputMap);
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) map.SetDefaults();
    ImGui::SameLine();
    if (Input::IsRebinding()) ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "Press a key, mouse button or gamepad input... (Esc cancels)");
    else ImGui::TextDisabled("%s", Input::IsGamepadConnected() ? ("Gamepad: " + Input::GamepadName()).c_str() : "No gamepad connected");

    auto drawBindings = [&](const std::string& name, std::vector<InputBinding>& bindings, bool isAxis) {
        ImGui::PushID(name.c_str());
        for (size_t i = 0; i < bindings.size(); i++) {
            ImGui::PushID((int)i);
            if (ImGui::Button(Input::BindingName(bindings[i]).c_str(), ImVec2(150, 0))) Input::BeginRebind(name, isAxis, (int)i, bindings[i].scale);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click, then press the new input");
            if (isAxis) { ImGui::SameLine(); ImGui::SetNextItemWidth(70); ImGui::DragFloat("##scale", &bindings[i].scale, 0.05f); }
            ImGui::SameLine();
            bool remove = ImGui::SmallButton("x");
            ImGui::PopID();
            if (remove) { bindings.erase(bindings.begin() + i); break; }
            if (i + 1 < bindings.size() && !isAxis) ImGui::SameLine();
        }
        if (!isAxis) ImGui::SameLine();
        if (ImGui::SmallButton("+ Add")) Input::BeginRebind(name, isAxis, -1, 1.0f);
        ImGui::PopID();
    };
    if (ImGui::BeginTable("##actions", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("Bindings");
        ImGui::TableHeadersRow();
        for (auto& a : map.actions) {
            ImGui::TableNextColumn();
            ImGui::Text("%s", a.name.c_str());
            if (Input::GetAction(a.name)) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.3f, 1, 0.5f, 1), "*"); }
            ImGui::TableNextColumn();
            drawBindings(a.name, a.bindings, false);
        }
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Axes");
    for (auto& a : map.axes) {
        ImGui::Text("%s  (%.2f)", a.name.c_str(), Input::GetAxis(a.name));
        ImGui::Indent();
        drawBindings(a.name, a.bindings, true);
        ImGui::Unindent();
    }
    static std::string newName;
    ImGui::Separator();
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##newaction", "New action name", &newName);
    ImGui::SameLine();
    if (ImGui::Button("Add Action") && !newName.empty()) { map.actions.push_back({ newName, {} }); newName.clear(); }
    ImGui::SameLine();
    if (ImGui::Button("Add Axis") && !newName.empty()) { map.axes.push_back({ newName, {}, 0.15f }); newName.clear(); }
    ImGui::End();
}

// ------------------------------------------------------------------ network
void EditorApp::DrawNetworkPanel() {
    ImGui::Begin("Multiplayer", &m_ShowNetwork);
    ImGui::TextWrapped("Run the game twice (e.g. editor + exported build). One hosts, the other joins. Each peer gets a copy of the '%s' entity.", Network::playerPrefab.c_str());
    ImGui::Separator();
    ImGui::Text("Status: %s", Network::Status().c_str());
    if (Network::IsActive() && !Network::IsServer()) ImGui::Text("Ping: %.0f ms", Network::PingMs());
    bool playing = m_State == PlayState::Play;
    ImGui::InputText("Player prefab", &Network::playerPrefab);
    ImGui::InputInt("Port", &m_NetPort);
    ImGui::SliderFloat("Send rate (Hz)", &Network::sendRate, 5.0f, 60.0f);
    ImGui::BeginDisabled(!playing);
    if (!Network::IsActive()) {
        if (ImGui::Button("Host")) Network::Host((uint16_t)m_NetPort);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        ImGui::InputText("##addr", &m_JoinAddress);
        ImGui::SameLine();
        if (ImGui::Button("Join")) Network::Connect(m_JoinAddress, (uint16_t)m_NetPort);
    } else if (ImGui::Button("Disconnect")) {
        Network::Disconnect();
    }
    ImGui::EndDisabled();
    if (!playing) ImGui::TextDisabled("Enter play mode to host or join.");
    if (Network::IsActive()) {
        ImGui::SeparatorText("Peers");
        for (auto id : Network::Peers()) ImGui::BulletText("Peer %u%s", id, id == Network::LocalId() ? " (you)" : "");
    }
    ImGui::End();
}

// ------------------------------------------------------------------ project
void EditorApp::DrawProjectSettings() {
    ImGui::Begin("Project Settings", &m_ShowProject);
    auto& info = Project::Info();
    ImGui::InputText("Name", &info.name);
    ImGui::InputText("Start scene", &info.startScene);
    ImGui::InputInt("Window width", &info.windowWidth);
    ImGui::InputInt("Window height", &info.windowHeight);
    ImGui::Checkbox("Fullscreen", &info.fullscreen);
    ImGui::Checkbox("VSync", &info.vsync);
    ImGui::SeparatorText("Scenes in build");
    for (size_t i = 0; i < info.scenes.size(); i++) {
        ImGui::PushID((int)i);
        ImGui::BulletText("%s", info.scenes[i].c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) { info.scenes.erase(info.scenes.begin() + i); ImGui::PopID(); break; }
        ImGui::PopID();
    }
    if (m_EditorScene && !m_EditorScene->path.empty() && ImGui::Button("Add current scene")) {
        if (std::find(info.scenes.begin(), info.scenes.end(), m_EditorScene->path) == info.scenes.end()) info.scenes.push_back(m_EditorScene->path);
    }
    ImGui::Separator();
    if (ImGui::Button("Save project.json")) Project::Save();
    ImGui::TextDisabled("Root: %s", Project::Root().string().c_str());
    ImGui::End();
}

// ------------------------------------------------------------------ popups
static std::vector<std::string> FindScenes() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(Project::Root(), ec))
        if (e.is_regular_file() && e.path().extension() == ".scene") out.push_back(Project::MakeRelative(e.path()));
    std::sort(out.begin(), out.end());
    return out;
}

void EditorApp::DrawPopups() {
    if (m_OpenScenePopup) { ImGui::OpenPopup("Open Scene"); m_OpenScenePopup = false; }
    if (m_SaveAsPopup) { ImGui::OpenPopup("Save Scene As"); m_SaveAsPopup = false; }
    if (m_BuildPopup) { ImGui::OpenPopup("Build Game"); m_BuildPopup = false; }
    if (m_AboutPopup) { ImGui::OpenPopup("About 9divine"); m_AboutPopup = false; }
    if (m_ConfirmQuit) { ImGui::OpenPopup("Unsaved Changes"); m_ConfirmQuit = false; }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        for (auto& s : FindScenes())
            if (ImGui::Selectable(s.c_str(), false, 0, ImVec2(360, 0))) { OpenScene(s); ImGui::CloseCurrentPopup(); }
        ImGui::Separator();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Project-relative path:");
        ImGui::SetNextItemWidth(360);
        ImGui::InputText("##path", &m_SaveAsPath);
        if (ImGui::Button("Save", ImVec2(120, 0))) { SaveSceneAs(m_SaveAsPath); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Build Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Packages the player, engine assets and this project into a folder\nyou can zip up and share.");
        ImGui::SetNextItemWidth(460);
        ImGui::InputText("Output folder", &m_BuildDir);
        ImGui::Text("Start scene: %s   (%zu scene(s) in build)", Project::Info().startScene.c_str(), Project::Info().scenes.size());
        if (ImGui::Button("Build", ImVec2(140, 0))) {
            if (m_Dirty) SaveScene();
            m_BuildOk = BuildGame(m_BuildDir, m_BuildLog);
        }
        ImGui::SameLine();
        if (m_BuildOk && ImGui::Button("Open Folder", ImVec2(140, 0))) Platform::OpenInFileBrowser(m_BuildDir);
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        if (!m_BuildLog.empty()) {
            ImGui::Separator();
            ImGui::BeginChild("##buildlog", ImVec2(560, 180), true);
            ImGui::TextUnformatted(m_BuildLog.c_str());
            ImGui::EndChild();
        }
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About 9divine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("9divine 0.1");
        ImGui::TextDisabled("C++17 / OpenGL 3.3 game engine with an ImGui editor.");
        ImGui::Separator();
        ImGui::BulletText("ECS, forward renderer with shadows, terrain, water, particles");
        ImGui::BulletText("Physics, skeletal animation, 3D audio, navigation + AI");
        ImGui::BulletText("Game UI, save system, UDP multiplayer, game export");
        ImGui::Separator();
        ImGui::TextDisabled("Libraries: GLFW, glad, GLM, Dear ImGui, ImGuizmo, stb,\ncgltf, tinyobjloader, ufbx, miniaudio, nlohmann/json");
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save changes to '%s' first?", m_EditorScene ? m_EditorScene->name.c_str() : "");
        auto proceed = [this] { m_PendingAction = std::move(m_AfterUnsavedPrompt); m_AfterUnsavedPrompt = nullptr; };
        if (ImGui::Button("Save", ImVec2(110, 0))) {
            if (m_State == PlayState::Play) StopPlay();
            if (SaveScene()) proceed();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(110, 0))) { m_Dirty = false; proceed(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0))) { m_AfterUnsavedPrompt = nullptr; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

} // namespace nd::editor
