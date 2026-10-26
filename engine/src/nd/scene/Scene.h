#pragma once
#include "nd/core/Base.h"
#include "nd/core/UUID.h"
#include "nd/scene/ECS.h"
#include "nd/scene/Components.h"
#include <string>
#include <unordered_map>

namespace nd {

class Entity;
class PhysicsWorld;
class NavGrid;
struct RenderCamera;

struct SceneEnvironment {
    vec3 skyTop{ 0.32f, 0.52f, 0.86f };
    vec3 skyHorizon{ 0.78f, 0.86f, 0.95f };
    vec3 groundColor{ 0.36f, 0.33f, 0.30f };
    float ambientIntensity = 0.45f;
    bool fog = true;
    vec3 fogColor{ 0.74f, 0.82f, 0.92f };
    float fogDensity = 0.006f;
    float exposure = 1.0f;
    vec3 gravity{ 0.0f, -20.0f, 0.0f };
};

class Scene {
public:
    Scene();
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // --- Entities ---------------------------------------------------------
    Entity CreateEntity(const std::string& name = "Entity");
    Entity CreateEntityWithUUID(UUID id, const std::string& name = "Entity");
    void DestroyEntity(Entity e);           // deferred to end of frame while the game runs
    Entity DuplicateEntity(Entity e);       // deep copy (incl. children)
    Entity FindByName(const std::string& name);
    Entity FindByUUID(UUID id);
    std::vector<Entity> FindAllWithTag(const std::string& tag);
    Entity PrimaryCamera();
    size_t EntityCount() const { return m_Registry.AliveCount(); }

    // --- Hierarchy --------------------------------------------------------
    void SetParent(Entity child, Entity parent, bool keepWorldTransform = true);
    Entity GetParent(Entity e);
    std::vector<Entity> GetChildren(Entity e);
    std::vector<Entity> RootEntities();
    mat4 WorldMatrix(Entity e);
    vec3 WorldPosition(Entity e);
    bool IsActiveInHierarchy(Entity e);

    // --- Lifecycle --------------------------------------------------------
    void OnRuntimeStart();
    void OnRuntimeStop();
    void OnUpdateRuntime(float dt);         // gameplay: scripts, physics, animation, audio, AI...
    void OnUpdateEditor(float dt);          // editor preview: animation + particles only
    bool IsRunning() const { return m_Running; }
    bool IsPaused() const { return m_Paused; }
    void SetPaused(bool p) { m_Paused = p; }
    void Step(int frames = 1) { m_StepFrames = frames; }

    // Runs scripts' OnGUI and draws UIElement components (call after rendering the 3D scene).
    void RenderGameUI(int viewportWidth, int viewportHeight, const vec2& mouseInViewport, bool mouseDown, bool mousePressed);

    // --- Events -----------------------------------------------------------
    void BroadcastMessage(const std::string& message);   // calls OnMessage on every script

    // --- Systems ----------------------------------------------------------
    Registry& Reg() { return m_Registry; }
    PhysicsWorld& Physics() { return *m_Physics; }
    NavGrid& Nav() { return *m_Nav; }
    SceneEnvironment& Environment() { return m_Environment; }

    // Camera used for audio listener / UI projection during runtime.
    bool GetRuntimeCamera(float aspect, mat4& view, mat4& proj, vec3& position);

    std::string name = "Untitled";
    std::string path;                       // project-relative file path

    // Copy of the whole scene (used for play mode). Implemented via serialization.
    static Ref<Scene> Copy(Scene& other);

private:
    void ProcessPendingDestroys();
    void InstantiateScripts();
    void UpdateScripts(float dt);
    void UpdateAnimators(float dt);
    void UpdateNavAgents(float dt);
    void UpdateAudio();
    void UpdateParticles(float dt);
    void UpdateTerrains();
    void DestroyImmediate(EntityId e);

    Registry m_Registry;
    std::unordered_map<UUID, EntityId> m_UUIDMap;
    std::vector<EntityId> m_PendingDestroy;
    Scope<PhysicsWorld> m_Physics;
    Scope<NavGrid> m_Nav;
    SceneEnvironment m_Environment;
    bool m_Running = false;
    bool m_Paused = false;
    int m_StepFrames = 0;
    float m_FixedAccumulator = 0.0f;

    friend class Entity;
    friend class SceneSerializer;
    friend class PhysicsWorld;
};

} // namespace nd
