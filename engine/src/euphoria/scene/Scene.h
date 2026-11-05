#pragma once
#include "euphoria/core/Base.h"
#include "euphoria/core/UUID.h"
#include "euphoria/scene/ECS.h"
#include "euphoria/scene/Components.h"
#include "euphoria/scene/Events.h"
#include <string>
#include <unordered_map>

namespace eu {

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

    // --- Post processing & image (per scene; quality switches live in RenderSettings) ---
    float reflectionIntensity = 1.0f;  // how strongly surfaces reflect the captured surroundings
    float ssaoStrength = 0.8f;         // ambient occlusion: 0 = off
    float ssaoRadius = 0.6f;           // world units
    float bloomIntensity = 0.06f;      // 0 = off
    float bloomThreshold = 1.0f;       // brightness (after exposure) where glow starts
    float contrast = 1.05f;            // color grading
    float saturation = 1.05f;
    float temperature = 0.0f;          // -1 cool ... +1 warm
    float tint = 0.0f;                 // -1 green ... +1 magenta
    float vignette = 0.22f;
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

    // Per-frame transform cache. CacheTransforms() resolves every entity's world matrix and
    // active-in-hierarchy flag in one pass (each parent chain is walked once, not once per
    // child), so systems that touch every entity - the renderer above all - don't redo it.
    // The cache is a snapshot: call it again after moving things.
    void CacheTransforms();
    const mat4& CachedWorld(EntityId id) const { return m_WorldCache[ecs::Index(id)].world; }
    bool CachedActive(EntityId id) const { return m_WorldCache[ecs::Index(id)].active; }

    // --- Lifecycle --------------------------------------------------------
    void OnRuntimeStart();
    void OnRuntimeStop();
    static void EndPlaySession();           // play mode / the game is ending: clear screen fades
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
    EventSystem& Events() { return m_Events; }           // named events + timers (see Events.h)

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
    // Event Trigger components
    void ResetEventTriggers();
    void UpdateEventTriggers(float dt);
    void FireRule(Entity owner, EventRule& rule);
    void RunAction(UUID owner, const EventRule& rule);
    void DispatchContactRules(Entity self, Entity other, TriggerWhen when);
    void UpdateTweens(float dt);
    struct Tween {
        UUID target;
        bool rotate = false;
        vec3 fromP{ 0.0f }, toP{ 0.0f };
        quat fromR{ 1, 0, 0, 0 }, toR{ 1, 0, 0, 0 };
        float t = 0.0f, duration = 1.0f;
    };
    std::vector<Tween> m_Tweens;
    EventSystem m_Events;
    bool m_TriggersStarted = false;

    Registry m_Registry;
    std::unordered_map<UUID, EntityId> m_UUIDMap;
    struct CachedTransform { mat4 world{ 1.0f }; bool active = true; uint32_t stamp = 0; };
    std::vector<CachedTransform> m_WorldCache;
    uint32_t m_CacheStamp = 0;
    const CachedTransform& Resolve(EntityId id, int depth);
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

} // namespace eu
