#pragma once
#include "nd/core/Base.h"
#include "nd/renderer/Mesh.h"
#include "nd/renderer/Texture.h"
#include "nd/animation/Animation.h"

namespace nd {

struct MaterialData {
    std::string name;
    vec4 baseColor{ 1.0f };
    Ref<Texture2D> albedo;
    float specular = 0.25f;
    float shininess = 32.0f;
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

} // namespace nd
