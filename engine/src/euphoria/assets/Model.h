#pragma once
#include <cmath>
#include <algorithm>
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
    Ref<Texture2D> emissiveMap;         // multiplies `emissive`
    bool doubleSided = false;

    // Where each map came from (for the editor's model info): a file path, "embedded", or a
    // "missing: <name>" note when the file referred to a texture that couldn't be found.
    std::vector<std::string> textureNotes;
    std::vector<std::string> sourceFiles;   // absolute paths of the image files used (for importing)
};

struct SubMesh {
    Ref<Mesh> mesh;
    int material = -1;
    std::string node;                   // name of the object this came from in the file
    mat4 nodeWorld{ 1.0f };             // that object's transform (its pivot) in model space
};

// A loaded model file (glTF / GLB / OBJ / FBX, or .blend / .dae through Blender) or a built-in
// primitive. "Models/Kit.fbx#Barrel" refers to the object named Barrel inside Kit.fbx, moved to
// its own pivot, so one file holding a whole kit of props can be placed piece by piece.
class Model {
public:
    std::string path;
    std::string sourceFormat;           // "glTF", "FBX", "OBJ", "Blender (via glTF)", ...
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
    // Names of the separate objects in the file, in file order (empty names are skipped).
    std::vector<std::string> ObjectNames() const {
        std::vector<std::string> names;
        for (auto& sm : meshes)
            if (!sm.node.empty() && std::find(names.begin(), names.end(), sm.node) == names.end()) names.push_back(sm.node);
        return names;
    }
};

} // namespace eu
