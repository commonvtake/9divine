#pragma once
// Common includes, math types and small helpers used everywhere in the engine.

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

namespace nd {

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using ivec2 = glm::ivec2;
using ivec4 = glm::ivec4;
using quat = glm::quat;
using mat3 = glm::mat3;
using mat4 = glm::mat4;

template <typename T> using Ref = std::shared_ptr<T>;
template <typename T> using Scope = std::unique_ptr<T>;
template <typename T, typename... Args> Ref<T> MakeRef(Args&&... args) { return std::make_shared<T>(std::forward<Args>(args)...); }
template <typename T, typename... Args> Scope<T> MakeScope(Args&&... args) { return std::make_unique<T>(std::forward<Args>(args)...); }

constexpr float PI = 3.14159265358979323846f;

struct AABB {
    vec3 min{ 1e30f };
    vec3 max{ -1e30f };
    void Expand(const vec3& p) { min = glm::min(min, p); max = glm::max(max, p); }
    void Expand(const AABB& b) { min = glm::min(min, b.min); max = glm::max(max, b.max); }
    bool Valid() const { return min.x <= max.x; }
    vec3 Center() const { return (min + max) * 0.5f; }
    vec3 Extents() const { return (max - min) * 0.5f; }
};

struct Ray {
    vec3 origin{ 0.0f };
    vec3 direction{ 0.0f, 0.0f, -1.0f };
};

inline mat4 ComposeTRS(const vec3& t, const quat& r, const vec3& s) {
    return glm::translate(mat4(1.0f), t) * glm::toMat4(r) * glm::scale(mat4(1.0f), s);
}

inline void DecomposeTRS(const mat4& m, vec3& t, quat& r, vec3& s) {
    t = vec3(m[3]);
    s = vec3(glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2])));
    mat3 rot(vec3(m[0]) / (s.x > 0 ? s.x : 1.0f), vec3(m[1]) / (s.y > 0 ? s.y : 1.0f), vec3(m[2]) / (s.z > 0 ? s.z : 1.0f));
    r = glm::normalize(glm::quat_cast(rot));
}

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float MoveTowards(float cur, float target, float maxDelta) {
    if (std::abs(target - cur) <= maxDelta) return target;
    return cur + (target > cur ? maxDelta : -maxDelta);
}

} // namespace nd
