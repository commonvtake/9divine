#pragma once
#include "nd/core/Base.h"

namespace nd {

// Immediate-mode debug lines. Call from anywhere during a frame; the renderer draws and
// clears them at the end of the scene pass. Great for visualising AI, raycasts, bounds.
class DebugDraw {
public:
    struct LineVertex { vec3 position; vec4 color; };

    static void Line(const vec3& a, const vec3& b, const vec4& color = vec4(1.0f));
    static void Ray(const vec3& origin, const vec3& dir, const vec4& color = vec4(1, 1, 0, 1));
    static void Box(const vec3& center, const vec3& halfExtents, const quat& rotation = quat(1, 0, 0, 0), const vec4& color = vec4(1.0f));
    static void AABB(const nd::AABB& box, const vec4& color = vec4(1.0f));
    static void Sphere(const vec3& center, float radius, const vec4& color = vec4(1.0f), int segments = 24);
    static void Circle(const vec3& center, float radius, const vec3& normal, const vec4& color = vec4(1.0f), int segments = 32);
    static void Cross(const vec3& p, float size, const vec4& color = vec4(1.0f));
    static void Grid(const vec3& center, float size, float spacing, const vec4& color);
    static void Path(const std::vector<vec3>& points, const vec4& color = vec4(0.2f, 1.0f, 0.6f, 1.0f));

    static std::vector<LineVertex>& Lines() { return s_Lines; }
    static void Clear() { s_Lines.clear(); }
    static bool enabled;

private:
    static std::vector<LineVertex> s_Lines;
};

} // namespace nd
