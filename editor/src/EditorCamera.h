#pragma once
#include <nd/core/Base.h>
#include <nd/renderer/Renderer.h>

namespace nd::editor {

// Scene-view camera:  RMB + WASD/QE fly, Alt+LMB orbit, MMB pan, wheel zoom, F to focus.
class EditorCamera {
public:
    void Update(float dt, bool hovered, bool focused);
    void Focus(const vec3& point, float distance = 8.0f);
    RenderCamera GetRenderCamera(float aspect) const;
    Ray ScreenRay(const vec2& ndc, float aspect) const;

    vec3 Position() const;
    vec3 Forward() const;
    vec3 Right() const;
    vec3 Up() const;

    vec3 pivot{ 0.0f, 2.0f, 0.0f };
    float distance = 18.0f;
    float yaw = 35.0f;       // degrees
    float pitch = 28.0f;     // degrees
    float fov = 55.0f;
    float flySpeed = 12.0f;
    bool flying = false;

private:
    quat Orientation() const;
};

} // namespace nd::editor
