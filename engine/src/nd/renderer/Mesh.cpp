#include "nd/renderer/Mesh.h"
#include "nd/renderer/GL.h"
#include "nd/core/Profiler.h"
#include <cmath>

namespace nd {

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, bool dynamic)
    : m_Vertices(vertices), m_Indices(indices) {
    for (auto& v : vertices) {
        m_Bounds.Expand(v.position);
        if (v.weights.x + v.weights.y + v.weights.z + v.weights.w > 0.0f) m_Skinned = true;
    }
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_IBO);
    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, joints));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, weights));
    glBindVertexArray(0);
}

Mesh::~Mesh() {
    if (!g_GLContextAlive) return;
    glDeleteBuffers(1, &m_VBO);
    glDeleteBuffers(1, &m_IBO);
    glDeleteVertexArrays(1, &m_VAO);
}

void Mesh::Draw() const {
    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, (GLsizei)m_Indices.size(), GL_UNSIGNED_INT, nullptr);
    Profiler::AddDrawCall(m_Indices.size() / 3);
}

void Mesh::DrawInstanced(int count) const {
    if (count <= 0) return;
    glBindVertexArray(m_VAO);
    glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)m_Indices.size(), GL_UNSIGNED_INT, nullptr, count);
    Profiler::AddDrawCall((uint64_t)(m_Indices.size() / 3) * count);
}

void Mesh::Update(const std::vector<Vertex>& vertices) {
    m_Vertices = vertices;
    m_Bounds = AABB();
    for (auto& v : vertices) m_Bounds.Expand(v.position);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(Vertex), vertices.data());
}

void Mesh::AttachInstanceBuffer(uint32_t vbo) {
    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(5 + i);
        glVertexAttribPointer(5 + i, 4, GL_FLOAT, GL_FALSE, sizeof(mat4), (void*)(sizeof(vec4) * i));
        glVertexAttribDivisor(5 + i, 1);
    }
    glBindVertexArray(0);
}

void Mesh::ComputeNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    for (auto& v : vertices) v.normal = vec3(0.0f);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        Vertex& a = vertices[indices[i]];
        Vertex& b = vertices[indices[i + 1]];
        Vertex& c = vertices[indices[i + 2]];
        vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += n; b.normal += n; c.normal += n;
    }
    for (auto& v : vertices) {
        float l = glm::length(v.normal);
        v.normal = l > 1e-8f ? v.normal / l : vec3(0, 1, 0);
    }
}

// ---------------------------------------------------------------- Primitives
namespace Primitives {

Ref<Mesh> Cube() {
    static Ref<Mesh> mesh;
    if (mesh) return mesh;
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    const vec3 normals[6] = { {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0} };
    for (int f = 0; f < 6; f++) {
        vec3 n = normals[f];
        vec3 up = std::abs(n.y) > 0.5f ? vec3(0, 0, -n.y) : vec3(0, 1, 0);
        vec3 right = glm::cross(up, n);
        uint32_t base = (uint32_t)v.size();
        vec2 uvs[4] = { {0,0}, {1,0}, {1,1}, {0,1} };
        vec3 corners[4] = { n - right - up, n + right - up, n + right + up, n - right + up };
        for (int i = 0; i < 4; i++) {
            Vertex vert;
            vert.position = corners[i] * 0.5f;
            vert.normal = n;
            vert.uv = uvs[i];
            v.push_back(vert);
        }
        idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
    mesh = MakeRef<Mesh>(v, idx);
    return mesh;
}

Ref<Mesh> Sphere(int segments, int rings) {
    static Ref<Mesh> mesh;
    if (mesh) return mesh;
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    for (int r = 0; r <= rings; r++) {
        float phi = PI * (float)r / rings;
        for (int s = 0; s <= segments; s++) {
            float theta = 2.0f * PI * (float)s / segments;
            vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            Vertex vert;
            vert.position = n * 0.5f;
            vert.normal = n;
            vert.uv = { (float)s / segments, 1.0f - (float)r / rings };
            v.push_back(vert);
        }
    }
    for (int r = 0; r < rings; r++)
        for (int s = 0; s < segments; s++) {
            uint32_t a = r * (segments + 1) + s, b = a + segments + 1;
            idx.insert(idx.end(), { a, a + 1, b, b, a + 1, b + 1 });
        }
    mesh = MakeRef<Mesh>(v, idx);
    return mesh;
}

Ref<Mesh> Plane(float size, int sub) {
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    for (int z = 0; z <= sub; z++)
        for (int x = 0; x <= sub; x++) {
            Vertex vert;
            vert.position = { ((float)x / sub - 0.5f) * size, 0.0f, ((float)z / sub - 0.5f) * size };
            vert.normal = { 0, 1, 0 };
            vert.uv = { (float)x / sub * size * 0.5f, (float)z / sub * size * 0.5f };
            v.push_back(vert);
        }
    for (int z = 0; z < sub; z++)
        for (int x = 0; x < sub; x++) {
            uint32_t a = z * (sub + 1) + x, b = a + sub + 1;
            idx.insert(idx.end(), { a, b, a + 1, a + 1, b, b + 1 });
        }
    return MakeRef<Mesh>(v, idx);
}

static void AddCap(std::vector<Vertex>& v, std::vector<uint32_t>& idx, int segments, float y, float radius, bool top) {
    uint32_t center = (uint32_t)v.size();
    Vertex c;
    c.position = { 0, y, 0 };
    c.normal = { 0, top ? 1.0f : -1.0f, 0 };
    c.uv = { 0.5f, 0.5f };
    v.push_back(c);
    for (int s = 0; s <= segments; s++) {
        float t = 2.0f * PI * s / segments;
        Vertex e;
        e.position = { std::cos(t) * radius, y, std::sin(t) * radius };
        e.normal = c.normal;
        e.uv = { std::cos(t) * 0.5f + 0.5f, std::sin(t) * 0.5f + 0.5f };
        v.push_back(e);
    }
    for (int s = 0; s < segments; s++) {
        uint32_t a = center + 1 + s, b = a + 1;
        if (top) idx.insert(idx.end(), { center, b, a });
        else idx.insert(idx.end(), { center, a, b });
    }
}

Ref<Mesh> Cylinder(int segments) {
    static Ref<Mesh> mesh;
    if (mesh) return mesh;
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    for (int s = 0; s <= segments; s++) {
        float t = 2.0f * PI * s / segments;
        vec3 n(std::cos(t), 0, std::sin(t));
        for (int k = 0; k < 2; k++) {
            Vertex vert;
            vert.position = vec3(n.x * 0.5f, k == 0 ? -0.5f : 0.5f, n.z * 0.5f);
            vert.normal = n;
            vert.uv = { (float)s / segments, (float)k };
            v.push_back(vert);
        }
    }
    for (int s = 0; s < segments; s++) {
        uint32_t a = s * 2, b = a + 2;
        idx.insert(idx.end(), { a, a + 1, b, b, a + 1, b + 1 });
    }
    AddCap(v, idx, segments, 0.5f, 0.5f, true);
    AddCap(v, idx, segments, -0.5f, 0.5f, false);
    mesh = MakeRef<Mesh>(v, idx);
    return mesh;
}

Ref<Mesh> Cone(int segments) {
    static Ref<Mesh> mesh;
    if (mesh) return mesh;
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    float slope = 0.5f / 1.0f;
    for (int s = 0; s <= segments; s++) {
        float t = 2.0f * PI * s / segments;
        vec3 n = glm::normalize(vec3(std::cos(t), slope, std::sin(t)));
        Vertex base, tip;
        base.position = { std::cos(t) * 0.5f, -0.5f, std::sin(t) * 0.5f };
        base.normal = n;
        base.uv = { (float)s / segments, 0 };
        tip.position = { 0, 0.5f, 0 };
        tip.normal = n;
        tip.uv = { (float)s / segments, 1 };
        v.push_back(base);
        v.push_back(tip);
    }
    for (int s = 0; s < segments; s++) {
        uint32_t a = s * 2, b = a + 2;
        idx.insert(idx.end(), { a, a + 1, b });
    }
    AddCap(v, idx, segments, -0.5f, 0.5f, false);
    mesh = MakeRef<Mesh>(v, idx);
    return mesh;
}

Ref<Mesh> Capsule(int segments, int rings) {
    static Ref<Mesh> mesh;
    if (mesh) return mesh;
    // Unity-style capsule: radius 0.5, total height 2.
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    int totalRings = rings * 2 + 1;
    for (int r = 0; r <= totalRings; r++) {
        float phi, yOffset;
        if (r <= rings) { phi = (PI * 0.5f) * r / rings; yOffset = 0.5f; }
        else { phi = PI * 0.5f + (PI * 0.5f) * (r - rings - 1) / rings; yOffset = -0.5f; }
        for (int s = 0; s <= segments; s++) {
            float theta = 2.0f * PI * s / segments;
            vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            Vertex vert;
            vert.position = n * 0.5f + vec3(0, yOffset, 0);
            vert.normal = n;
            vert.uv = { (float)s / segments, 1.0f - (vert.position.y + 1.0f) * 0.5f };
            v.push_back(vert);
        }
    }
    for (int r = 0; r < totalRings; r++)
        for (int s = 0; s < segments; s++) {
            uint32_t a = r * (segments + 1) + s, b = a + segments + 1;
            idx.insert(idx.end(), { a, a + 1, b, b, a + 1, b + 1 });
        }
    mesh = MakeRef<Mesh>(v, idx);
    return mesh;
}

Ref<Mesh> Quad() {
    static Ref<Mesh> mesh;
    if (mesh) return mesh;
    std::vector<Vertex> v(4);
    v[0].position = { -0.5f, -0.5f, 0 }; v[0].uv = { 0, 0 };
    v[1].position = { 0.5f, -0.5f, 0 };  v[1].uv = { 1, 0 };
    v[2].position = { 0.5f, 0.5f, 0 };   v[2].uv = { 1, 1 };
    v[3].position = { -0.5f, 0.5f, 0 };  v[3].uv = { 0, 1 };
    for (auto& x : v) x.normal = { 0, 0, 1 };
    mesh = MakeRef<Mesh>(v, std::vector<uint32_t>{ 0, 1, 2, 0, 2, 3 });
    return mesh;
}

const std::vector<std::string>& Names() {
    static std::vector<std::string> names = { "cube", "sphere", "plane", "cylinder", "cone", "capsule", "quad" };
    return names;
}

Ref<Mesh> ByName(const std::string& name) {
    if (name == "cube") return Cube();
    if (name == "sphere") return Sphere();
    if (name == "plane") { static Ref<Mesh> p = Plane(1.0f, 1); return p; }
    if (name == "cylinder") return Cylinder();
    if (name == "cone") return Cone();
    if (name == "capsule") return Capsule();
    if (name == "quad") return Quad();
    return nullptr;
}

} // namespace Primitives
} // namespace nd
