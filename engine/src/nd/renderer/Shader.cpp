#include "nd/renderer/Shader.h"
#include "nd/renderer/GL.h"
#include "nd/core/FileSystem.h"
#include "nd/core/Log.h"
#include <sstream>

namespace nd {

static std::string ResolveIncludes(const std::string& src, const fs::path& dir, int depth = 0) {
    if (depth > 8) return src;
    std::stringstream in(src), out;
    std::string line;
    while (std::getline(in, line)) {
        auto pos = line.find("#include");
        if (pos != std::string::npos && line.find_first_not_of(" \t") == pos) {
            auto a = line.find('"'), b = line.rfind('"');
            if (a != std::string::npos && b > a) {
                fs::path inc = dir / line.substr(a + 1, b - a - 1);
                auto text = FileSystem::ReadText(inc);
                if (text) { out << ResolveIncludes(*text, inc.parent_path(), depth + 1) << "\n"; continue; }
                ND_ERROR("Shader include not found: {}", inc.string());
            }
        }
        out << line << "\n";
    }
    return out.str();
}

static uint32_t Compile(GLenum type, const std::string& src, const std::string& path) {
    uint32_t s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        ND_ERROR("Shader compile error in {} ({}):\n{}", path, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
    }
    return s;
}

Ref<Shader> Shader::FromFile(const std::string& path, const std::vector<std::string>& defines) {
    fs::path full = Project::EngineAssetsDir() / "shaders" / path;
    auto text = FileSystem::ReadText(full);
    if (!text) {
        ND_ERROR("Shader not found: {}", full.string());
        return nullptr;
    }
    std::string src = ResolveIncludes(*text, full.parent_path());

    std::string header = "#version 330 core\n";
    for (auto& d : defines) header += "#define " + d + "\n";

    // Split into stages
    std::unordered_map<std::string, std::string> stages;
    std::string current;
    std::stringstream in(src);
    std::string line;
    std::string common;
    while (std::getline(in, line)) {
        if (line.rfind("#type", 0) == 0) {
            current = line.substr(6);
            while (!current.empty() && (current.back() == '\r' || current.back() == ' ')) current.pop_back();
            continue;
        }
        if (current.empty()) common += line + "\n";
        else stages[current] += line + "\n";
    }
    if (!stages.count("vertex") || !stages.count("fragment")) {
        ND_ERROR("Shader {} needs '#type vertex' and '#type fragment' sections", path);
        return nullptr;
    }

    auto shader = MakeRef<Shader>();
    shader->m_Path = path;
    uint32_t vs = Compile(GL_VERTEX_SHADER, header + "#define VERTEX_SHADER\n" + common + stages["vertex"], path);
    uint32_t fsh = Compile(GL_FRAGMENT_SHADER, header + "#define FRAGMENT_SHADER\n" + common + stages["fragment"], path);
    shader->m_Program = glCreateProgram();
    glAttachShader(shader->m_Program, vs);
    glAttachShader(shader->m_Program, fsh);
    glLinkProgram(shader->m_Program);
    int ok = 0;
    glGetProgramiv(shader->m_Program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(shader->m_Program, sizeof(log), nullptr, log);
        ND_ERROR("Shader link error in {}:\n{}", path, log);
    }
    glDeleteShader(vs);
    glDeleteShader(fsh);
    return shader;
}

Shader::~Shader() { if (m_Program && g_GLContextAlive) glDeleteProgram(m_Program); }
void Shader::Bind() const { glUseProgram(m_Program); }

int Shader::Location(const char* name) {
    auto it = m_Locations.find(name);
    if (it != m_Locations.end()) return it->second;
    int loc = glGetUniformLocation(m_Program, name);
    m_Locations[name] = loc;
    return loc;
}

void Shader::SetInt(const char* n, int v) { glUniform1i(Location(n), v); }
void Shader::SetFloat(const char* n, float v) { glUniform1f(Location(n), v); }
void Shader::SetVec2(const char* n, const vec2& v) { glUniform2fv(Location(n), 1, glm::value_ptr(v)); }
void Shader::SetVec3(const char* n, const vec3& v) { glUniform3fv(Location(n), 1, glm::value_ptr(v)); }
void Shader::SetVec4(const char* n, const vec4& v) { glUniform4fv(Location(n), 1, glm::value_ptr(v)); }
void Shader::SetMat4(const char* n, const mat4& m) { glUniformMatrix4fv(Location(n), 1, GL_FALSE, glm::value_ptr(m)); }
void Shader::SetVec3Array(const char* n, const vec3* v, int count) { glUniform3fv(Location(n), count, glm::value_ptr(v[0])); }
void Shader::SetVec4Array(const char* n, const vec4* v, int count) { glUniform4fv(Location(n), count, glm::value_ptr(v[0])); }
void Shader::SetFloatArray(const char* n, const float* v, int count) { glUniform1fv(Location(n), count, v); }
void Shader::SetMat4Array(const char* n, const mat4* m, int count) { glUniformMatrix4fv(Location(n), count, GL_FALSE, glm::value_ptr(m[0])); }

} // namespace nd
