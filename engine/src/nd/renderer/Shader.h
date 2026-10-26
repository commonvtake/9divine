#pragma once
#include "nd/core/Base.h"
#include <string>
#include <unordered_map>

namespace nd {

// GLSL program loaded from a single file with "#type vertex" / "#type fragment" sections.
// Supports #include "other.glsl" (relative to the shaders folder) and compile-time defines.
class Shader {
public:
    static Ref<Shader> FromFile(const std::string& path, const std::vector<std::string>& defines = {});
    ~Shader();

    void Bind() const;
    uint32_t Id() const { return m_Program; }
    const std::string& Path() const { return m_Path; }

    void SetInt(const char* name, int v);
    void SetFloat(const char* name, float v);
    void SetVec2(const char* name, const vec2& v);
    void SetVec3(const char* name, const vec3& v);
    void SetVec4(const char* name, const vec4& v);
    void SetMat4(const char* name, const mat4& m);
    void SetMat4Array(const char* name, const mat4* m, int count);
    void SetVec3Array(const char* name, const vec3* v, int count);
    void SetVec4Array(const char* name, const vec4* v, int count);
    void SetFloatArray(const char* name, const float* v, int count);
    int Location(const char* name);

private:
    uint32_t m_Program = 0;
    std::string m_Path;
    std::unordered_map<std::string, int> m_Locations;
};

} // namespace nd
