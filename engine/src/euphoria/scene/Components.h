#pragma once
// Every built-in component. Components are plain data; systems in Scene.cpp and the
// subsystem folders (physics, audio, animation, ...) give them behaviour.
#include "euphoria/core/Base.h"
#include "euphoria/core/UUID.h"
#include "euphoria/core/Json.h"
#include "euphoria/animation/Animation.h"
#include <string>
#include <vector>

namespace eu {

class Model;
class ScriptableEntity;
class TerrainRuntime;
class ParticleRuntime;

struct IDComponent {
    UUID id;
};

struct TagComponent {
    std::string name = "Entity";
    std::string tag = "Untagged";
    bool active = true;
};

struct TransformComponent {
    vec3 position{ 0.0f };
    quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    vec3 scale{ 1.0f };
    UUID parent = 0;                         // 0 = root

    mat4 Local() const { return ComposeTRS(position, rotation, scale); }
    vec3 EulerDegrees() const { return glm::degrees(glm::eulerAngles(rotation)); }
    void SetEulerDegrees(const vec3& e) { rotation = quat(glm::radians(e)); }
    vec3 Forward() const { return rotation * vec3(0, 0, -1); }
    vec3 Right() const { return rotation * vec3(1, 0, 0); }
    vec3 Up() const { return rotation * vec3(0, 1, 0); }
    void LookAt(const vec3& target, const vec3& up = { 0, 1, 0 }) {
        vec3 d = target - position;
        if (glm::length(d) < 1e-5f) return;
        rotation = glm::quatLookAt(glm::normalize(d), up);
    }
};

struct MeshRendererComponent {
    std::string mesh = "builtin:cube";      // "builtin:<shape>" or a model file in the project
    bool useModelMaterials = true;          // use materials from the model file
    vec4 color{ 1.0f };
    std::string texture;                    // optional albedo texture (project-relative)
    float tiling = 1.0f;
    // Physically based material (multiplied with the model's own material when used):
    float metallic = 0.0f;                  // 0 = plastic/wood/skin, 1 = bare metal
    float roughness = 0.5f;                 // 0 = mirror, 1 = chalk
    std::string normalMap;                  // optional tangent-space normal map (project-relative)
    float normalStrength = 1.0f;
    std::string metallicRoughnessMap;       // optional, glTF packing: G = roughness, B = metallic
    vec3 emissive{ 0.0f };
    bool castShadows = true;
    bool visible = true;
    // Toon / cel shading: lighting is snapped into flat bands instead of a smooth gradient.
    bool toon = false;
    int toonBands = 3;                      // number of light steps (2 = hard two-tone)
    float toonSoftness = 0.02f;             // blur at band edges (0 = razor sharp)
    vec3 toonShadowColor{ 0.45f, 0.40f, 0.62f }; // sRGB tint multiplied into the darkest band
    float toonShadowFill = 0.0f;            // 0 = tinted albedo, 1 = solid shadow color (Killer7)
    float toonRim = 0.25f;                  // hard rim light on the lit side (0 = off)
    // Ink outline drawn with an inverted hull (works with or without toon shading).
    bool outline = false;
    float outlineWidth = 3.0f;              // in pixels at close range, thinner with distance
    vec4 outlineColor{ 0.04f, 0.03f, 0.05f, 1.0f };
    // runtime
    Ref<Model> model;
    std::string loadedMesh;
    uint32_t loadedVersion = 0;
};

// One-click looks for MeshRendererComponent's toon settings.
enum class ToonPreset {
    SoftCel,        // 3 bands, cool purple shadows, rim light, ink outline (the default)
    NoMoreHeroes,   // 2 hard bands, near-black shadows that keep a hint of the texture, thin ink line
    Killer7,        // 2 hard bands, solid black shadows, no outline
};
inline void ApplyToonPreset(MeshRendererComponent& m, ToonPreset p) {
    m.toon = true;
    switch (p) {
    case ToonPreset::SoftCel:
        m.toonBands = 3; m.toonSoftness = 0.02f; m.toonShadowColor = { 0.45f, 0.40f, 0.62f }; m.toonShadowFill = 0.0f;
        m.toonRim = 0.25f; m.outline = true; m.outlineWidth = 3.0f; m.outlineColor = { 0.04f, 0.03f, 0.05f, 1.0f };
        break;
    case ToonPreset::NoMoreHeroes:
        m.toonBands = 2; m.toonSoftness = 0.0f; m.toonShadowColor = { 0.12f, 0.08f, 0.12f }; m.toonShadowFill = 0.65f;
        m.toonRim = 0.0f; m.outline = true; m.outlineWidth = 1.5f; m.outlineColor = { 0.0f, 0.0f, 0.0f, 1.0f };
        break;
    case ToonPreset::Killer7:
        m.toonBands = 2; m.toonSoftness = 0.0f; m.toonShadowColor = { 0.0f, 0.0f, 0.0f }; m.toonShadowFill = 1.0f;
        m.toonRim = 0.0f; m.outline = false;
        break;
    }
}

struct CameraComponent {
    bool primary = true;
    bool orthographic = false;
    float fov = 60.0f;                      // degrees (vertical)
    float orthoSize = 10.0f;
    float nearClip = 0.1f;
    float farClip = 800.0f;
    mat4 Projection(float aspect) const {
        if (orthographic) {
            float h = orthoSize * 0.5f, w = h * aspect;
            return glm::ortho(-w, w, -h, h, nearClip, farClip);
        }
        return glm::perspective(glm::radians(fov), aspect, nearClip, farClip);
    }
};

enum class LightType { Directional, Point, Spot };
struct LightComponent {
    LightType type = LightType::Point;
    vec3 color{ 1.0f };
    float intensity = 1.0f;
    float range = 10.0f;                    // point / spot
    float spotAngle = 35.0f;                // degrees (outer cone)
    bool castShadows = true;                // directional only
};

enum class BodyType { Static, Dynamic, Kinematic };
struct RigidBodyComponent {
    BodyType type = BodyType::Dynamic;
    float mass = 1.0f;
    float drag = 0.05f;
    float gravityScale = 1.0f;
    float restitution = 0.1f;
    float friction = 0.6f;
    bool lockY = false;                     // keep on its height (top-down games)
    vec3 velocity{ 0.0f };
    bool interpolate = true;                // smooth rendering between 60 Hz physics steps
    // runtime
    vec3 force{ 0.0f };
    bool grounded = false;
    vec3 groundNormal{ 0, 1, 0 };
    // Render interpolation state (see Scene::OnUpdateRuntime)
    vec3 physicsPos{ 0.0f }, previousPos{ 0.0f }, shownPos{ 0.0f };
    bool interpValid = false;

    void AddForce(const vec3& f) { force += f; }
    void AddImpulse(const vec3& j) { if (type == BodyType::Dynamic && mass > 0) velocity += j / mass; }
};

struct BoxColliderComponent {
    vec3 size{ 1.0f };
    vec3 offset{ 0.0f };
    bool isTrigger = false;
};

struct SphereColliderComponent {
    float radius = 0.5f;
    vec3 offset{ 0.0f };
    bool isTrigger = false;
};

struct ScriptInstance {
    std::string className;
    json fields = json::object();           // serialized exposed fields
    Ref<ScriptableEntity> instance;         // runtime
    bool started = false;
};
struct ScriptComponent {
    std::vector<ScriptInstance> scripts;
};

// ---------------------------------------------------------------- Event Trigger
// "When this happens -> do that" rules, set up in the inspector without writing code.
enum class TriggerWhen {
    Start,           // the game starts
    TriggerEnter,    // something enters this entity's trigger collider (optionally: with tag)
    TriggerExit,
    CollisionEnter,
    Event,           // a named event is emitted (by scripts or other triggers)
    Timer,           // every `interval` seconds
    InputAction,     // an input action (from the project's input map) is pressed
};
enum class TriggerDo {
    EmitEvent,       // emit event `param`
    Move,            // move target by `vector` (local axes of the world) over `duration`
    Rotate,          // rotate target by `vector` degrees over `duration`
    SetActive,       // param: "true", "false" or "toggle"
    Destroy,
    PlaySound,       // param: audio clip; plays at the target's position
    SendMessage,     // calls OnMessage(param) on the target's scripts
    LoadScene,       // param: scene path
    Log,             // writes param to the console
};
struct EventRule {
    TriggerWhen when = TriggerWhen::TriggerEnter;
    std::string whenName;        // event name / input action for Event / InputAction
    std::string tagFilter;       // Trigger/Collision: only react to entities with this tag (empty = any)
    float interval = 1.0f;       // Timer period
    TriggerDo action = TriggerDo::EmitEvent;
    uint64_t target = 0;         // entity acted on (0 = this entity)
    std::string param;
    vec3 vector{ 0.0f };
    float duration = 1.0f;       // Move / Rotate
    float delay = 0.0f;          // wait before acting
    bool once = false;           // fire at most once per play session
    // runtime
    bool fired = false;
    uint64_t subscription = 0;
    float timer = 0.0f;
};
struct EventTriggerComponent {
    std::vector<EventRule> rules;
};

struct AudioSourceComponent {
    std::string clip;                       // project-relative .wav/.mp3/.flac
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool playOnStart = true;
    bool spatial = true;
    bool music = false;                     // routed to the music bus
    float minDistance = 1.0f;
    float maxDistance = 40.0f;
    // runtime
    uint32_t handle = 0;
};

struct AudioListenerComponent {
    bool active = true;
};

struct AnimatorComponent {
    std::vector<AnimParam> params;
    std::vector<AnimState> states;
    std::vector<AnimTransition> transitions;
    std::string defaultState;
    float speed = 1.0f;
    bool rootMotion = false;
    // runtime
    Ref<AnimatorRuntime> runtime;

    AnimParam* FindParam(const std::string& n) { for (auto& p : params) if (p.name == n) return &p; return nullptr; }
    void SetFloat(const std::string& n, float v) { if (auto* p = FindParam(n)) p->value = v; else params.push_back({ n, AnimParamType::Float, v }); }
    void SetBool(const std::string& n, bool v) { if (auto* p = FindParam(n)) p->value = v ? 1.0f : 0.0f; else params.push_back({ n, AnimParamType::Bool, v ? 1.0f : 0.0f }); }
    void SetTrigger(const std::string& n) { if (auto* p = FindParam(n)) p->value = 1.0f; else params.push_back({ n, AnimParamType::Trigger, 1.0f }); }
    float GetFloat(const std::string& n) { auto* p = FindParam(n); return p ? p->value : 0.0f; }
    std::string CurrentState() const { return runtime ? runtime->currentState : defaultState; }
};

struct TerrainComponent {
    int resolution = 129;                   // vertices per side
    float size = 120.0f;                    // world units per side (centered on the entity)
    float heightScale = 14.0f;
    int seed = 1337;
    float noiseScale = 0.012f;
    int octaves = 5;
    vec3 sandColor{ 0.76f, 0.70f, 0.50f };
    vec3 grassColor{ 0.30f, 0.52f, 0.20f };
    vec3 rockColor{ 0.42f, 0.40f, 0.38f };
    vec3 snowColor{ 0.95f, 0.95f, 0.97f };
    float sandHeight = 0.8f, snowHeight = 11.0f;
    bool grass = true;
    float grassDensity = 1.2f;              // blades per square meter (clumps)
    float grassMinHeight = 0.6f;            // only above this world height
    int treeCount = 60;
    std::string treeModel;                  // empty = built-in stylised tree
    std::vector<float> heights;             // resolution*resolution, world-space Y (local)
    // runtime
    Ref<TerrainRuntime> runtime;
    bool dirty = true;                      // mesh needs rebuild
    bool foliageDirty = true;

    float HeightAtLocal(float x, float z) const;   // bilinear sample, local coords
    vec3 NormalAtLocal(float x, float z) const;
};

struct WaterComponent {
    vec2 size{ 200.0f, 200.0f };
    vec4 shallowColor{ 0.20f, 0.55f, 0.60f, 0.65f };
    vec4 deepColor{ 0.03f, 0.18f, 0.30f, 0.9f };
    float waveHeight = 0.12f;
    float waveSpeed = 1.0f;
    float waveScale = 0.25f;
};

enum class ParticleShape { Point, Sphere, Box };
struct ParticleEmitterComponent {
    bool playing = true;
    bool loop = true;
    bool playOnStart = true;
    float rate = 40.0f;                     // particles per second
    int burst = 0;                          // particles emitted at start
    int maxParticles = 800;
    float lifetime = 1.4f;
    float lifetimeVariance = 0.4f;
    ParticleShape shape = ParticleShape::Sphere;
    vec3 shapeSize{ 0.2f };
    vec3 velocity{ 0.0f, 2.0f, 0.0f };
    float spread = 0.4f;                    // random velocity added (units/s)
    vec3 gravity{ 0.0f };
    float drag = 0.0f;
    vec4 startColor{ 1.0f, 0.7f, 0.2f, 1.0f };
    vec4 endColor{ 0.8f, 0.1f, 0.0f, 0.0f };
    float startSize = 0.35f;
    float endSize = 0.05f;
    bool additive = true;
    bool worldSpace = true;
    // runtime
    Ref<ParticleRuntime> runtime;
};

enum class UIType { Panel, Text, Button, ProgressBar, Image };
enum class UIAnchor { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
struct UIElementComponent {
    UIType type = UIType::Panel;
    UIAnchor anchor = UIAnchor::TopLeft;
    vec2 position{ 20.0f, 20.0f };          // offset from anchor (reference 1920x1080 pixels)
    vec2 size{ 300.0f, 60.0f };
    vec4 color{ 0.0f, 0.0f, 0.0f, 0.5f };
    vec4 textColor{ 1.0f };
    std::string text;
    float fontSize = 28.0f;
    float value = 1.0f;                     // progress bar 0..1
    std::string image;
    bool visible = true;
    int layer = 0;
    // runtime
    bool hovered = false;
};

struct NavAgentComponent {
    float speed = 3.5f;
    float turnSpeed = 720.0f;               // degrees per second
    float stoppingDistance = 0.4f;
    float repathInterval = 0.5f;
    bool snapToGround = true;
    float baseOffset = 0.0f;                // height above ground
    // runtime
    bool hasDestination = false;
    vec3 destination{ 0.0f };
    std::vector<vec3> path;
    size_t pathIndex = 0;
    float repathTimer = 0.0f;
    float currentSpeed = 0.0f;

    void SetDestination(const vec3& d) { destination = d; hasDestination = true; repathTimer = 0.0f; }
    void Stop() { hasDestination = false; path.clear(); currentSpeed = 0.0f; }
    bool Arrived() const { return !hasDestination; }
};

struct NetworkIdentityComponent {
    uint32_t netId = 0;                     // assigned at runtime by the network manager
    uint32_t owner = 0;                     // 0 = server/host
    bool syncTransform = true;
    bool interpolate = true;
    // runtime
    vec3 targetPosition{ 0.0f };
    quat targetRotation{ 1, 0, 0, 0 };
    bool hasTarget = false;
};

// Marks an entity whose state goes into checkpoints / save slots.
struct SaveableComponent {
    bool saveTransform = true;
    bool saveScripts = true;
    bool saveActive = true;
};

} // namespace eu
