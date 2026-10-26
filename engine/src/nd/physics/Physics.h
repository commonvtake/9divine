#pragma once
#include "nd/core/Base.h"
#include "nd/scene/Entity.h"
#include <set>

namespace nd {

struct RaycastHit {
    Entity entity;           // invalid when the terrain was hit
    bool hitTerrain = false;
    vec3 point{ 0.0f };
    vec3 normal{ 0.0f, 1.0f, 0.0f };
    float distance = 0.0f;
};

struct CollisionEvent {
    enum Type { CollisionEnter, CollisionExit, TriggerEnter, TriggerExit } type;
    EntityId a, b;
};

// Impulse-based rigid body physics.
//  * Colliders: spheres and boxes (boxes are oriented when colliding with spheres and rays;
//    box-vs-box uses their world-space bounding boxes).
//  * Heightfield collision against TerrainComponent.
//  * Bodies don't rotate from collisions (good for characters/props; see README roadmap).
class PhysicsWorld {
public:
    explicit PhysicsWorld(Scene* scene) : m_Scene(scene) {}

    void Step(float dt);
    void Reset() { m_Contacts.clear(); m_Events.clear(); }

    bool Raycast(const Ray& ray, float maxDistance, RaycastHit& hit, EntityId ignore = NullEntity, bool includeTriggers = false);
    std::vector<Entity> OverlapSphere(const vec3& center, float radius, bool includeTriggers = true);
    bool TerrainHeight(const vec3& worldPos, float& height, vec3* normal = nullptr);

    const std::vector<CollisionEvent>& Events() const { return m_Events; }
    void ClearEvents() { m_Events.clear(); }
    uint32_t BodyCount() const { return m_BodyCount; }

    void DebugDraw();

    // Batch queries (e.g. nav baking): gather colliders once instead of per query.
    void BeginBatchQueries() { Gather(); m_Batch = true; }
    void EndBatchQueries() { m_Batch = false; }

    int solverIterations = 4;

private:
    struct Body {
        EntityId id;
        bool sphere;
        vec3 center;
        vec3 half;          // box half extents (world scale)
        quat rot;           // box orientation
        float radius;
        bool trigger;
        float invMass;
        bool movable;       // dynamic
        RigidBodyComponent* rb;
        TransformComponent* tf;
        vec3 offsetWorld;   // collider center - transform position
        AABB bounds;
    };
    void Gather();
    void ComputeBounds(Body& b);
    bool Collide(const Body& a, const Body& b, vec3& normal, float& depth);
    void Resolve(Body& a, Body& b, const vec3& n, float depth);
    void CollideTerrain(Body& b);

    Scene* m_Scene;
    std::vector<Body> m_Bodies;
    std::set<std::pair<EntityId, EntityId>> m_Contacts;      // + trigger flag in high bit set below
    std::set<std::pair<EntityId, EntityId>> m_TriggerContacts;
    std::vector<CollisionEvent> m_Events;
    uint32_t m_BodyCount = 0;
    bool m_Batch = false;
};

} // namespace nd
