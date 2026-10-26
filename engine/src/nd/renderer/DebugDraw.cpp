#include "nd/renderer/DebugDraw.h"
#include <cmath>

namespace nd {

std::vector<DebugDraw::LineVertex> DebugDraw::s_Lines;
bool DebugDraw::enabled = true;

void DebugDraw::Line(const vec3& a, const vec3& b, const vec4& c) {
    if (!enabled || s_Lines.size() > 400000) return;
    s_Lines.push_back({ a, c });
    s_Lines.push_back({ b, c });
}

void DebugDraw::Ray(const vec3& o, const vec3& d, const vec4& c) { Line(o, o + d, c); }

void DebugDraw::Box(const vec3& center, const vec3& h, const quat& r, const vec4& c) {
    vec3 p[8];
    for (int i = 0; i < 8; i++) {
        vec3 l((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
        p[i] = center + r * l;
    }
    const int e[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
    for (auto& edge : e) Line(p[edge[0]], p[edge[1]], c);
}

void DebugDraw::AABB(const nd::AABB& b, const vec4& c) { Box(b.Center(), b.Extents(), quat(1, 0, 0, 0), c); }

void DebugDraw::Circle(const vec3& center, float radius, const vec3& normal, const vec4& c, int seg) {
    vec3 n = glm::normalize(normal);
    vec3 t = std::abs(n.y) < 0.99f ? glm::normalize(glm::cross(n, vec3(0, 1, 0))) : vec3(1, 0, 0);
    vec3 b = glm::cross(n, t);
    vec3 prev = center + t * radius;
    for (int i = 1; i <= seg; i++) {
        float a = 2.0f * PI * i / seg;
        vec3 p = center + (t * std::cos(a) + b * std::sin(a)) * radius;
        Line(prev, p, c);
        prev = p;
    }
}

void DebugDraw::Sphere(const vec3& center, float radius, const vec4& c, int seg) {
    Circle(center, radius, { 1, 0, 0 }, c, seg);
    Circle(center, radius, { 0, 1, 0 }, c, seg);
    Circle(center, radius, { 0, 0, 1 }, c, seg);
}

void DebugDraw::Cross(const vec3& p, float s, const vec4& c) {
    Line(p - vec3(s, 0, 0), p + vec3(s, 0, 0), c);
    Line(p - vec3(0, s, 0), p + vec3(0, s, 0), c);
    Line(p - vec3(0, 0, s), p + vec3(0, 0, s), c);
}

void DebugDraw::Grid(const vec3& center, float size, float spacing, const vec4& c) {
    int n = (int)(size / spacing);
    float half = n * spacing * 0.5f;
    for (int i = 0; i <= n; i++) {
        float o = -half + i * spacing;
        vec4 col = (std::abs(o) < spacing * 0.01f) ? vec4(c.x, c.y, c.z, c.w * 2.0f) : c;
        Line(center + vec3(o, 0, -half), center + vec3(o, 0, half), col);
        Line(center + vec3(-half, 0, o), center + vec3(half, 0, o), col);
    }
}

void DebugDraw::Path(const std::vector<vec3>& pts, const vec4& c) {
    for (size_t i = 1; i < pts.size(); i++) Line(pts[i - 1], pts[i], c);
    for (auto& p : pts) Cross(p, 0.1f, c);
}

} // namespace nd
