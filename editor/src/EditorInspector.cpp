#include "EditorApp.h"
#include <nd/assets/AssetManager.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace nd::editor {

namespace {

// Two-column property layout helpers ---------------------------------------
bool g_Changed = false;
void Mark(bool c) { if (c) g_Changed = true; }

void BeginProps() { ImGui::Columns(2, nullptr, false); ImGui::SetColumnWidth(0, 120.0f); }
void EndProps() { ImGui::Columns(1); }
void Label(const char* l) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(l); ImGui::NextColumn(); ImGui::SetNextItemWidth(-1); }
void Next() { ImGui::NextColumn(); }

bool PFloat(const char* l, float& v, float speed = 0.05f, float mn = 0.0f, float mx = 0.0f, const char* fmt = "%.3f") {
    Label(l); ImGui::PushID(l); bool c = ImGui::DragFloat("##v", &v, speed, mn, mx, fmt); ImGui::PopID(); Next(); Mark(c); return c;
}
bool PInt(const char* l, int& v, float speed = 1.0f, int mn = 0, int mx = 0) {
    Label(l); ImGui::PushID(l); bool c = ImGui::DragInt("##v", &v, speed, mn, mx); ImGui::PopID(); Next(); Mark(c); return c;
}
bool PBool(const char* l, bool& v) { Label(l); ImGui::PushID(l); bool c = ImGui::Checkbox("##v", &v); ImGui::PopID(); Next(); Mark(c); return c; }
bool PColor3(const char* l, vec3& v, bool hdr = false) {
    Label(l); ImGui::PushID(l);
    bool c = ImGui::ColorEdit3("##v", glm::value_ptr(v), hdr ? ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float : 0);
    ImGui::PopID(); Next(); Mark(c); return c;
}
bool PColor4(const char* l, vec4& v) { Label(l); ImGui::PushID(l); bool c = ImGui::ColorEdit4("##v", glm::value_ptr(v), ImGuiColorEditFlags_AlphaBar); ImGui::PopID(); Next(); Mark(c); return c; }
bool PText(const char* l, std::string& v) { Label(l); ImGui::PushID(l); bool c = ImGui::InputText("##v", &v); ImGui::PopID(); Next(); Mark(c); return c; }

bool PCombo(const char* l, int& v, const char* const* items, int count) {
    Label(l); ImGui::PushID(l); bool c = ImGui::Combo("##v", &v, items, count); ImGui::PopID(); Next(); Mark(c); return c;
}

// XYZ with colored reset buttons
bool PVec3(const char* l, vec3& v, float reset = 0.0f, float speed = 0.05f) {
    Label(l);
    ImGui::PushID(l);
    bool changed = false;
    float w = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetFrameHeight() - 8) / 3.0f;
    const ImVec4 cols[3] = { { 0.75f, 0.25f, 0.25f, 1 }, { 0.3f, 0.65f, 0.3f, 1 }, { 0.25f, 0.4f, 0.8f, 1 } };
    const char* names[3] = { "X", "Y", "Z" };
    for (int i = 0; i < 3; i++) {
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Button, cols[i]);
        if (ImGui::Button(names[i], ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) { v[i] = reset; changed = true; }
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 1);
        ImGui::SetNextItemWidth(w);
        changed |= ImGui::DragFloat("##c", &v[i], speed, 0, 0, "%.2f");
        ImGui::PopID();
        if (i < 2) ImGui::SameLine(0, 3);
    }
    ImGui::PopID();
    Next();
    Mark(changed);
    return changed;
}

// Asset path field that accepts drag & drop from the Assets panel.
bool PAsset(const char* l, std::string& path, bool (*accept)(const std::string&)) {
    Label(l);
    ImGui::PushID(l);
    bool c = ImGui::InputTextWithHint("##v", "drop an asset here", &path);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ND_ASSET")) {
            std::string s((const char*)p->Data, p->DataSize);
            if (!accept || accept(s)) { path = s; c = true; }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
    Next();
    Mark(c);
    return c;
}

bool EntityRefWidget(const char* l, uint64_t& uuid, Scene& scene) {
    Label(l);
    ImGui::PushID(l);
    bool c = false;
    Entity e = uuid ? scene.FindByUUID(uuid) : Entity();
    std::string text = e ? e.Name() : (uuid ? "(missing)" : "None");
    if (ImGui::Button(text.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 26, 0))) ImGui::OpenPopup("pick");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ND_ENTITY")) { uuid = *(const uint64_t*)p->Data; c = true; }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine(0, 2);
    if (ImGui::Button("x", ImVec2(24, 0))) { uuid = 0; c = true; }
    if (ImGui::BeginPopup("pick")) {
        static std::string filter;
        ImGui::InputTextWithHint("##f", "Search", &filter);
        ImGui::BeginChild("##list", ImVec2(260, 260));
        for (EntityId id : scene.Reg().AllEntities()) {
            Entity x(id, &scene);
            if (!filter.empty() && x.Name().find(filter) == std::string::npos) continue;
            ImGui::PushID((int)id);
            if (ImGui::Selectable(x.Name().c_str())) { uuid = x.GetUUID(); c = true; ImGui::CloseCurrentPopup(); }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    Next();
    Mark(c);
    return c;
}

class ImGuiFieldVisitor : public FieldVisitor {
public:
    explicit ImGuiFieldVisitor(Scene& s) : m_Scene(s) {}
    void Float(const char* n, float& v, float speed) override { changed |= PFloat(n, v, speed); }
    void Int(const char* n, int& v) override { changed |= PInt(n, v); }
    void Bool(const char* n, bool& v) override { changed |= PBool(n, v); }
    void Vec3(const char* n, vec3& v) override { changed |= PVec3(n, v); }
    void Color(const char* n, vec4& v) override { changed |= PColor4(n, v); }
    void String(const char* n, std::string& v) override { changed |= PText(n, v); }
    void EntityRef(const char* n, uint64_t& v) override { changed |= EntityRefWidget(n, v, m_Scene); }
    bool changed = false;
private:
    Scene& m_Scene;
};

template <typename T, typename Fn>
void Section(Entity e, const char* name, Fn&& draw, bool removable = true) {
    if (!e.Has<T>()) return;
    ImGui::PushID(name);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
    bool open = ImGui::TreeNodeEx("##section", flags, "%s", name);
    bool remove = false;
    if (removable) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4);
        if (ImGui::SmallButton("...")) ImGui::OpenPopup("opts");
        if (ImGui::BeginPopup("opts")) {
            if (ImGui::MenuItem("Remove component")) remove = true;
            if (ImGui::MenuItem("Reset")) { e.Get<T>() = T{}; g_Changed = true; }
            ImGui::EndPopup();
        }
    }
    if (open) {
        BeginProps();
        draw(e.Get<T>());
        EndProps();
        ImGui::TreePop();
    }
    ImGui::PopID();
    if (remove) { e.Remove<T>(); g_Changed = true; }
    ImGui::Spacing();
}

bool IsModel(const std::string& p) { return AssetManager::IsModelFile(p); }
bool IsTexture(const std::string& p) { return AssetManager::IsTextureFile(p); }
bool IsAudio(const std::string& p) { return AssetManager::IsAudioFile(p); }

} // namespace

// ==========================================================================
void EditorApp::DrawComponents(Entity e) {
    g_Changed = false;
    auto scene = ActiveScene();
    bool playing = m_State == PlayState::Play;

    // Header: active + name + tag
    auto& tag = e.Get<TagComponent>();
    Mark(ImGui::Checkbox("##active", &tag.active));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    Mark(ImGui::InputText("##name", &tag.name));
    ImGui::SetNextItemWidth(160);
    Mark(ImGui::InputTextWithHint("##tag", "Tag", &tag.tag));
    ImGui::SameLine();
    ImGui::TextDisabled("ID %016llx", (unsigned long long)(uint64_t)e.GetUUID());
    ImGui::Separator();

    Section<TransformComponent>(e, "Transform", [&](TransformComponent& t) {
        PVec3("Position", t.position);
        vec3 euler = t.EulerDegrees();
        if (PVec3("Rotation", euler, 0.0f, 0.5f)) t.SetEulerDegrees(euler);
        PVec3("Scale", t.scale, 1.0f, 0.02f);
    }, false);

    Section<MeshRendererComponent>(e, "Mesh Renderer", [&](MeshRendererComponent& m) {
        Label("Mesh");
        if (ImGui::BeginCombo("##mesh", m.mesh.c_str())) {
            for (auto& n : Primitives::Names())
                if (ImGui::Selectable(("builtin:" + n).c_str(), m.mesh == "builtin:" + n)) { m.mesh = "builtin:" + n; g_Changed = true; }
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ND_ASSET")) {
                std::string s((const char*)p->Data, p->DataSize);
                if (IsModel(s)) { m.mesh = s; g_Changed = true; }
            }
            ImGui::EndDragDropTarget();
        }
        Next();
        if (m.model) {
            Label("Model");
            ImGui::TextDisabled("%zu mesh(es), %zu material(s)%s", m.model->meshes.size(), m.model->materials.size(), m.model->IsSkinned() ? ", skinned" : "");
            Next();
        }
        PBool("Model materials", m.useModelMaterials);
        PColor4("Color", m.color);
        PAsset("Texture", m.texture, IsTexture);
        PFloat("Tiling", m.tiling, 0.05f, 0.01f, 100.0f);
        PFloat("Specular", m.specular, 0.01f, 0.0f, 2.0f);
        PFloat("Shininess", m.shininess, 0.5f, 1.0f, 256.0f);
        PColor3("Emissive", m.emissive, true);
        PBool("Cast shadows", m.castShadows);
        PBool("Visible", m.visible);
    });

    Section<CameraComponent>(e, "Camera", [&](CameraComponent& c) {
        PBool("Primary", c.primary);
        PBool("Orthographic", c.orthographic);
        if (c.orthographic) PFloat("Size", c.orthoSize, 0.1f, 0.1f, 1000.0f);
        else PFloat("Field of view", c.fov, 0.2f, 10.0f, 150.0f);
        PFloat("Near", c.nearClip, 0.01f, 0.001f, 10.0f);
        PFloat("Far", c.farClip, 1.0f, 1.0f, 10000.0f);
    });

    Section<LightComponent>(e, "Light", [&](LightComponent& l) {
        const char* types[] = { "Directional", "Point", "Spot" };
        int t = (int)l.type;
        if (PCombo("Type", t, types, 3)) l.type = (LightType)t;
        PColor3("Color", l.color);
        PFloat("Intensity", l.intensity, 0.02f, 0.0f, 100.0f);
        if (l.type != LightType::Directional) PFloat("Range", l.range, 0.1f, 0.1f, 500.0f);
        if (l.type == LightType::Spot) PFloat("Spot angle", l.spotAngle, 0.2f, 1.0f, 179.0f);
        if (l.type == LightType::Directional) PBool("Shadows", l.castShadows);
    });

    Section<RigidBodyComponent>(e, "Rigid Body", [&](RigidBodyComponent& rb) {
        const char* types[] = { "Static", "Dynamic", "Kinematic" };
        int t = (int)rb.type;
        if (PCombo("Type", t, types, 3)) rb.type = (BodyType)t;
        PFloat("Mass", rb.mass, 0.05f, 0.001f, 10000.0f);
        PFloat("Drag", rb.drag, 0.01f, 0.0f, 100.0f);
        PFloat("Gravity scale", rb.gravityScale, 0.02f);
        PFloat("Bounciness", rb.restitution, 0.01f, 0.0f, 1.0f);
        PFloat("Friction", rb.friction, 0.01f, 0.0f, 2.0f);
        PBool("Lock height", rb.lockY);
        if (playing) {
            Label("Velocity");
            ImGui::Text("%.2f, %.2f, %.2f%s", rb.velocity.x, rb.velocity.y, rb.velocity.z, rb.grounded ? "  (grounded)" : "");
            Next();
        }
    });

    Section<BoxColliderComponent>(e, "Box Collider", [&](BoxColliderComponent& c) {
        PVec3("Size", c.size, 1.0f, 0.02f);
        PVec3("Offset", c.offset);
        PBool("Is trigger", c.isTrigger);
    });
    Section<SphereColliderComponent>(e, "Sphere Collider", [&](SphereColliderComponent& c) {
        PFloat("Radius", c.radius, 0.01f, 0.001f, 1000.0f);
        PVec3("Offset", c.offset);
        PBool("Is trigger", c.isTrigger);
    });

    Section<ScriptComponent>(e, "Scripts", [&](ScriptComponent& sc) {
        EndProps();
        int removeIndex = -1;
        for (size_t i = 0; i < sc.scripts.size(); i++) {
            auto& s = sc.scripts[i];
            ImGui::PushID((int)i);
            bool known = ScriptRegistry::Exists(s.className);
            ImGui::PushStyleColor(ImGuiCol_Text, known ? ImVec4(0.55f, 0.85f, 0.7f, 1) : ImVec4(1, 0.45f, 0.4f, 1));
            bool open = ImGui::TreeNodeEx("##script", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap, "%s%s", s.className.c_str(), known ? "" : "  (not found)");
            ImGui::PopStyleColor();
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10);
            if (ImGui::SmallButton("x")) removeIndex = (int)i;
            if (open) {
                BeginProps();
                ImGuiFieldVisitor v(*scene);
                if (s.instance) {
                    s.instance->Reflect(v);   // live values while playing
                } else if (known) {
                    auto temp = ScriptRegistry::Create(s.className);
                    LoadScriptFields(*temp, s.fields);
                    temp->Reflect(v);
                    if (v.changed) s.fields = SaveScriptFields(*temp);
                }
                EndProps();
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removeIndex >= 0) { sc.scripts.erase(sc.scripts.begin() + removeIndex); g_Changed = true; }
        if (ImGui::Button("Add Script", ImVec2(-1, 0))) ImGui::OpenPopup("AddScript");
        if (ImGui::BeginPopup("AddScript")) {
            for (auto& n : ScriptRegistry::Names())
                if (ImGui::MenuItem(n.c_str())) {
                    ScriptInstance si;
                    si.className = n;
                    if (auto temp = ScriptRegistry::Create(n)) si.fields = SaveScriptFields(*temp);
                    sc.scripts.push_back(si);
                    g_Changed = true;
                }
            ImGui::EndPopup();
        }
        BeginProps();
    });

    Section<AudioSourceComponent>(e, "Audio Source", [&](AudioSourceComponent& a) {
        PAsset("Clip", a.clip, IsAudio);
        PFloat("Volume", a.volume, 0.01f, 0.0f, 2.0f);
        PFloat("Pitch", a.pitch, 0.01f, 0.1f, 4.0f);
        PBool("Loop", a.loop);
        PBool("Play on start", a.playOnStart);
        PBool("Music bus", a.music);
        PBool("3D (spatial)", a.spatial);
        if (a.spatial) {
            PFloat("Min distance", a.minDistance, 0.1f, 0.01f, 1000.0f);
            PFloat("Max distance", a.maxDistance, 0.1f, 0.01f, 5000.0f);
        }
        Label("");
        if (ImGui::Button("Preview")) Audio::PlayOneShot(a.clip, a.volume);
        Next();
    });
    Section<AudioListenerComponent>(e, "Audio Listener", [&](AudioListenerComponent& l) { PBool("Active", l.active); });

    Section<AnimatorComponent>(e, "Animator", [&](AnimatorComponent& a) {
        auto* mr = e.TryGet<MeshRendererComponent>();
        std::vector<std::string> clips;
        if (mr && mr->model) for (auto& c : mr->model->clips) clips.push_back(c->name);
        Label("Clips");
        if (clips.empty()) ImGui::TextDisabled("needs a skinned model");
        else { std::string all; for (auto& c : clips) all += c + "  "; ImGui::TextWrapped("%s", all.c_str()); }
        Next();
        Label("Default state");
        if (ImGui::BeginCombo("##def", a.defaultState.c_str())) {
            for (auto& s : a.states) if (ImGui::Selectable(s.name.c_str(), s.name == a.defaultState)) { a.defaultState = s.name; g_Changed = true; }
            ImGui::EndCombo();
        }
        Next();
        PFloat("Speed", a.speed, 0.01f, 0.0f, 10.0f);
        PBool("Root motion", a.rootMotion);
        if (a.runtime) { Label("Current"); ImGui::Text("%s  (%.2f)", a.runtime->currentState.c_str(), a.runtime->NormalizedTime()); Next(); }
        EndProps();

        if (ImGui::TreeNode("Parameters")) {
            for (size_t i = 0; i < a.params.size(); i++) {
                auto& p = a.params[i];
                ImGui::PushID((int)i);
                ImGui::SetNextItemWidth(110);
                Mark(ImGui::InputText("##n", &p.name));
                ImGui::SameLine();
                const char* types[] = { "Float", "Bool", "Trigger" };
                int t = (int)p.type;
                ImGui::SetNextItemWidth(80);
                if (ImGui::Combo("##t", &t, types, 3)) { p.type = (AnimParamType)t; g_Changed = true; }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                if (p.type == AnimParamType::Float) Mark(ImGui::DragFloat("##v", &p.value, 0.05f));
                else { bool b = p.value > 0.5f; if (ImGui::Checkbox("##v", &b)) { p.value = b ? 1.0f : 0.0f; g_Changed = true; } }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) { a.params.erase(a.params.begin() + i); g_Changed = true; ImGui::PopID(); break; }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+ Parameter")) { a.params.push_back({ "Param", AnimParamType::Float, 0.0f }); g_Changed = true; }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("States")) {
            for (size_t i = 0; i < a.states.size(); i++) {
                auto& s = a.states[i];
                ImGui::PushID((int)i);
                bool open = ImGui::TreeNode("##st", "%s", s.name.c_str());
                ImGui::SameLine();
                if (a.runtime && ImGui::SmallButton("Play")) a.runtime->Play(a, s.name, 0.2f);
                ImGui::SameLine();
                bool del = ImGui::SmallButton("x");
                if (open) {
                    BeginProps();
                    PText("Name", s.name);
                    Label("Clip");
                    if (ImGui::BeginCombo("##clip", s.clip.c_str())) {
                        for (auto& c : clips) if (ImGui::Selectable(c.c_str(), c == s.clip)) { s.clip = c; g_Changed = true; }
                        ImGui::EndCombo();
                    }
                    Next();
                    PFloat("Speed", s.speed, 0.01f);
                    PBool("Loop", s.loop);
                    PText("Blend param", s.blendParam);
                    EndProps();
                    for (size_t b = 0; b < s.blend.size(); b++) {
                        ImGui::PushID((int)b);
                        ImGui::SetNextItemWidth(120);
                        if (ImGui::BeginCombo("##bc", s.blend[b].clip.c_str())) {
                            for (auto& c : clips) if (ImGui::Selectable(c.c_str())) { s.blend[b].clip = c; g_Changed = true; }
                            ImGui::EndCombo();
                        }
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(70);
                        Mark(ImGui::DragFloat("##th", &s.blend[b].threshold, 0.05f));
                        ImGui::SameLine();
                        if (ImGui::SmallButton("x")) { s.blend.erase(s.blend.begin() + b); g_Changed = true; ImGui::PopID(); break; }
                        ImGui::PopID();
                    }
                    if (ImGui::SmallButton("+ Blend clip")) { s.blend.push_back({ clips.empty() ? "" : clips[0], 0.0f }); g_Changed = true; }
                    ImGui::TreePop();
                }
                ImGui::PopID();
                if (del) { a.states.erase(a.states.begin() + i); g_Changed = true; break; }
            }
            if (ImGui::SmallButton("+ State")) { AnimState st; st.name = "State" + std::to_string(a.states.size()); if (!clips.empty()) st.clip = clips[0]; a.states.push_back(st); g_Changed = true; }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Transitions")) {
            for (size_t i = 0; i < a.transitions.size(); i++) {
                auto& t = a.transitions[i];
                ImGui::PushID((int)i);
                bool open = ImGui::TreeNode("##tr", "%s -> %s", t.from.c_str(), t.to.c_str());
                ImGui::SameLine();
                bool del = ImGui::SmallButton("x");
                if (open) {
                    BeginProps();
                    PText("From (* = any)", t.from);
                    PText("To", t.to);
                    PFloat("Blend time", t.duration, 0.01f, 0.0f, 5.0f);
                    PFloat("Exit time", t.exitTime, 0.01f, -1.0f, 1.0f);
                    EndProps();
                    for (size_t c = 0; c < t.conditions.size(); c++) {
                        auto& cd = t.conditions[c];
                        ImGui::PushID((int)c);
                        ImGui::SetNextItemWidth(100);
                        Mark(ImGui::InputText("##p", &cd.param));
                        ImGui::SameLine();
                        const char* ops[] = { ">", "<", "is true", "is false", "trigger" };
                        int op = (int)cd.op;
                        ImGui::SetNextItemWidth(80);
                        if (ImGui::Combo("##op", &op, ops, 5)) { cd.op = (AnimCondOp)op; g_Changed = true; }
                        if (cd.op == AnimCondOp::Greater || cd.op == AnimCondOp::Less) {
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(60);
                            Mark(ImGui::DragFloat("##th", &cd.threshold, 0.05f));
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("x")) { t.conditions.erase(t.conditions.begin() + c); g_Changed = true; ImGui::PopID(); break; }
                        ImGui::PopID();
                    }
                    if (ImGui::SmallButton("+ Condition")) { t.conditions.push_back({}); g_Changed = true; }
                    ImGui::TreePop();
                }
                ImGui::PopID();
                if (del) { a.transitions.erase(a.transitions.begin() + i); g_Changed = true; break; }
            }
            if (ImGui::SmallButton("+ Transition")) { a.transitions.push_back({}); g_Changed = true; }
            ImGui::TreePop();
        }
        BeginProps();
    });

    Section<TerrainComponent>(e, "Terrain", [&](TerrainComponent& t) {
        bool regen = false;
        regen |= PInt("Resolution", t.resolution, 1.0f, 9, 513);
        regen |= PFloat("Size", t.size, 0.5f, 1.0f, 2000.0f);
        regen |= PFloat("Height", t.heightScale, 0.1f, 0.0f, 500.0f);
        regen |= PInt("Seed", t.seed);
        regen |= PFloat("Noise scale", t.noiseScale, 0.0005f, 0.0001f, 1.0f, "%.4f");
        regen |= PInt("Octaves", t.octaves, 0.1f, 1, 8);
        Label("");
        if (ImGui::Button("Generate", ImVec2(-1, 0))) { TerrainSystem::Generate(t); g_Changed = true; }
        Next();
        if (regen) TerrainSystem::Generate(t);
        bool look = false;
        look |= PColor3("Sand", t.sandColor);
        look |= PColor3("Grass", t.grassColor);
        look |= PColor3("Rock", t.rockColor);
        look |= PColor3("Snow", t.snowColor);
        look |= PFloat("Sand height", t.sandHeight, 0.05f);
        look |= PFloat("Snow height", t.snowHeight, 0.05f);
        bool fol = false;
        fol |= PBool("Grass", t.grass);
        fol |= PFloat("Grass density", t.grassDensity, 0.02f, 0.0f, 8.0f);
        fol |= PFloat("Grass min height", t.grassMinHeight, 0.05f);
        fol |= PInt("Trees", t.treeCount, 1.0f, 0, 5000);
        fol |= PAsset("Tree model", t.treeModel, IsModel);
        if (fol) t.foliageDirty = true;
        (void)look;
        EndProps();
        ImGui::SeparatorText("Sculpt");
        ImGui::Checkbox("Sculpt mode", &m_Sculpt);
        if (m_Sculpt) {
            const char* modes[] = { "Raise (Shift = lower)", "Lower", "Smooth", "Flatten" };
            ImGui::Combo("Brush", &m_SculptMode, modes, 4);
            ImGui::SliderFloat("Radius", &m_BrushRadius, 0.5f, 40.0f);
            ImGui::SliderFloat("Strength", &m_BrushStrength, 0.1f, 20.0f);
            ImGui::TextDisabled("Paint with the left mouse button in the viewport.");
        }
        BeginProps();
    });

    Section<WaterComponent>(e, "Water", [&](WaterComponent& w) {
        Label("Size");
        Mark(ImGui::DragFloat2("##size", glm::value_ptr(w.size), 0.5f, 1.0f, 10000.0f));
        Next();
        PColor4("Shallow", w.shallowColor);
        PColor4("Deep", w.deepColor);
        PFloat("Wave height", w.waveHeight, 0.005f, 0.0f, 5.0f);
        PFloat("Wave speed", w.waveSpeed, 0.01f, 0.0f, 10.0f);
        PFloat("Wave scale", w.waveScale, 0.005f, 0.001f, 5.0f);
    });

    Section<ParticleEmitterComponent>(e, "Particle Emitter", [&](ParticleEmitterComponent& p) {
        Label("Presets");
        struct P { const char* n; void (*fn)(ParticleEmitterComponent&); };
        P presets[] = { { "Fire", ParticlePresets::Fire }, { "Smoke", ParticlePresets::Smoke }, { "Sparks", ParticlePresets::Sparks },
                        { "Magic", ParticlePresets::Magic }, { "Rain", ParticlePresets::Rain }, { "Dust", ParticlePresets::Dust } };
        for (int i = 0; i < 6; i++) {
            if (i) ImGui::SameLine(0, 2);
            if (ImGui::SmallButton(presets[i].n)) { auto rt = p.runtime; presets[i].fn(p); p.runtime = rt; if (rt) rt->Clear(); g_Changed = true; }
        }
        Next();
        PBool("Playing", p.playing);
        PBool("Play on start", p.playOnStart);
        PBool("Loop", p.loop);
        PFloat("Rate", p.rate, 0.5f, 0.0f, 5000.0f);
        PInt("Burst", p.burst, 1.0f, 0, 10000);
        PInt("Max particles", p.maxParticles, 10.0f, 1, 100000);
        PFloat("Lifetime", p.lifetime, 0.01f, 0.01f, 60.0f);
        PFloat("Lifetime +/-", p.lifetimeVariance, 0.01f, 0.0f, 60.0f);
        const char* shapes[] = { "Point", "Sphere", "Box" };
        int s = (int)p.shape;
        if (PCombo("Shape", s, shapes, 3)) p.shape = (ParticleShape)s;
        if (p.shape != ParticleShape::Point) PVec3("Shape size", p.shapeSize, 1.0f, 0.02f);
        PVec3("Velocity", p.velocity);
        PFloat("Spread", p.spread, 0.01f, 0.0f, 100.0f);
        PVec3("Gravity", p.gravity);
        PFloat("Drag", p.drag, 0.01f, 0.0f, 50.0f);
        PColor4("Start color", p.startColor);
        PColor4("End color", p.endColor);
        PFloat("Start size", p.startSize, 0.005f, 0.0f, 50.0f);
        PFloat("End size", p.endSize, 0.005f, 0.0f, 50.0f);
        PBool("Additive", p.additive);
        PBool("World space", p.worldSpace);
        Label("");
        if (ImGui::Button("Restart")) { if (p.runtime) p.runtime->Clear(); p.playing = true; }
        if (p.runtime) { ImGui::SameLine(); ImGui::TextDisabled("%zu alive", p.runtime->particles.size()); }
        Next();
    });

    Section<UIElementComponent>(e, "UI Element", [&](UIElementComponent& u) {
        const char* types[] = { "Panel", "Text", "Button", "Progress Bar", "Image" };
        int t = (int)u.type;
        if (PCombo("Type", t, types, 5)) u.type = (UIType)t;
        const char* anchors[] = { "Top Left", "Top", "Top Right", "Left", "Center", "Right", "Bottom Left", "Bottom", "Bottom Right" };
        int a = (int)u.anchor;
        if (PCombo("Anchor", a, anchors, 9)) u.anchor = (UIAnchor)a;
        Label("Position"); Mark(ImGui::DragFloat2("##pos", glm::value_ptr(u.position), 1.0f)); Next();
        Label("Size"); Mark(ImGui::DragFloat2("##size", glm::value_ptr(u.size), 1.0f, 0.0f, 4000.0f)); Next();
        PColor4("Color", u.color);
        if (u.type == UIType::Text || u.type == UIType::Button) {
            Label("Text");
            Mark(ImGui::InputTextMultiline("##text", &u.text, ImVec2(-1, 60)));
            Next();
            PColor4("Text color", u.textColor);
            PFloat("Font size", u.fontSize, 0.2f, 6.0f, 200.0f);
        }
        if (u.type == UIType::ProgressBar) PFloat("Value", u.value, 0.005f, 0.0f, 1.0f);
        if (u.type == UIType::Image) PAsset("Image", u.image, IsTexture);
        PBool("Visible", u.visible);
        PInt("Layer", u.layer, 0.1f);
        if (u.type == UIType::Button) { Label(""); ImGui::TextDisabled("Clicks call OnUIButton(\"%s\")", e.Name().c_str()); Next(); }
    });

    Section<NavAgentComponent>(e, "Nav Agent", [&](NavAgentComponent& n) {
        PFloat("Speed", n.speed, 0.05f, 0.0f, 100.0f);
        PFloat("Turn speed", n.turnSpeed, 1.0f, 0.0f, 5000.0f);
        PFloat("Stopping dist", n.stoppingDistance, 0.01f, 0.0f, 50.0f);
        PFloat("Repath every", n.repathInterval, 0.01f, 0.05f, 10.0f);
        PBool("Snap to ground", n.snapToGround);
        PFloat("Base offset", n.baseOffset, 0.01f);
        if (playing) { Label("State"); ImGui::Text(n.hasDestination ? "moving (%zu waypoints)" : "idle", n.path.size()); Next(); }
    });

    Section<NetworkIdentityComponent>(e, "Network Identity", [&](NetworkIdentityComponent& n) {
        int owner = (int)n.owner;
        if (PInt("Owner peer", owner, 0.1f, 0, 64)) n.owner = (uint32_t)owner;
        PBool("Sync transform", n.syncTransform);
        PBool("Interpolate", n.interpolate);
    });

    Section<SaveableComponent>(e, "Saveable", [&](SaveableComponent& s) {
        PBool("Transform", s.saveTransform);
        PBool("Script fields", s.saveScripts);
        PBool("Active state", s.saveActive);
    });

    ImGui::Separator();
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) ImGui::OpenPopup("AddComponent");
    DrawAddComponentMenu(e);

    if (g_Changed) {
        m_EditedThisInteraction = true;
        if (auto* t = e.TryGet<TerrainComponent>()) {
            // Terrain edits rebuild the mesh/foliage immediately.
            t->dirty = true;
            t->foliageDirty = true;
        }
    }
}

void EditorApp::DrawAddComponentMenu(Entity e) {
    if (!ImGui::BeginPopup("AddComponent")) return;
    auto item = [&](const char* name, auto tag) {
        using T = typename decltype(tag)::type;
        if (e.Has<T>()) return;
        if (ImGui::MenuItem(name)) { e.Add<T>(); m_EditedThisInteraction = true; }
    };
    struct Mesh_ { using type = MeshRendererComponent; };
    struct Cam_ { using type = CameraComponent; };
    struct Light_ { using type = LightComponent; };
    struct RB_ { using type = RigidBodyComponent; };
    struct Box_ { using type = BoxColliderComponent; };
    struct Sph_ { using type = SphereColliderComponent; };
    struct Scr_ { using type = ScriptComponent; };
    struct AS_ { using type = AudioSourceComponent; };
    struct AL_ { using type = AudioListenerComponent; };
    struct An_ { using type = AnimatorComponent; };
    struct Ter_ { using type = TerrainComponent; };
    struct Wat_ { using type = WaterComponent; };
    struct Par_ { using type = ParticleEmitterComponent; };
    struct UI_ { using type = UIElementComponent; };
    struct Nav_ { using type = NavAgentComponent; };
    struct Net_ { using type = NetworkIdentityComponent; };
    struct Sav_ { using type = SaveableComponent; };
    ImGui::TextDisabled("Rendering");
    item("Mesh Renderer", Mesh_{}); item("Camera", Cam_{}); item("Light", Light_{}); item("Particle Emitter", Par_{});
    ImGui::TextDisabled("Physics");
    item("Rigid Body", RB_{}); item("Box Collider", Box_{}); item("Sphere Collider", Sph_{});
    ImGui::TextDisabled("Gameplay");
    item("Scripts", Scr_{}); item("Animator", An_{}); item("Nav Agent", Nav_{}); item("Saveable", Sav_{}); item("Network Identity", Net_{});
    ImGui::TextDisabled("Audio");
    item("Audio Source", AS_{}); item("Audio Listener", AL_{});
    ImGui::TextDisabled("World & UI");
    item("Terrain", Ter_{}); item("Water", Wat_{}); item("UI Element", UI_{});
    ImGui::EndPopup();
}

} // namespace nd::editor
