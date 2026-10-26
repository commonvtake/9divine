#pragma once
// Base class for C++ gameplay scripts.
//
//   class Spinner : public nd::ScriptableEntity {
//   public:
//       float speed = 90.0f;
//       void Reflect(nd::FieldVisitor& v) override { v.Float("Speed", speed); }
//       void OnUpdate(float dt) override { Transform().rotation *= glm::angleAxis(glm::radians(speed * dt), nd::vec3(0,1,0)); }
//   };
//   ND_SCRIPT(Spinner)
//
// Fields visited in Reflect() show up in the editor inspector and are saved with the scene.
#include "nd/scene/Entity.h"
#include "nd/core/Input.h"
#include "nd/core/Log.h"
#include "nd/core/Profiler.h"
#include <map>

namespace nd {

class FieldVisitor {
public:
    virtual ~FieldVisitor() = default;
    virtual void Float(const char* name, float& v, float speed = 0.1f) = 0;
    virtual void Int(const char* name, int& v) = 0;
    virtual void Bool(const char* name, bool& v) = 0;
    virtual void Vec3(const char* name, vec3& v) = 0;
    virtual void Color(const char* name, vec4& v) = 0;
    virtual void String(const char* name, std::string& v) = 0;
    virtual void EntityRef(const char* name, uint64_t& uuid) = 0;   // reference another entity by UUID
};

class ScriptableEntity {
public:
    virtual ~ScriptableEntity() = default;

    // Lifecycle
    virtual void OnCreate() {}                       // when the game starts / entity spawns
    virtual void OnStart() {}                        // first frame after every script's OnCreate
    virtual void OnUpdate(float dt) {}
    virtual void OnFixedUpdate(float dt) {}          // 60 Hz, before physics
    virtual void OnLateUpdate(float dt) {}           // after physics + animation (cameras)
    virtual void OnDestroy() {}

    // Physics
    virtual void OnCollisionEnter(Entity other) {}
    virtual void OnCollisionExit(Entity other) {}
    virtual void OnTriggerEnter(Entity other) {}
    virtual void OnTriggerExit(Entity other) {}

    // UI / messages / network
    virtual void OnGUI() {}                          // immediate-mode game UI (nd::UI::...)
    virtual void OnUIButton(const std::string& buttonName) {}
    virtual void OnMessage(const std::string& message) {}
    virtual void OnNetworkMessage(const std::string& channel, const json& data, uint32_t sender) {}

    // Exposed fields (inspector + serialization)
    virtual void Reflect(FieldVisitor& v) {}

    // Helpers ------------------------------------------------------------
    template <typename T> T& Get() { return m_Entity.Get<T>(); }
    template <typename T> T* TryGet() { return m_Entity.TryGet<T>(); }
    template <typename T> bool Has() { return m_Entity.Has<T>(); }
    template <typename T, typename... A> T& Add(A&&... a) { return m_Entity.Add<T>(std::forward<A>(a)...); }
    TransformComponent& Transform() { return m_Entity.Get<TransformComponent>(); }
    Entity GetEntity() const { return m_Entity; }
    Scene& GetScene() { return *m_Entity.GetScene(); }
    Entity Find(const std::string& name) { return GetScene().FindByName(name); }
    Entity FindByUUID(uint64_t id) { return GetScene().FindByUUID(id); }
    void Destroy(Entity e) { GetScene().DestroyEntity(e); }
    void DestroySelf() { GetScene().DestroyEntity(m_Entity); }
    Entity Instantiate(Entity original, const vec3& position);

    // Look up another script on the same (or another) entity.
    template <typename T> T* GetScript() { return GetScriptOn<T>(m_Entity); }
    template <typename T> static T* GetScriptOn(Entity e) {
        if (!e || !e.Has<ScriptComponent>()) return nullptr;
        for (auto& s : e.Get<ScriptComponent>().scripts)
            if (auto* p = dynamic_cast<T*>(s.instance.get())) return p;
        return nullptr;
    }

private:
    Entity m_Entity;
    friend class Scene;
    friend class ScriptRegistry;
};

// Registry of script classes so scenes can store scripts by name.
class ScriptRegistry {
public:
    using Factory = std::function<Ref<ScriptableEntity>()>;
    static void Register(const std::string& name, Factory factory);
    static Ref<ScriptableEntity> Create(const std::string& name);
    static std::vector<std::string> Names();
    static bool Exists(const std::string& name);
private:
    static std::map<std::string, Factory>& Map();
};

template <typename T>
struct ScriptRegistrar {
    explicit ScriptRegistrar(const char* name) { ScriptRegistry::Register(name, [] { return std::static_pointer_cast<ScriptableEntity>(std::make_shared<T>()); }); }
};

// JSON <-> field helpers used by the serializer and the editor.
void LoadScriptFields(ScriptableEntity& script, const json& fields);
json SaveScriptFields(ScriptableEntity& script);

} // namespace nd

#define ND_SCRIPT(Class) static ::nd::ScriptRegistrar<Class> ND_CONCAT(s_ndScript_, Class)(#Class);
