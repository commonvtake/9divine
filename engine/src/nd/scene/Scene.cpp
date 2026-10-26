#include "nd/scene/Scene.h"
#include "nd/scene/Entity.h"
#include "nd/scene/ScriptableEntity.h"
#include "nd/scene/SceneSerializer.h"
#include "nd/physics/Physics.h"
#include "nd/ai/Navigation.h"
#include "nd/audio/Audio.h"
#include "nd/assets/AssetManager.h"
#include "nd/terrain/Terrain.h"
#include "nd/particles/Particles.h"
#include "nd/renderer/Renderer.h"
#include "nd/ui/GameUI.h"
#include "nd/net/Network.h"
#include "nd/core/Profiler.h"
#include "nd/core/Log.h"
#include <cmath>

namespace nd {

static constexpr float kFixedStep = 1.0f / 60.0f;

Scene::Scene() {
    m_Physics = MakeScope<PhysicsWorld>(this);
    m_Nav = MakeScope<NavGrid>(this);
}

Scene::~Scene() {
    if (m_Running) OnRuntimeStop();
}

// ------------------------------------------------------------------ entities
Entity Scene::CreateEntity(const std::string& name) { return CreateEntityWithUUID(UUID(), name); }

Entity Scene::CreateEntityWithUUID(UUID id, const std::string& name) {
    Entity e(m_Registry.Create(), this);
    e.Add<IDComponent>(IDComponent{ id });
    auto& tag = e.Add<TagComponent>();
    tag.name = name;
    e.Add<TransformComponent>();
    m_UUIDMap[id] = e.Id();
    return e;
}

void Scene::DestroyEntity(Entity e) {
    if (!e.IsValid()) return;
    if (m_Running) {
        if (std::find(m_PendingDestroy.begin(), m_PendingDestroy.end(), e.Id()) == m_PendingDestroy.end())
            m_PendingDestroy.push_back(e.Id());
        e.Get<TagComponent>().active = false;   // disappear immediately
        return;
    }
    DestroyImmediate(e.Id());
}

void Scene::DestroyImmediate(EntityId id) {
    Entity e(id, this);
    if (!e.IsValid()) return;
    for (auto& child : GetChildren(e)) DestroyImmediate(child.Id());
    if (auto* sc = e.TryGet<ScriptComponent>()) {
        for (auto& s : sc->scripts)
            if (s.instance && m_Running) s.instance->OnDestroy();
        sc->scripts.clear();
    }
    if (auto* as = e.TryGet<AudioSourceComponent>()) if (as->handle) { Audio::Stop(as->handle); as->handle = 0; }
    m_UUIDMap.erase(e.GetUUID());
    m_Registry.Destroy(id);
}

void Scene::ProcessPendingDestroys() {
    auto pending = std::move(m_PendingDestroy);
    m_PendingDestroy.clear();
    for (EntityId id : pending) DestroyImmediate(id);
}

Entity Scene::DuplicateEntity(Entity e) {
    if (!e.IsValid()) return {};
    std::vector<json> data;
    std::function<void(Entity)> collect = [&](Entity x) {
        data.push_back(SceneSerializer::SerializeEntity(x));
        for (auto& c : GetChildren(x)) collect(c);
    };
    collect(e);
    std::unordered_map<uint64_t, uint64_t> remap;
    // First pass assigns new UUIDs for every entity so parent links resolve.
    for (auto& j : data) remap[JGet<uint64_t>(j, "uuid", 0)] = UUID();
    Entity root;
    for (size_t i = 0; i < data.size(); i++) {
        auto& j = data[i];
        uint64_t old = JGet<uint64_t>(j, "uuid", 0);
        j["uuid"] = remap[old];
        Entity created = SceneSerializer::DeserializeEntity(*this, j, nullptr);
        auto& t = created.Get<TransformComponent>();
        if (remap.count(t.parent)) t.parent = remap[t.parent];
        if (auto* sc = created.TryGet<ScriptComponent>())
            for (auto& s : sc->scripts)
                for (auto& [k, v] : s.fields.items())
                    if (v.is_number_unsigned() && remap.count(v.get<uint64_t>())) v = remap[v.get<uint64_t>()];
        if (i == 0) root = created;
    }
    if (m_Running) InstantiateScripts();
    return root;
}

Entity Scene::FindByName(const std::string& name) {
    Entity found;
    m_Registry.View<TagComponent>([&](EntityId id, TagComponent& t) { if (!found && t.name == name) found = Entity(id, this); });
    return found;
}

Entity Scene::FindByUUID(UUID id) {
    auto it = m_UUIDMap.find(id);
    if (it == m_UUIDMap.end() || !m_Registry.Valid(it->second)) return {};
    return Entity(it->second, this);
}

std::vector<Entity> Scene::FindAllWithTag(const std::string& tag) {
    std::vector<Entity> out;
    m_Registry.View<TagComponent>([&](EntityId id, TagComponent& t) { if (t.tag == tag) out.emplace_back(id, this); });
    return out;
}

Entity Scene::PrimaryCamera() {
    Entity best;
    m_Registry.View<CameraComponent>([&](EntityId id, CameraComponent& c) {
        Entity e(id, this);
        if (!IsActiveInHierarchy(e)) return;
        if (!best || (c.primary && !best.Get<CameraComponent>().primary)) best = e;
    });
    return best;
}

// ----------------------------------------------------------------- hierarchy
Entity Scene::GetParent(Entity e) {
    UUID p = e.Get<TransformComponent>().parent;
    return p ? FindByUUID(p) : Entity();
}

std::vector<Entity> Scene::GetChildren(Entity e) {
    std::vector<Entity> out;
    UUID id = e.GetUUID();
    m_Registry.View<TransformComponent>([&](EntityId c, TransformComponent& t) { if (t.parent == id) out.emplace_back(c, this); });
    return out;
}

std::vector<Entity> Scene::RootEntities() {
    std::vector<Entity> out;
    for (EntityId id : m_Registry.AllEntities()) {
        Entity e(id, this);
        UUID p = e.Get<TransformComponent>().parent;
        if (p == 0 || !FindByUUID(p)) out.push_back(e);
    }
    return out;
}

void Scene::SetParent(Entity child, Entity parent, bool keepWorld) {
    if (!child) return;
    // Refuse cycles
    for (Entity p = parent; p; p = GetParent(p))
        if (p == child) return;
    mat4 world = WorldMatrix(child);
    auto& t = child.Get<TransformComponent>();
    t.parent = parent ? parent.GetUUID() : UUID(0);
    if (keepWorld) {
        mat4 local = parent ? glm::inverse(WorldMatrix(parent)) * world : world;
        DecomposeTRS(local, t.position, t.rotation, t.scale);
    }
}

mat4 Scene::WorldMatrix(Entity e) {
    mat4 m = e.Get<TransformComponent>().Local();
    int depth = 0;
    for (Entity p = GetParent(e); p && depth < 64; p = GetParent(p), depth++) m = p.Get<TransformComponent>().Local() * m;
    return m;
}

vec3 Scene::WorldPosition(Entity e) { return vec3(WorldMatrix(e)[3]); }

bool Scene::IsActiveInHierarchy(Entity e) {
    int depth = 0;
    for (Entity p = e; p && depth < 64; p = GetParent(p), depth++)
        if (!p.Get<TagComponent>().active) return false;
    return true;
}

// ----------------------------------------------------------------- runtime
void Scene::InstantiateScripts() {
    m_Registry.View<ScriptComponent>([&](EntityId id, ScriptComponent& sc) {
        for (size_t i = 0; i < sc.scripts.size(); i++) {
            ScriptInstance& s = sc.scripts[i];
            if (s.instance) continue;
            s.instance = ScriptRegistry::Create(s.className);
            if (!s.instance) {
                ND_WARN("Unknown script class '{}' on '{}'", s.className, Entity(id, this).Name());
                continue;
            }
            s.instance->m_Entity = Entity(id, this);
            LoadScriptFields(*s.instance, s.fields);
            s.started = false;
            s.instance->OnCreate();
        }
    });
}

void Scene::OnRuntimeStart() {
    m_Running = true;
    m_Paused = false;
    m_FixedAccumulator = 0.0f;
    m_Physics->Reset();
    m_Registry.View<TerrainComponent>([&](EntityId, TerrainComponent& t) { TerrainSystem::Update(t); });
    if (m_Registry.Count<NavAgentComponent>() > 0) m_Nav->Bake();
    m_Registry.View<ParticleEmitterComponent>([&](EntityId, ParticleEmitterComponent& p) {
        p.playing = p.playOnStart;
        if (p.runtime) p.runtime->Clear();
    });
    m_Registry.View<TransformComponent, AudioSourceComponent>([&](EntityId id, TransformComponent&, AudioSourceComponent& a) {
        if (!a.playOnStart || a.clip.empty() || !IsActiveInHierarchy(Entity(id, this))) return;
        SoundParams p;
        p.volume = a.volume; p.pitch = a.pitch; p.loop = a.loop; p.spatial = a.spatial && !a.music;
        p.position = WorldPosition(Entity(id, this));
        p.minDistance = a.minDistance; p.maxDistance = a.maxDistance;
        p.bus = a.music ? AudioBus::Music : AudioBus::SFX;
        p.stream = a.music;
        a.handle = Audio::Play(a.clip, p);
    });
    InstantiateScripts();
}

void Scene::OnRuntimeStop() {
    m_Registry.View<ScriptComponent>([&](EntityId, ScriptComponent& sc) {
        for (auto& s : sc.scripts) {
            if (s.instance) {
                s.instance->OnDestroy();
                s.fields = SaveScriptFields(*s.instance);
            }
            s.instance.reset();
        }
    });
    m_Registry.View<AudioSourceComponent>([&](EntityId, AudioSourceComponent& a) {
        if (a.handle) Audio::Stop(a.handle);
        a.handle = 0;
    });
    m_Running = false;
}

template <typename Fn>
static void ForEachScript(Registry& reg, Scene* scene, Fn&& fn) {
    reg.View<ScriptComponent>([&](EntityId id, ScriptComponent& sc) {
        if (!scene->IsActiveInHierarchy(Entity(id, scene))) return;
        for (size_t i = 0; i < sc.scripts.size(); i++) {
            Ref<ScriptableEntity> inst = sc.scripts[i].instance;   // keep alive during the call
            if (inst) fn(sc.scripts[i], *inst);
        }
    });
}

void Scene::UpdateScripts(float dt) {
    ND_PROFILE_SCOPE("Scripts");
    ForEachScript(m_Registry, this, [&](ScriptInstance& s, ScriptableEntity& inst) {
        if (!s.started) { s.started = true; inst.OnStart(); }
    });
    ForEachScript(m_Registry, this, [&](ScriptInstance&, ScriptableEntity& inst) { inst.OnUpdate(dt); });
}

void Scene::UpdateAnimators(float dt) {
    ND_PROFILE_SCOPE("Animation");
    m_Registry.View<TransformComponent, MeshRendererComponent, AnimatorComponent>(
        [&](EntityId id, TransformComponent& t, MeshRendererComponent& mr, AnimatorComponent& anim) {
            if (!IsActiveInHierarchy(Entity(id, this))) return;
            if (!mr.model || mr.loadedMesh != mr.mesh) {
                mr.model = AssetManager::GetModel(mr.mesh);
                mr.loadedMesh = mr.mesh;
                mr.loadedVersion = AssetManager::Version();
            }
            if (!mr.model || !mr.model->IsSkinned()) return;
            if (!anim.runtime) anim.runtime = MakeRef<AnimatorRuntime>();
            anim.runtime->Update(anim, *mr.model, dt);
            if (anim.rootMotion && m_Running) t.position += t.rotation * (t.scale * anim.runtime->rootMotionDelta);
        });
}

void Scene::UpdateNavAgents(float dt) {
    if (m_Registry.Count<NavAgentComponent>() == 0) return;
    ND_PROFILE_SCOPE("Navigation");
    if (!m_Nav->IsBaked()) m_Nav->Bake();
    m_Registry.View<TransformComponent, NavAgentComponent>([&](EntityId id, TransformComponent& t, NavAgentComponent& a) {
        if (!IsActiveInHierarchy(Entity(id, this))) return;
        if (!a.hasDestination) { a.currentSpeed = MoveTowards(a.currentSpeed, 0.0f, a.speed * dt * 4.0f); return; }
        a.repathTimer -= dt;
        if (a.repathTimer <= 0.0f) {
            a.repathTimer = a.repathInterval;
            if (m_Nav->FindPath(t.position, a.destination, a.path)) a.pathIndex = a.path.size() > 1 ? 1 : 0;
            else { a.Stop(); return; }
        }
        if (a.path.empty()) { a.Stop(); return; }
        vec3 target = a.path[std::min(a.pathIndex, a.path.size() - 1)];
        vec2 to(target.x - t.position.x, target.z - t.position.z);
        float dist = glm::length(to);
        bool last = a.pathIndex >= a.path.size() - 1;
        if (last && dist <= a.stoppingDistance) { a.Stop(); return; }
        if (!last && dist < 0.3f) { a.pathIndex++; return; }
        a.currentSpeed = MoveTowards(a.currentSpeed, a.speed, a.speed * dt * 4.0f);
        vec2 dir = dist > 1e-4f ? to / dist : vec2(0.0f);
        float step = std::min(dist, a.currentSpeed * dt);
        t.position.x += dir.x * step;
        t.position.z += dir.y * step;
        // Turn towards travel direction
        if (dist > 1e-3f) {
            float desired = std::atan2(-dir.x, -dir.y);
            quat want = glm::angleAxis(desired, vec3(0, 1, 0));
            float maxAngle = glm::radians(a.turnSpeed) * dt;
            float cosHalf = std::min(1.0f, std::abs(glm::dot(glm::normalize(t.rotation), want)));
            float angle = 2.0f * std::acos(cosHalf);
            float f = angle > 1e-4f ? std::min(1.0f, maxAngle / angle) : 1.0f;
            t.rotation = glm::normalize(glm::slerp(t.rotation, want, f));
        }
        if (a.snapToGround) {
            float h;
            if (m_Physics->TerrainHeight(t.position, h) || m_Nav->SampleHeight(t.position, h)) t.position.y = h + a.baseOffset;
        }
    });
}

void Scene::UpdateParticles(float dt) {
    m_Registry.View<TransformComponent, ParticleEmitterComponent>([&](EntityId id, TransformComponent&, ParticleEmitterComponent& p) {
        Entity e(id, this);
        if (!IsActiveInHierarchy(e)) { if (p.runtime) p.runtime->Clear(); return; }
        if (!p.runtime) p.runtime = MakeRef<ParticleRuntime>();
        p.runtime->Update(p, WorldMatrix(e), dt);
    });
}

void Scene::UpdateAudio() {
    if (!Audio::Available()) return;
    bool listenerSet = false;
    m_Registry.View<AudioListenerComponent>([&](EntityId id, AudioListenerComponent& l) {
        if (listenerSet || !l.active || !IsActiveInHierarchy(Entity(id, this))) return;
        mat4 w = WorldMatrix(Entity(id, this));
        Audio::SetListener(vec3(w[3]), glm::normalize(vec3(w * vec4(0, 0, -1, 0))));
        listenerSet = true;
    });
    if (!listenerSet) {
        if (Entity cam = PrimaryCamera()) {
            mat4 w = WorldMatrix(cam);
            Audio::SetListener(vec3(w[3]), glm::normalize(vec3(w * vec4(0, 0, -1, 0))));
        }
    }
    m_Registry.View<AudioSourceComponent>([&](EntityId id, AudioSourceComponent& a) {
        if (a.handle && a.spatial) Audio::SetPosition(a.handle, WorldPosition(Entity(id, this)));
    });
}

void Scene::OnUpdateRuntime(float dt) {
    if (!m_Running) return;
    if (m_Paused) {
        if (m_StepFrames <= 0) { UpdateAudio(); return; }
        m_StepFrames--;
        dt = kFixedStep;
    }
    InstantiateScripts();
    UpdateScripts(dt);

    // Fixed timestep physics
    m_FixedAccumulator += dt;
    int steps = 0;
    while (m_FixedAccumulator >= kFixedStep && steps < 5) {
        ForEachScript(m_Registry, this, [&](ScriptInstance&, ScriptableEntity& inst) { inst.OnFixedUpdate(kFixedStep); });
        m_Physics->Step(kFixedStep);
        for (auto& ev : m_Physics->Events()) {
            Entity a(ev.a, this), b(ev.b, this);
            if (!a.IsValid() || !b.IsValid()) continue;
            auto dispatch = [&](Entity self, Entity other) {
                auto* sc = self.TryGet<ScriptComponent>();
                if (!sc) return;
                for (size_t i = 0; i < sc->scripts.size(); i++) {
                    Ref<ScriptableEntity> s = sc->scripts[i].instance;
                    if (!s) continue;
                    switch (ev.type) {
                    case CollisionEvent::CollisionEnter: s->OnCollisionEnter(other); break;
                    case CollisionEvent::CollisionExit: s->OnCollisionExit(other); break;
                    case CollisionEvent::TriggerEnter: s->OnTriggerEnter(other); break;
                    case CollisionEvent::TriggerExit: s->OnTriggerExit(other); break;
                    }
                }
            };
            dispatch(a, b);
            dispatch(b, a);
        }
        m_Physics->ClearEvents();
        m_FixedAccumulator -= kFixedStep;
        steps++;
    }
    if (steps == 5) m_FixedAccumulator = 0.0f;

    UpdateNavAgents(dt);
    UpdateAnimators(dt);
    {
        ND_PROFILE_SCOPE("LateUpdate");
        ForEachScript(m_Registry, this, [&](ScriptInstance&, ScriptableEntity& inst) { inst.OnLateUpdate(dt); });
    }
    UpdateParticles(dt);
    UpdateAudio();
    Network::Update(*this, dt);
    ProcessPendingDestroys();

    Profiler::Stats().entities = (uint32_t)EntityCount();
    Profiler::Stats().physicsBodies = m_Physics->BodyCount();
}

void Scene::OnUpdateEditor(float dt) {
    UpdateAnimators(dt);
    UpdateParticles(dt);
    Profiler::Stats().entities = (uint32_t)EntityCount();
}

bool Scene::GetRuntimeCamera(float aspect, mat4& view, mat4& proj, vec3& position) {
    Entity cam = PrimaryCamera();
    if (!cam) return false;
    auto& c = cam.Get<CameraComponent>();
    RenderCamera rc = Renderer::MakeCamera(WorldMatrix(cam), c.fov, aspect, c.nearClip, c.farClip, c.orthographic, c.orthoSize);
    view = rc.view;
    proj = rc.projection;
    position = rc.position;
    return true;
}

void Scene::RenderGameUI(int w, int h, const vec2& mouse, bool mouseDown, bool mousePressed) {
    UI::BeginFrame(w, h, mouse, mouseDown, mousePressed);
    mat4 view, proj;
    vec3 pos;
    if (GetRuntimeCamera((float)w / std::max(1, h), view, proj, pos)) UI::SetCamera(proj * view);

    // Retained UI elements (sorted by layer)
    std::vector<std::pair<int, EntityId>> elements;
    m_Registry.View<UIElementComponent>([&](EntityId id, UIElementComponent& u) {
        if (u.visible && IsActiveInHierarchy(Entity(id, this))) elements.push_back({ u.layer, id });
    });
    std::stable_sort(elements.begin(), elements.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<std::string> clicked;
    for (auto& [layer, id] : elements) {
        auto& u = m_Registry.Get<UIElementComponent>(id);
        UI::Rect r = UI::Anchored(u.anchor, u.position, u.size);
        switch (u.type) {
        case UIType::Panel: UI::Panel(r, u.color); break;
        case UIType::Text: UI::Text(r.pos, u.text, u.fontSize, u.textColor, UI::Align::Left); break;
        case UIType::Button:
            if (UI::Button(u.text, r, u.fontSize) && m_Running) clicked.push_back(Entity(id, this).Name());
            break;
        case UIType::ProgressBar: UI::ProgressBar(r, u.value, u.color); break;
        case UIType::Image: UI::Image(r, AssetManager::GetTexture(u.image), u.color); break;
        }
    }
    if (m_Running) {
        ForEachScript(m_Registry, this, [&](ScriptInstance&, ScriptableEntity& inst) { inst.OnGUI(); });
        for (auto& name : clicked)
            ForEachScript(m_Registry, this, [&](ScriptInstance&, ScriptableEntity& inst) { inst.OnUIButton(name); });
    }
    UI::EndFrame();
}

void Scene::BroadcastMessage(const std::string& message) {
    ForEachScript(m_Registry, this, [&](ScriptInstance&, ScriptableEntity& inst) { inst.OnMessage(message); });
}

Ref<Scene> Scene::Copy(Scene& other) {
    auto scene = MakeRef<Scene>();
    SceneSerializer src(other);
    json j = src.ToJson();
    SceneSerializer dst(*scene);
    dst.FromJson(j);
    scene->name = other.name;
    scene->path = other.path;
    // Share already-built terrain/nav data to avoid rebuilding when entering play mode.
    scene->m_Nav->settings = other.m_Nav->settings;
    return scene;
}

} // namespace nd
