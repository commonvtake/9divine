#pragma once
#include <nd/9divine.h>
#include <nd/core/FileSystem.h>
#include <nd/renderer/Framebuffer.h>
#include <nd/renderer/Renderer.h>
#include "EditorCamera.h"

namespace nd::editor {

enum class PlayState { Edit, Play };

class EditorApp : public Application {
public:
    EditorApp(const AppConfig& config, fs::path projectDir);

    // Scene the editor is showing: the play copy while playing, else the edited scene.
    Ref<Scene> ActiveScene() const;
    Entity Selected();
    void Select(Entity e);
    void Select(UUID id) { m_SelectedID = id; }

protected:
    void OnInit() override;
    void OnUpdate(float dt) override;
    void OnImGui() override;
    void OnShutdown() override;
    bool OnCloseRequested() override;

private:
    // Scene management (EditorApp.cpp)
    void NewScene();
    bool OpenScene(const std::string& path);
    bool SaveScene();
    bool SaveSceneAs(const std::string& path);
    void StartPlay();
    void StopPlay();
    void UpdateTitle();

    // Undo (EditorApp.cpp)
    void Commit();                  // record the current scene as a new undo step
    void Undo();
    void Redo();
    void RestoreSnapshot(const std::string& data);

    // Entity helpers
    Entity CreateEntity(const std::string& name, Entity parent = {});
    Entity CreatePrimitive(const std::string& shape);
    Entity CreateFromAsset(const std::string& path, const vec3& position);
    void DeleteSelected();
    void DuplicateSelected();
    vec3 SpawnPoint();

    // Rendering + viewport (EditorViewport.cpp)
    void RenderViewport();
    void DrawViewportWindow();
    void DrawToolbar();
    void HandleViewportClick();
    void HandleTerrainSculpt();
    void DrawEditorOverlays(Scene& scene);

    // Panels (EditorPanels.cpp)
    void DrawMenuBar();
    void DrawHierarchy();
    void DrawEntityNode(Entity e);
    void DrawCreateMenu(Entity parent);
    void DrawInspector();
    void DrawAssetBrowser();
    void DrawConsole();
    void DrawProfiler();
    void DrawSceneSettings();
    void DrawInputSettings();
    void DrawNetworkPanel();
    void DrawProjectSettings();
    void DrawPopups();
    void SetupDefaultLayout(unsigned int dockspaceId);

    // Components (EditorInspector.cpp)
    void DrawComponents(Entity e);
    void DrawAddComponentMenu(Entity e);

    // Build (ProjectBuilder.cpp)
public:
    static bool BuildGame(const fs::path& outputDir, std::string& log);
private:

    fs::path m_ProjectDir;
    Ref<Scene> m_EditorScene;
    PlayState m_State = PlayState::Edit;
    UUID m_SelectedID = 0;
    bool m_Dirty = false;

    // Viewport
    Scope<Framebuffer> m_Viewport;
    vec2 m_ViewportSize{ 1280, 720 };
    vec2 m_ViewportMin{ 0.0f };
    bool m_ViewportHovered = false, m_ViewportFocused = false;
    bool m_GameView = true;           // while playing: render through the game camera
    EditorCamera m_Camera;
    int m_GizmoOp = 1;                // 0 none, 1 translate, 2 rotate, 3 scale
    bool m_GizmoLocal = false;
    bool m_Snap = false;
    bool m_GizmoWasUsing = false;
    RenderOptions m_RenderOptions;
    bool m_ShowGrid = true;
    bool m_ShowUI = true;
    bool m_ShowStats = true;

    // Undo
    std::vector<std::string> m_UndoStack, m_RedoStack;
    std::string m_Snapshot;
    bool m_EditedThisInteraction = false;

    // Terrain sculpting
    bool m_Sculpt = false;
    int m_SculptMode = 0;
    float m_BrushRadius = 6.0f, m_BrushStrength = 4.0f;
    bool m_Sculpting = false;
    bool m_BrushHit = false;
    vec3 m_BrushPoint{ 0.0f };
    float m_FlattenHeight = 0.0f;

    // Panels
    bool m_ShowHierarchy = true, m_ShowInspector = true, m_ShowAssets = true, m_ShowConsole = true;
    bool m_ShowProfiler = true, m_ShowSceneSettings = true, m_ShowInput = false, m_ShowNetwork = false, m_ShowProject = false;
    bool m_ShowDemo = false;
    fs::path m_AssetDir;
    std::string m_AssetFilter;
    bool m_ConsoleAutoScroll = true;
    int m_ConsoleLevel = 0;

    // Popups
    bool m_OpenScenePopup = false, m_SaveAsPopup = false, m_BuildPopup = false, m_AboutPopup = false, m_ConfirmQuit = false;
    std::string m_SaveAsPath = "Scenes/NewScene.scene";
    std::string m_BuildDir;
    std::string m_BuildLog;
    bool m_BuildOk = false;
    std::string m_JoinAddress = "127.0.0.1";
    int m_NetPort = 7777;
    bool m_FirstFrame = true;
public:
    bool playOnStart = false;
};

} // namespace nd::editor
