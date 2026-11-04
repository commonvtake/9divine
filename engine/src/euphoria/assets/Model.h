#pragma once
#include <cmath>
#include "euphoria/core/Base.h"
#include "euphoria/renderer/Mesh.h"
#include "euphoria/renderer/Texture.h"
#include "euphoria/animation/Animation.h"

namespace eu {

// Converts an old Blinn-Phong shininess exponent to a GGX roughness with roughly the same
// highlight size (alpha^2 = 2 / (n + 2), roughness = sqrt(alpha)).
inline float RoughnessFromShininess(float shininess) {
    return glm::clamp(std::pow(2.0f / (std::max(shininess, 0.0f) + 2.0f), 0.25f), 0.03f, 1.0f);
}

struct MaterialData {
    std::string name;
    vec4 baseColor{ 1.0f };
    Ref<Texture2D> albedo;
    float metallic = 0.0f;
    float roughness = 0.5f;
    Ref<Texture2D> normalMap;           // tangent-space, OpenGL (+Y up) convention as in glTF
    float normalScale = 1.0f;
    Ref<Texture2D> metallicRoughnessMap; // glTF packing: G = roughness, B = metallic
    Ref<Texture2D> occlusionMap;        // R = ambient occlusion
    vec3 emissive{ 0.0f };
    bool doubleSided = false;
};

struct SubMesh {
    Ref<Mesh> mesh;
    int material = -1;
};

// A loaded model file (glTF / GLB / OBJ / FBX) or a built-in primitive.
class Model {
public:
    std::string path;
    std::vector<SubMesh> meshes;
    std::vector<MaterialData> materials;
    Ref<Skeleton> skeleton;
    std::vector<Ref<AnimationClip>> clips;
    AABB bounds;

    bool IsSkinned() const { return skeleton && !skeleton->joints.empty(); }
    const AnimationClip* FindClip(const std::string& name) const {
        for (auto& c : clips) if (c->name == name) return c.get();
        return nullptr;
    }
};

} // namespace eu
