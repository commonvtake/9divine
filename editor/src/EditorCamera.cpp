#include "EditorCamera.h"
#include <nd/core/Input.h>
#include <imgui.h>
#include <cmath>

namespace nd::editor {

quat EditorCamera::Orientation() const {
    return glm::angleAxis(glm::radians(-yaw), vec3(0, 1, 0)) * glm::angleAxis(glm::radians(-pitch), vec3(1, 0, 0));
}
vec3 EditorCamera::Forward() const { return Orientation() * vec3(0, 0, -1); }
vec3 EditorCamera::Right() const { return Orientation() * vec3(1, 0, 0); }
vec3 EditorCamera::Up() const { return Orientation() * vec3(0, 1, 0); }
vec3 EditorCamera::Position() const { return pivot - Forward() * distance; }

void EditorCamera::Focus(const vec3& p, float d) {
    pivot = p;
    distance = d;
}

void EditorCamera::Update(float dt, bool hovered, bool focused) {
    ImGuiIO& io = ImGui::GetIO();
    vec2 delta(io.MouseDelta.x, io.MouseDelta.y);
    bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    bool mmb = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);

    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right))) flying = true;
    if (!rmb) flying = false;

    if (flying) {
        yaw += delta.x * 0.25f;
        pitch = glm::clamp(pitch + delta.y * 0.25f, -89.0f, 89.0f);
        vec3 move(0.0f);
        if (ImGui::IsKeyDown(ImGuiKey_W)) move += Forward();
        if (ImGui::IsKeyDown(ImGuiKey_S)) move -= Forward();
        if (ImGui::IsKeyDown(ImGuiKey_D)) move += Right();
        if (ImGui::IsKeyDown(ImGuiKey_A)) move -= Right();
        if (ImGui::IsKeyDown(ImGuiKey_E)) move += vec3(0, 1, 0);
        if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= vec3(0, 1, 0);
        float speed = flySpeed * (io.KeyShift ? 3.0f : 1.0f);
        if (hovered && io.MouseWheel != 0.0f) flySpeed = glm::clamp(flySpeed * (io.MouseWheel > 0 ? 1.2f : 1.0f / 1.2f), 0.5f, 200.0f);
        // Fly moves the eye; keep the pivot in front of it
        vec3 eye = Position() + move * speed * dt;
        pivot = eye + Forward() * distance;
        return;
    }
    if (!hovered) return;
    if (io.KeyAlt && lmb) {
        yaw += delta.x * 0.3f;
        pitch = glm::clamp(pitch + delta.y * 0.3f, -89.0f, 89.0f);
    } else if (mmb) {
        float k = distance * 0.0018f;
        pivot -= Right() * delta.x * k;
        pivot += Up() * delta.y * k;
    }
    if (io.MouseWheel != 0.0f) distance = glm::clamp(distance * (io.MouseWheel > 0 ? 0.87f : 1.15f), 0.5f, 2000.0f);
    (void)focused;
}

RenderCamera EditorCamera::GetRenderCamera(float aspect) const {
    RenderCamera c;
    c.position = Position();
    c.view = glm::lookAt(c.position, c.position + Forward(), Up());
    c.nearClip = 0.05f;
    c.farClip = 2000.0f;
    c.projection = glm::perspective(glm::radians(fov), aspect, c.nearClip, c.farClip);
    return c;
}

Ray EditorCamera::ScreenRay(const vec2& ndc, float aspect) const {
    RenderCamera c = GetRenderCamera(aspect);
    mat4 inv = glm::inverse(c.projection * c.view);
    vec4 nearP = inv * vec4(ndc, -1.0f, 1.0f);
    vec4 farP = inv * vec4(ndc, 1.0f, 1.0f);
    vec3 a = vec3(nearP) / nearP.w, b = vec3(farP) / farP.w;
    return { a, glm::normalize(b - a) };
}

} // namespace nd::editor
