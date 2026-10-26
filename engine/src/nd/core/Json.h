#pragma once
// nlohmann::json plus conversions for GLM types so components serialize cleanly.
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace nd { using json = nlohmann::json; }

namespace glm {
inline void to_json(nlohmann::json& j, const vec2& v) { j = { v.x, v.y }; }
inline void from_json(const nlohmann::json& j, vec2& v) { v = { j.at(0).get<float>(), j.at(1).get<float>() }; }
inline void to_json(nlohmann::json& j, const vec3& v) { j = { v.x, v.y, v.z }; }
inline void from_json(const nlohmann::json& j, vec3& v) { v = { j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>() }; }
inline void to_json(nlohmann::json& j, const vec4& v) { j = { v.x, v.y, v.z, v.w }; }
inline void from_json(const nlohmann::json& j, vec4& v) { v = { j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(), j.at(3).get<float>() }; }
inline void to_json(nlohmann::json& j, const quat& q) { j = { q.w, q.x, q.y, q.z }; }
inline void from_json(const nlohmann::json& j, quat& q) { q = quat(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(), j.at(3).get<float>()); }
} // namespace glm

namespace nd {

// Safe getter: returns fallback when the key is missing or has the wrong type.
template <typename T>
T JGet(const json& j, const char* key, const T& fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    try { return it->get<T>(); } catch (...) { return fallback; }
}

std::string Base64Encode(const void* data, size_t size);
std::vector<uint8_t> Base64Decode(const std::string& text);

} // namespace nd
