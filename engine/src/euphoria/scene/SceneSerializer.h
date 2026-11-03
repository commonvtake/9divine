#pragma once
#include "euphoria/scene/Entity.h"
#include "euphoria/core/Json.h"

namespace eu {

// Saves and loads scenes as human-readable JSON (.scene files).
class SceneSerializer {
public:
    explicit SceneSerializer(Scene& scene) : m_Scene(scene) {}

    bool Save(const std::string& projectRelativePath);
    bool Load(const std::string& projectRelativePath);
    json ToJson();
    bool FromJson(const json& j);

    static json SerializeEntity(Entity e);
    // Creates an entity from JSON. If `remap` is given, UUIDs are replaced (duplication).
    static Entity DeserializeEntity(Scene& scene, const json& j, std::unordered_map<uint64_t, uint64_t>* remap = nullptr);

private:
    Scene& m_Scene;
};

} // namespace eu
