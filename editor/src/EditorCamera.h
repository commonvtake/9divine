#pragma once
#include <euphoria/core/Base.h>
#include <euphoria/renderer/Renderer.h>

namespace eu::editor {

// Scene-view camera:  RMB + WASD/QE fly, Alt+LMB orbit, MMB pan, wheel zoom, F to focus.
// In 2D scenes it looks straight at the XY plane: RMB/MMB drag pans, the wheel zooms at the cursor.
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

    // 2D: pivot.xy is the view's center, zoom2D the visible height in world units
    bool mode2D = false;
    float zoom2D = 14.0f;
    vec2 viewportPixels{ 1280.0f, 720.0f };   // set by the viewport each frame
    vec2 mouseNdc{ 0.0f };                      // cursor in the viewport, -1..1

private:
    quat Orientation() const;
};

} // namespace eu::editor
