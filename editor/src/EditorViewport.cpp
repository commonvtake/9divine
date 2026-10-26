#include "EditorApp.h"
#include <nd/assets/AssetManager.h>
#include <nd/renderer/GL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

namespace nd::editor {

void EditorApp::DrawEditorOverlays(Scene& scene) {
    Registry& reg = scene.Reg();
    bool editing = m_State == PlayState::Edit || !m_GameView;
    if (!editing) return;
    if (m_ShowGrid) DebugDraw::Grid(vec3(std::round(m_Camera.pivot.x), 0.0f, std::round(m_Camera.pivot.z)), 60.0f, 1.0f, { 1, 1, 1, 0.06f });

    // Camera frustums
    reg.View<TransformComponent, CameraComponent>([&](EntityId id, TransformComponent&, CameraComponent& c) {
        Entity e(id, &scene);
        mat4 w = scene.WorldMatrix(e);
        RenderCamera rc = Renderer::MakeCamera(w, c.fov, 16.0f / 9.0f, c.nearClip, c.farClip, c.orthographic, c.orthoSize);
        mat4 inv = glm::inverse(rc.projection * rc.view);
        vec3 corners[8];
        for (int i = 0; i < 8; i++) {
            vec4 ndc((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f, 1.0f);
            if (i & 4) ndc.z = 0.92f;   // shorten the far plane so the gizmo stays readable
            vec4 p = inv * ndc;
            corners[i] = vec3(p) / p.w;
        }
        const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
        for (auto& ed : edges) DebugDraw::Line(corners[ed[0]], corners[ed[1]], { 0.8f, 0.8f, 0.8f, 0.5f });
    });

    // Selection
    Entity sel = Selected();
    if (sel) {
        mat4 w = scene.WorldMatrix(sel);
        if (auto* mr = sel.TryGet<MeshRendererComponent>(); mr && mr->model) {
            AABB b = mr->model->bounds;
            vec3 c = vec3(w * vec4(b.Center(), 1.0f));
            vec3 t, s; quat r;
            DecomposeTRS(w, t, r, s);
            DebugDraw::Box(c, b.Extents() * s * 1.02f, r, { 1.0f, 0.65f, 0.15f, 1.0f });
        }
        if (auto* l = sel.TryGet<LightComponent>(); l && l->type != LightType::Directional)
            DebugDraw::Sphere(vec3(w[3]), l->range, { l->color, 0.35f }, 32);
        if (auto* a = sel.TryGet<AudioSourceComponent>(); a && a->spatial) {
            DebugDraw::Sphere(vec3(w[3]), a->minDistance, { 0.4f, 0.7f, 1.0f, 0.5f }, 24);
            DebugDraw::Sphere(vec3(w[3]), a->maxDistance, { 0.4f, 0.7f, 1.0f, 0.2f }, 32);
        }
        if (auto* n = sel.TryGet<NavAgentComponent>()) DebugDraw::Path(n->path);
        if (sel.Has<BoxColliderComponent>() || sel.Has<SphereColliderComponent>()) {
            // Draw just this entity's collider (all colliders via the toolbar toggle)
            if (auto* bc = sel.TryGet<BoxColliderComponent>()) {
                vec3 t, s; quat r;
                DecomposeTRS(w, t, r, s);
                DebugDraw::Box(vec3(w * vec4(bc->offset, 1.0f)), bc->size * s * 0.5f, r, { 0.3f, 1.0f, 0.4f, 0.9f });
            }
            if (auto* sc = sel.TryGet<SphereColliderComponent>()) {
                vec3 t, s; quat r;
                DecomposeTRS(w, t, r, s);
                DebugDraw::Sphere(vec3(w * vec4(sc->offset, 1.0f)), sc->radius * std::max(s.x, std::max(s.y, s.z)), { 0.3f, 1.0f, 0.4f, 0.9f });
            }
        }
    }
    if (m_BrushHit && m_Sculpt) DebugDraw::Circle(m_BrushPoint + vec3(0, 0.05f, 0), m_BrushRadius, { 0, 1, 0 }, { 0.3f, 1.0f, 0.6f, 1.0f }, 48);
}

void EditorApp::RenderViewport() {
    auto scene = ActiveScene();
    if (!scene) return;
    int w = std::max(1, (int)m_ViewportSize.x), h = std::max(1, (int)m_ViewportSize.y);
    m_Viewport->Resize(w, h);
    float aspect = (float)w / h;

    RenderCamera cam;
    bool gameCam = m_State == PlayState::Play && m_GameView && scene->GetRuntimeCamera(aspect, cam.view, cam.projection, cam.position);
    if (!gameCam) cam = m_Camera.GetRenderCamera(aspect);

    DrawEditorOverlays(*scene);
    RenderOptions opt = m_RenderOptions;
    opt.selected = Selected() ? Selected().Id() : NullEntity;
    opt.editorMode = !gameCam;
    if (gameCam) opt.showLightGizmos = false;
    Renderer::RenderScene(*scene, cam, *m_Viewport, opt);

    if (m_ShowUI || m_State == PlayState::Play) {
        m_Viewport->Bind();
        vec2 mouse = Input::MousePosition() - m_ViewportMin;
        // Window coords -> framebuffer pixels (HiDPI)
        ivec2 fb = FramebufferSize(), win = WindowSize();
        mouse *= vec2((float)fb.x / std::max(1, win.x), (float)fb.y / std::max(1, win.y));
        bool allowMouse = m_ViewportHovered && m_State == PlayState::Play;
        scene->RenderGameUI(w, h, allowMouse ? mouse : vec2(-1000.0f), allowMouse && Input::RawMouseDown(Mouse::Left),
                            allowMouse && Input::IsMousePressed(Mouse::Left));
    }
}

void EditorApp::DrawToolbar() {
    auto toolButton = [](const char* label, bool active, const char* tip) {
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        bool pressed = ImGui::Button(label);
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        return pressed;
    };
    if (toolButton("Select", m_GizmoOp == 0, "No gizmo (Q)")) m_GizmoOp = 0;
    ImGui::SameLine();
    if (toolButton("Move", m_GizmoOp == 1, "Translate (W)")) m_GizmoOp = 1;
    ImGui::SameLine();
    if (toolButton("Rotate", m_GizmoOp == 2, "Rotate (E)")) m_GizmoOp = 2;
    ImGui::SameLine();
    if (toolButton("Scale", m_GizmoOp == 3, "Scale (R)")) m_GizmoOp = 3;
    ImGui::SameLine();
    if (toolButton(m_GizmoLocal ? "Local" : "World", false, "Gizmo space")) m_GizmoLocal = !m_GizmoLocal;
    ImGui::SameLine();
    if (toolButton("Snap", m_Snap, "Snap to grid (hold Ctrl while dragging)")) m_Snap = !m_Snap;

    // Play controls (centered)
    float playWidth = 230.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20.0f, (ImGui::GetWindowWidth() - playWidth) * 0.5f));
    bool playing = m_State == PlayState::Play;
    if (playing) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.55f, 0.4f, 1.0f));
    if (ImGui::Button(playing ? "  Stop  " : "  Play  ")) { if (playing) StopPlay(); else StartPlay(); }
    if (playing) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+P");
    ImGui::SameLine();
    ImGui::BeginDisabled(!playing);
    auto scene = ActiveScene();
    bool paused = playing && scene && scene->IsPaused();
    if (toolButton("Pause", paused, "Pause the game")) scene->SetPaused(!paused);
    ImGui::SameLine();
    if (toolButton("Step", false, "Advance one frame while paused")) { scene->SetPaused(true); scene->Step(1); }
    ImGui::SameLine();
    if (toolButton(m_GameView ? "Game View" : "Scene View", false, "Switch between the game camera and the editor camera")) m_GameView = !m_GameView;
    ImGui::EndDisabled();

    // View options (right)
    ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
    if (ImGui::Button("View ...")) ImGui::OpenPopup("ViewOptions");
    if (ImGui::BeginPopup("ViewOptions")) {
        ImGui::Checkbox("Grid", &m_ShowGrid);
        ImGui::Checkbox("Colliders", &m_RenderOptions.showColliders);
        ImGui::Checkbox("Navigation grid", &m_RenderOptions.showNavGrid);
        ImGui::Checkbox("Light gizmos", &m_RenderOptions.showLightGizmos);
        ImGui::Checkbox("Game UI", &m_ShowUI);
        ImGui::Checkbox("Stats overlay", &m_ShowStats);
        ImGui::Separator();
        ImGui::SliderFloat("Camera FOV", &m_Camera.fov, 20.0f, 100.0f);
        ImGui::SliderFloat("Fly speed", &m_Camera.flySpeed, 0.5f, 100.0f);
        ImGui::EndPopup();
    }
}

void EditorApp::DrawViewportWindow() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    DrawToolbar();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    m_ViewportSize = { std::max(1.0f, avail.x), std::max(1.0f, avail.y) };
    ImVec2 pos = ImGui::GetCursorScreenPos();
    m_ViewportMin = { pos.x - ImGui::GetMainViewport()->Pos.x, pos.y - ImGui::GetMainViewport()->Pos.y };
    ImGui::Image((ImTextureID)(uintptr_t)m_Viewport->ColorAttachment(0), avail, ImVec2(0, 1), ImVec2(1, 0));
    m_ViewportHovered = ImGui::IsItemHovered();
    m_ViewportFocused = ImGui::IsWindowFocused();
    if (m_ViewportHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) ImGui::SetWindowFocus();

    // Drop assets from the browser into the scene
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ND_ASSET")) {
            std::string path((const char*)p->Data, p->DataSize);
            vec2 local = vec2(ImGui::GetMousePos().x - pos.x, ImGui::GetMousePos().y - pos.y);
            vec2 ndc(local.x / avail.x * 2.0f - 1.0f, 1.0f - local.y / avail.y * 2.0f);
            Ray ray = m_Camera.ScreenRay(ndc, avail.x / avail.y);
            RaycastHit hit;
            vec3 at = ActiveScene()->Physics().Raycast(ray, 200.0f, hit) ? hit.point : ray.origin + ray.direction * 10.0f;
            CreateFromAsset(path, at);
        }
        ImGui::EndDragDropTarget();
    }

    bool gameCam = m_State == PlayState::Play && m_GameView;
    if (!gameCam) m_Camera.Update(Time::UnscaledDelta(), m_ViewportHovered, m_ViewportFocused);

    // Gizmo
    Entity sel = Selected();
    bool usingGizmo = false;
    if (sel && m_GizmoOp != 0 && !gameCam && !m_Sculpt) {
        auto scene = ActiveScene();
        float aspect = avail.x / avail.y;
        RenderCamera cam = m_Camera.GetRenderCamera(aspect);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(pos.x, pos.y, avail.x, avail.y);
        mat4 world = scene->WorldMatrix(sel);
        ImGuizmo::OPERATION op = m_GizmoOp == 1 ? ImGuizmo::TRANSLATE : m_GizmoOp == 2 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
        bool snap = m_Snap || ImGui::GetIO().KeyCtrl;
        float snapValue = m_GizmoOp == 2 ? 15.0f : (m_GizmoOp == 3 ? 0.25f : 0.5f);
        float snaps[3] = { snapValue, snapValue, snapValue };
        ImGuizmo::Manipulate(glm::value_ptr(cam.view), glm::value_ptr(cam.projection), op, m_GizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                             glm::value_ptr(world), nullptr, snap ? snaps : nullptr);
        usingGizmo = ImGuizmo::IsUsing();
        if (usingGizmo) {
            Entity parent = scene->GetParent(sel);
            mat4 local = parent ? glm::inverse(scene->WorldMatrix(parent)) * world : world;
            auto& t = sel.Transform();
            DecomposeTRS(local, t.position, t.rotation, t.scale);
            if (auto* rb = sel.TryGet<RigidBodyComponent>()) rb->velocity = vec3(0.0f);
        }
    }
    if (m_GizmoWasUsing && !usingGizmo) Commit();
    m_GizmoWasUsing = usingGizmo;

    if (m_Sculpt && !gameCam) HandleTerrainSculpt();
    else if (!usingGizmo && !gameCam) HandleViewportClick();

    // Stats overlay
    if (m_ShowStats) {
        auto& st = Profiler::Stats();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        char buf[256];
        snprintf(buf, sizeof(buf), "%.0f FPS  %.2f ms\n%u draws  %.1fk tris\n%u entities", st.fps, st.frameMs, st.drawCalls, st.triangles / 1000.0f, st.entities);
        ImVec2 tp = m_State == PlayState::Play ? ImVec2(pos.x + avail.x - 180, pos.y + avail.y - 96) : ImVec2(pos.x + 10, pos.y + 10);
        dl->AddRectFilled(ImVec2(tp.x - 6, tp.y - 4), ImVec2(tp.x + 170, tp.y + 58), IM_COL32(0, 0, 0, 120), 4.0f);
        dl->AddText(tp, IM_COL32(230, 235, 240, 255), buf);
    }
    if (m_State == PlayState::Play) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(pos, ImVec2(pos.x + avail.x, pos.y + avail.y), IM_COL32(70, 200, 150, 200), 0.0f, 0, 2.0f);
        if (Input::GetCursorMode() == CursorMode::Locked)
            dl->AddText(ImVec2(pos.x + avail.x - 250, pos.y + avail.y - 22), IM_COL32(255, 255, 255, 160), "Shift+Esc releases the mouse");
    }
    ImGui::End();
}

void EditorApp::HandleViewportClick() {
    if (!m_ViewportHovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::GetIO().KeyAlt) return;
    if (ImGuizmo::IsOver()) return;
    ImVec2 mouse = ImGui::GetMousePos();
    ImVec2 itemMin = ImGui::GetItemRectMin();
    int x = (int)(mouse.x - itemMin.x);
    int y = (int)(m_ViewportSize.y - (mouse.y - itemMin.y));
    int id = m_Viewport->ReadPixelInt(1, x, y);
    auto scene = ActiveScene();
    if (id < 0 || !scene->Reg().Valid((EntityId)id)) { Select(Entity()); return; }
    Entity e((EntityId)id, scene.get());
    // Clicking part of a model selects its top-level object first, then the part on a second click.
    Entity root = e;
    while (Entity p = scene->GetParent(root)) root = p;
    if (Selected() == root || Selected() == e) Select(e);
    else Select(root);
}

void EditorApp::HandleTerrainSculpt() {
    m_BrushHit = false;
    Entity sel = Selected();
    auto scene = ActiveScene();
    if (!sel || !sel.Has<TerrainComponent>()) { m_Sculpt = false; return; }
    auto& terrain = sel.Get<TerrainComponent>();
    ImVec2 mouse = ImGui::GetMousePos(), itemMin = ImGui::GetItemRectMin();
    vec2 local(mouse.x - itemMin.x, mouse.y - itemMin.y);
    vec2 ndc(local.x / m_ViewportSize.x * 2.0f - 1.0f, 1.0f - local.y / m_ViewportSize.y * 2.0f);
    Ray ray = m_Camera.ScreenRay(ndc, m_ViewportSize.x / m_ViewportSize.y);
    RaycastHit hit;
    if (m_ViewportHovered && scene->Physics().Raycast(ray, 1000.0f, hit) && hit.hitTerrain) {
        m_BrushHit = true;
        m_BrushPoint = hit.point;
    }
    bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt;
    if (m_BrushHit && down && m_ViewportHovered) {
        mat4 inv = glm::inverse(scene->WorldMatrix(sel));
        vec3 lp = vec3(inv * vec4(m_BrushPoint, 1.0f));
        if (!m_Sculpting) { m_Sculpting = true; m_FlattenHeight = lp.y; }
        SculptMode mode = (SculptMode)m_SculptMode;
        if (ImGui::GetIO().KeyShift && mode == SculptMode::Raise) mode = SculptMode::Lower;
        TerrainSystem::Sculpt(terrain, { lp.x, lp.z }, m_BrushRadius, m_BrushStrength * Time::UnscaledDelta(), mode, m_FlattenHeight);
    }
    if (m_Sculpting && !down) {
        m_Sculpting = false;
        terrain.foliageDirty = true;
        if (scene->Nav().IsBaked()) scene->Nav().Clear();
        Commit();
    }
}

} // namespace nd::editor
