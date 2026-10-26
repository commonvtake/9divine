#pragma once
#include "nd/core/Base.h"

namespace nd {

struct Vertex {
    vec3 position{ 0.0f };
    vec3 normal{ 0.0f, 1.0f, 0.0f };
    vec2 uv{ 0.0f };
    ivec4 joints{ 0 };       // skinning: up to 4 bone indices
    vec4 weights{ 0.0f };    // skinning weights
};

// A GPU mesh (VAO + VBO + IBO). CPU copies are kept for physics, picking and terrain editing.
class Mesh {
public:
    Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, bool dynamic = false);
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void Draw() const;
    void DrawInstanced(int count) const;
    void Update(const std::vector<Vertex>& vertices);   // same vertex count (dynamic meshes)

    // Attach a per-instance buffer of mat4 (locations 5..8) for instanced drawing.
    void AttachInstanceBuffer(uint32_t vbo);

    const std::vector<Vertex>& Vertices() const { return m_Vertices; }
    const std::vector<uint32_t>& Indices() const { return m_Indices; }
    const AABB& Bounds() const { return m_Bounds; }
    uint32_t IndexCount() const { return (uint32_t)m_Indices.size(); }
    bool IsSkinned() const { return m_Skinned; }

    static void ComputeNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

private:
    uint32_t m_VAO = 0, m_VBO = 0, m_IBO = 0;
    std::vector<Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    AABB m_Bounds;
    bool m_Skinned = false;
};

// Procedural shapes used by "builtin:" mesh paths.
namespace Primitives {
Ref<Mesh> Cube();
Ref<Mesh> Sphere(int segments = 32, int rings = 16);
Ref<Mesh> Plane(float size = 10.0f, int subdivisions = 1);
Ref<Mesh> Cylinder(int segments = 24);
Ref<Mesh> Cone(int segments = 24);
Ref<Mesh> Capsule(int segments = 24, int rings = 8);
Ref<Mesh> Quad();             // 1x1 XY quad (billboards / grass)
Ref<Mesh> ByName(const std::string& name); // "cube", "sphere", ...
const std::vector<std::string>& Names();
}

} // namespace nd
