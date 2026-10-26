#include "EditorApp.h"
#include <nd/scene/SceneSerializer.h>
#include <nd/assets/AssetManager.h>
#include <nd/renderer/GL.h>
#include <imgui.h>
#include <ImGuizmo.h>

namespace nd::editor {

EditorApp::EditorApp(const AppConfig& config, fs::path projectDir) : Application(config), m_ProjectDir(std::move(projectDir)) {}

Ref<Scene> EditorApp::ActiveScene() const {
    if (m_State == PlayState::Play && SceneManager::Active()) return SceneManager::Active();
    return m_EditorScene;
}

Entity EditorApp::Selected() {
    if (!m_SelectedID) return {};
    auto scene = ActiveScene();
    return scene ? scene->FindByUUID(m_SelectedID) : Entity();
}

void EditorApp::Select(Entity e) { m_SelectedID = e ? e.GetUUID() : UUID(0); }

void EditorApp::OnInit() {
    Input::LoadMap(Project::Info().inputMap);
    FramebufferSpec spec;
    spec.width = 1280;
    spec.height = 720;
    m_Viewport = MakeScope<Framebuffer>(spec);
    m_AssetDir = Project::Root();
    m_BuildDir = (Project::Root().parent_path() / "Builds" / Project::Info().name).generic_string();
    SceneManager::onQuitRequested = [this] { if (m_State == PlayState::Play) StopPlay(); };

    std::string start = Project::Info().startScene;
    if (start.empty() || !OpenScene(start)) NewScene();
    m_RenderOptions.showLightGizmos = true;
    ND_INFO("9divine Editor ready. Press Play (Ctrl+P) to run the game.");
}

void EditorApp::OnShutdown() {
    if (m_State == PlayState::Play) StopPlay();
    UI::Shutdown();
    m_EditorScene.reset();
}

bool EditorApp::OnCloseRequested() {
    if (!m_Dirty) return true;
    m_ConfirmQuit = true;
    return false;
}

void EditorApp::UpdateTitle() {
    std::string title = "9divine Editor - " + Project::Info().name + " - " + (m_EditorScene ? m_EditorScene->name : std::string("")) +
                        (m_EditorScene && !m_EditorScene->path.empty() ? " (" + m_EditorScene->path + ")" : "") + (m_Dirty ? " *" : "");
    if (m_State == PlayState::Play) title += "  [PLAYING]";
    SetTitle(title);
}

// ----------------------------------------------------------------- scenes
void EditorApp::NewScene() {
    if (m_State == PlayState::Play) StopPlay();
    m_EditorScene = MakeRef<Scene>();
    m_EditorScene->name = "Untitled";
    // A sensible starting point: sun, camera, ground
    Entity sun = m_EditorScene->CreateEntity("Sun");
    sun.Transform().SetEulerDegrees({ -45, 35, 0 });
    sun.Transform().position = { 0, 10, 0 };
    auto& l = sun.Add<LightComponent>();
    l.type = LightType::Directional;
    l.intensity = 2.2f;
    Entity cam = m_EditorScene->CreateEntity("Main Camera");
    cam.Transform().position = { 0, 3, 10 };
    cam.Transform().SetEulerDegrees({ -12, 0, 0 });
    cam.Add<CameraComponent>();
    Entity ground = m_EditorScene->CreateEntity("Ground");
    ground.Transform().scale = { 40, 1, 40 };
    ground.Transform().position = { 0, -0.5f, 0 };
    auto& mr = ground.Add<MeshRendererComponent>();
    mr.color = { 0.75f, 0.78f, 0.8f, 1 };
    ground.Add<BoxColliderComponent>();
    m_SelectedID = 0;
    m_UndoStack.clear();
    m_RedoStack.clear();
    m_Snapshot = SceneSerializer(*m_EditorScene).ToJson().dump();
    m_Dirty = false;
    UpdateTitle();
}

bool EditorApp::OpenScene(const std::string& path) {
    if (m_State == PlayState::Play) StopPlay();
    auto scene = SceneManager::LoadNow(path);
    if (!scene) return false;
    m_EditorScene = scene;
    m_SelectedID = 0;
    m_UndoStack.clear();
    m_RedoStack.clear();
    m_Snapshot = SceneSerializer(*m_EditorScene).ToJson().dump();
    m_Dirty = false;
    // Frame the scene's main camera if there is one
    if (Entity cam = m_EditorScene->PrimaryCamera()) {
        vec3 p = m_EditorScene->WorldPosition(cam);
        m_Camera.Focus(p + vec3(0, -1.5f, -8.0f), 14.0f);
    }
    UpdateTitle();
    return true;
}

bool EditorApp::SaveScene() {
    if (!m_EditorScene) return false;
    if (m_EditorScene->path.empty()) { m_SaveAsPopup = true; return false; }
    return SaveSceneAs(m_EditorScene->path);
}

bool EditorApp::SaveSceneAs(const std::string& path) {
    if (m_State == PlayState::Play) { ND_WARN("Stop play mode before saving"); return false; }
    std::string p = path;
    if (fs::path(p).extension() != ".scene") p += ".scene";
    if (m_EditorScene->name == "Untitled") m_EditorScene->name = fs::path(p).stem().string();
    bool ok = SceneSerializer(*m_EditorScene).Save(p);
    if (ok) {
        m_Dirty = false;
        auto& scenes = Project::Info().scenes;
        if (std::find(scenes.begin(), scenes.end(), p) == scenes.end()) { scenes.push_back(p); Project::Save(); }
    }
    UpdateTitle();
    return ok;
}

void EditorApp::StartPlay() {
    if (m_State == PlayState::Play) return;
    Commit();
    auto play = Scene::Copy(*m_EditorScene);
    SceneManager::SetActive(play);
    m_State = PlayState::Play;
    m_GameView = true;
    Time::SetScale(1.0f);
    play->OnRuntimeStart();
    ImGui::SetWindowFocus("Viewport");
    UpdateTitle();
}

void EditorApp::StopPlay() {
    if (m_State != PlayState::Play) return;
    Network::Disconnect();
    if (auto s = SceneManager::Active()) if (s->IsRunning()) s->OnRuntimeStop();
    SceneManager::SetActive(nullptr);
    m_State = PlayState::Edit;
    Input::SetCursorMode(CursorMode::Normal);
    Time::SetScale(1.0f);
    Audio::StopAll();
    UpdateTitle();
}

// ----------------------------------------------------------------- undo
void EditorApp::Commit() {
    if (!m_EditorScene || m_State == PlayState::Play) return;
    std::string now = SceneSerializer(*m_EditorScene).ToJson().dump();
    if (now == m_Snapshot) return;
    m_UndoStack.push_back(std::move(m_Snapshot));
    if (m_UndoStack.size() > 60) m_UndoStack.erase(m_UndoStack.begin());
    m_RedoStack.clear();
    m_Snapshot = std::move(now);
    if (!m_Dirty) { m_Dirty = true; UpdateTitle(); }
}

void EditorApp::RestoreSnapshot(const std::string& data) {
    auto scene = MakeRef<Scene>();
    SceneSerializer(*scene).FromJson(json::parse(data));
    scene->path = m_EditorScene->path;
    m_EditorScene = scene;
    m_Snapshot = data;
    m_Dirty = true;
    UpdateTitle();
}

void EditorApp::Undo() {
    if (m_State == PlayState::Play || m_UndoStack.empty()) return;
    Commit();   // capture any un-committed change first
    if (m_UndoStack.empty()) return;
    m_RedoStack.push_back(m_Snapshot);
    std::string prev = std::move(m_UndoStack.back());
    m_UndoStack.pop_back();
    RestoreSnapshot(prev);
}

void EditorApp::Redo() {
    if (m_State == PlayState::Play || m_RedoStack.empty()) return;
    m_UndoStack.push_back(m_Snapshot);
    std::string next = std::move(m_RedoStack.back());
    m_RedoStack.pop_back();
    RestoreSnapshot(next);
}

// ----------------------------------------------------------------- entities
vec3 EditorApp::SpawnPoint() {
    auto scene = ActiveScene();
    Ray ray{ m_Camera.Position(), m_Camera.Forward() };
    RaycastHit hit;
    if (scene && scene->Physics().Raycast(ray, 60.0f, hit)) return hit.point;
    return m_Camera.pivot;
}

Entity EditorApp::CreateEntity(const std::string& name, Entity parent) {
    auto scene = ActiveScene();
    Entity e = scene->CreateEntity(name);
    if (parent) e.Transform().parent = parent.GetUUID();
    else e.Transform().position = SpawnPoint();
    Select(e);
    return e;
}

Entity EditorApp::CreatePrimitive(const std::string& shape) {
    std::string name = shape;
    name[0] = (char)std::toupper(name[0]);
    Entity e = CreateEntity(name);
    e.Add<MeshRendererComponent>().mesh = "builtin:" + shape;
    if (shape == "sphere") e.Add<SphereColliderComponent>();
    else if (shape != "quad" && shape != "plane") e.Add<BoxColliderComponent>();
    if (shape != "plane" && shape != "quad") e.Transform().position.y += 0.5f;
    if (shape == "plane") e.Transform().scale = vec3(10, 1, 10);
    return e;
}

Entity EditorApp::CreateFromAsset(const std::string& path, const vec3& position) {
    auto scene = ActiveScene();
    if (AssetManager::IsSceneFile(path)) { if (m_State == PlayState::Edit) OpenScene(path); return {}; }
    if (!AssetManager::IsModelFile(path)) return {};
    Entity e = scene->CreateEntity(fs::path(path).stem().string());
    e.Transform().position = position;
    e.Add<MeshRendererComponent>().mesh = path;
    if (auto model = AssetManager::GetModel(path)) {
        // Auto-scale huge models (centimetre exports) to ~2 m
        float size = glm::length(model->bounds.max - model->bounds.min);
        if (size > 20.0f) e.Transform().scale = vec3(2.0f / size * 1.7f);
        if (model->IsSkinned() && !model->clips.empty()) {
            auto& anim = e.Add<AnimatorComponent>();
            for (auto& c : model->clips) {
                AnimState st;
                st.name = c->name;
                st.clip = c->name;
                anim.states.push_back(st);
            }
            anim.defaultState = model->clips[0]->name;
        }
    }
    Select(e);
    Commit();
    return e;
}

void EditorApp::DeleteSelected() {
    Entity e = Selected();
    if (!e) return;
    ActiveScene()->DestroyEntity(e);
    m_SelectedID = 0;
    Commit();
}

void EditorApp::DuplicateSelected() {
    Entity e = Selected();
    if (!e) return;
    Entity copy = ActiveScene()->DuplicateEntity(e);
    if (copy) {
        copy.Get<TagComponent>().name = e.Name() + " (copy)";
        Select(copy);
    }
    Commit();
}

// ----------------------------------------------------------------- frame
void EditorApp::OnUpdate(float dt) {
    AssetManager::PollChanges();
    if (m_State == PlayState::Play) {
        Input::SetGameInputEnabled(m_ViewportFocused || Input::GetCursorMode() == CursorMode::Locked);
        SceneManager::Update();
        if (auto s = SceneManager::Active()) s->OnUpdateRuntime(Time::Delta());
        SceneManager::Update();
        if (m_State == PlayState::Play && !SceneManager::Active()) StopPlay();
    } else {
        Input::SetGameInputEnabled(false);
        if (m_EditorScene) m_EditorScene->OnUpdateEditor(dt);
    }
    RenderViewport();
    ivec2 fb = FramebufferSize();
    Framebuffer::BindDefault(fb.x, fb.y);
    glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void EditorApp::OnImGui() {
    ImGuizmo::BeginFrame();
    ImGuiIO& io = ImGui::GetIO();

    // Global shortcuts
    bool ctrl = io.KeyCtrl;
    if (!io.WantTextInput) {
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveScene();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) NewScene();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) m_OpenScenePopup = true;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) Redo(); else Undo(); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) DuplicateSelected();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) { if (m_State == PlayState::Edit) StartPlay(); else StopPlay(); }
        bool gameHasMouse = m_State == PlayState::Play && Input::GetCursorMode() == CursorMode::Locked;
        if (!ctrl && !m_Camera.flying && !gameHasMouse && (m_ViewportHovered || m_ViewportFocused)) {
            if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_GizmoOp = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_GizmoOp = 1;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_GizmoOp = 2;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_GizmoOp = 3;
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) if (Entity e = Selected()) m_Camera.Focus(ActiveScene()->WorldPosition(e), 6.0f);
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_State == PlayState::Edit) DeleteSelected();
        }
        // Release the mouse from a running game
        if (gameHasMouse && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) Input::SetCursorMode(CursorMode::Normal);
    }

    ImGuiID dock = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    if (m_FirstFrame) {
        SetupDefaultLayout(dock);
        m_FirstFrame = false;
        if (playOnStart) StartPlay();
    }
    DrawMenuBar();
    DrawViewportWindow();
    if (m_ShowHierarchy) DrawHierarchy();
    if (m_ShowInspector) DrawInspector();
    if (m_ShowAssets) DrawAssetBrowser();
    if (m_ShowConsole) DrawConsole();
    if (m_ShowProfiler) DrawProfiler();
    if (m_ShowSceneSettings) DrawSceneSettings();
    if (m_ShowInput) DrawInputSettings();
    if (m_ShowNetwork) DrawNetworkPanel();
    if (m_ShowProject) DrawProjectSettings();
    if (m_ShowDemo) ImGui::ShowDemoWindow(&m_ShowDemo);
    DrawPopups();

    // Commit inspector edits once the user lets go of a widget
    if (!ImGui::IsAnyItemActive() && m_EditedThisInteraction) {
        m_EditedThisInteraction = false;
        Commit();
    }
}

} // namespace nd::editor
