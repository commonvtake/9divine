#include "euphoria/scene/Scene.h"
#include "euphoria/scene/Entity.h"
#include "euphoria/scene/ScriptableEntity.h"
#include "euphoria/scene/SceneSerializer.h"
#include "euphoria/scene/SceneManager.h"
#include "euphoria/physics/Physics.h"
#include "euphoria/ai/Navigation.h"
#include "euphoria/audio/Audio.h"
#include "euphoria/assets/AssetManager.h"
#include "euphoria/terrain/Terrain.h"
#include "euphoria/twod/Sprite2D.h"
#include "euphoria/particles/Particles.h"
#include "euphoria/renderer/Renderer.h"
#include "euphoria/renderer/Renderer2D.h"
#include "euphoria/ui/GameUI.h"
#include "euphoria/ui/ScreenFade.h"
#include "euphoria/scene/CameraShake.h"
#include "euphoria/save/SaveGame.h"
#include "euphoria/core/Application.h"
#include "euphoria/net/Network.h"
#include "euphoria/core/Profiler.h"
#include "euphoria/core/Log.h"
#include <cmath>
#include <algorithm>

namespace eu {

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
    m_Events.OffAll(e.GetUUID());   // its listeners and timers go with it
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

const Scene::CachedTransform& Scene::Resolve(EntityId id, int depth) {
    CachedTransform& c = m_WorldCache[ecs::Index(id)];
    if (c.stamp == m_CacheStamp) return c;
    c.stamp = m_CacheStamp;              // mark first: a parent cycle then terminates here
    auto& t = m_Registry.Get<TransformComponent>(id);
    bool active = m_Registry.Get<TagComponent>(id).active;
    Entity parent = (t.parent && depth < 64) ? FindByUUID(t.parent) : Entity();
    if (parent) {
        const CachedTransform& p = Resolve(parent.Id(), depth + 1);
        c.world = p.world * t.Local();
        c.active = active && p.active;
    } else {
        c.world = t.Local();
        c.active = active;
    }
    return c;
}

void Scene::CacheTransforms() {
    m_CacheStamp++;
    if (m_CacheStamp == 0) {            // wrapped: invalidate everything
        for (auto& c : m_WorldCache) c.stamp = 0;
        m_CacheStamp = 1;
    }
    uint32_t maxIndex = 0;
    m_Registry.View<TransformComponent>([&](EntityId id, TransformComponent&) { maxIndex = std::max(maxIndex, ecs::Index(id)); });
    if (m_WorldCache.size() <= maxIndex) m_WorldCache.resize(maxIndex + 1);
    m_Registry.View<TransformComponent>([&](EntityId id, TransformComponent&) { Resolve(id, 0); });
}

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
                EU_WARN("Unknown script class '{}' on '{}'", s.className, Entity(id, this).Name());
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
    m_Events.Clear();
    m_Tweens.clear();
    ResetEventTriggers();
    m_Registry.View<RigidBodyComponent>([&](EntityId, RigidBodyComponent& rb) { rb.interpValid = false; });
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
    m_Events.Clear();
    m_Tweens.clear();
    ResetEventTriggers();
    m_Running = false;
}

void Scene::EndPlaySession() {
    ScreenFade::Reset();
    CameraShake::Reset();
    Time::ClearHitStop();
    SaveGame::Clear();   // session state (items, unlocked doors) must not leak into the next play
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
    EU_PROFILE_SCOPE("Scripts");
    ForEachScript(m_Registry, this, [&](ScriptInstance& s, ScriptableEntity& inst) {
        if (!s.started) { s.started = true; inst.OnStart(); }
    });
    ForEachScript(m_Registry, this, [&](ScriptInstance&, ScriptableEntity& inst) { inst.OnUpdate(dt); });
}

void Scene::UpdateAnimators(float dt) {
    EU_PROFILE_SCOPE("Animation");
    m_Registry.View<SpriteAnimatorComponent, SpriteRendererComponent>([&](EntityId id, SpriteAnimatorComponent& an, SpriteRendererComponent& sr) {
        if (IsActiveInHierarchy(Entity(id, this))) Sprite2D::Update(an, sr, dt);
    });
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
    EU_PROFILE_SCOPE("Navigation");
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
    CameraShake::Update(Time::UnscaledDelta());
    if (m_Paused) {
        if (m_StepFrames <= 0) { UpdateAudio(); return; }
        m_StepFrames--;
        dt = kFixedStep;
    }
    // Physics runs at a fixed 60 Hz, but frames don't line up with steps: at 144 Hz most
    // frames run zero steps and some run one, so a body drawn at its raw physics position
    // moves in uneven jumps (judder) - and so does the camera following it. Bodies are
    // therefore *drawn* between their previous and current physics positions. Before
    // gameplay runs, the true positions are put back (unless a script moved the body since).
    m_Registry.View<TransformComponent, RigidBodyComponent>([&](EntityId, TransformComponent& t, RigidBodyComponent& rb) {
        if (!rb.interpValid) return;
        if (t.position == rb.shownPos) t.position = rb.physicsPos;   // untouched since we drew it
        else rb.interpValid = false;                                // teleported by a script
    });
    InstantiateScripts();
    UpdateScripts(dt);
    UpdateEventTriggers(dt);

    // Fixed timestep physics
    m_FixedAccumulator += dt;
    int steps = 0;
    while (m_FixedAccumulator >= kFixedStep && steps < 5) {
        m_Registry.View<TransformComponent, RigidBodyComponent>([&](EntityId, TransformComponent& t, RigidBodyComponent& rb) {
            rb.previousPos = rb.interpValid ? rb.physicsPos : t.position;
        });
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
            if (ev.type != CollisionEvent::CollisionExit) {
                TriggerWhen when = ev.type == CollisionEvent::TriggerEnter ? TriggerWhen::TriggerEnter
                                 : ev.type == CollisionEvent::TriggerExit ? TriggerWhen::TriggerExit : TriggerWhen::CollisionEnter;
                DispatchContactRules(a, b, when);
                DispatchContactRules(b, a, when);
            }
        }
        m_Physics->ClearEvents();
        m_FixedAccumulator -= kFixedStep;
        steps++;
        m_Registry.View<TransformComponent, RigidBodyComponent>([&](EntityId, TransformComponent& t, RigidBodyComponent& rb) {
            rb.physicsPos = t.position;
            rb.interpValid = true;
        });
    }
    if (steps == 5) m_FixedAccumulator = 0.0f;
    Profiler::Stats().physicsSteps += steps;
    // Draw each body at (previous -> current) by how far we are into the next step.
    {
        float alpha = glm::clamp(m_FixedAccumulator / kFixedStep, 0.0f, 1.0f);
        m_Registry.View<TransformComponent, RigidBodyComponent>([&](EntityId, TransformComponent& t, RigidBodyComponent& rb) {
            if (!rb.interpValid) return;
            if (!rb.interpolate || rb.type == BodyType::Static || t.position != rb.physicsPos) { rb.interpValid = false; return; }
            t.position = glm::mix(rb.previousPos, rb.physicsPos, alpha);
            rb.shownPos = t.position;
        });
    }

    UpdateNavAgents(dt);
    UpdateAnimators(dt);
    {
        EU_PROFILE_SCOPE("LateUpdate");
        ForEachScript(m_Registry, this, [&](ScriptInstance&, ScriptableEntity& inst) { inst.OnLateUpdate(dt); });
    }
    {
        EU_PROFILE_SCOPE("Events");
        m_Events.Update(dt);      // timers + queued events (from scripts, physics, triggers)
        UpdateTweens(dt);
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
    mat4 world = WorldMatrix(cam);
    if (m_Running) CameraShake::Apply(world);
    RenderCamera rc = Renderer::MakeCamera(world, c.fov, aspect, c.nearClip, c.farClip, c.orthographic, c.orthoSize);
    view = rc.view;
    proj = rc.projection;
    position = rc.position;
    return true;
}

void Scene::RenderGameUI(int w, int h, const vec2& mouse, bool mouseDown, bool mousePressed) {
    UI::BeginFrame(w, h, mouse, mouseDown, mousePressed);
    mat4 view, proj;
    vec3 pos;
    if (GetRuntimeCamera((float)w / std::max(1, h), view, proj, pos)) {
        if (m_Environment.mode2D) {   // the camera the 2D renderer really draws with (snapped, low-res)
            RenderCamera rc;
            rc.view = view;
            rc.projection = proj;
            rc.position = pos;
            rc = Renderer2D::ComputeView(m_Environment, rc, w, h, false).camera;
            view = rc.view;
            proj = rc.projection;
        }
        UI::SetCamera(proj * view);
    }

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
        ScreenFade::UpdateAndDraw(Time::UnscaledDelta());   // on top of everything
    }
    UI::EndFrame();
}

// ----------------------------------------------------------------- Event Triggers
void Scene::ResetEventTriggers() {
    m_TriggersStarted = false;
    m_Registry.View<EventTriggerComponent>([&](EntityId, EventTriggerComponent& et) {
        for (auto& r : et.rules) { r.fired = false; r.subscription = 0; r.timer = 0.0f; }
    });
}

void Scene::UpdateEventTriggers(float dt) {
    EU_PROFILE_SCOPE("Event Triggers");
    bool starting = !m_TriggersStarted;
    m_TriggersStarted = true;
    std::vector<std::pair<EntityId, size_t>> toFire;
    m_Registry.View<EventTriggerComponent>([&](EntityId id, EventTriggerComponent& et) {
        Entity owner(id, this);
        if (!IsActiveInHierarchy(owner)) return;
        UUID ownerId = owner.GetUUID();
        for (size_t i = 0; i < et.rules.size(); i++) {
            EventRule& r = et.rules[i];
            switch (r.when) {
            case TriggerWhen::Start: if (starting) toFire.push_back({ id, i }); break;
            case TriggerWhen::Timer:
                r.timer += dt;
                if (r.timer >= std::max(r.interval, 0.01f)) { r.timer = 0.0f; toFire.push_back({ id, i }); }
                break;
            case TriggerWhen::InputAction:
                if (!r.whenName.empty() && Input::GetActionPressed(r.whenName)) toFire.push_back({ id, i });
                break;
            case TriggerWhen::Event:
                if (!r.subscription && !r.whenName.empty()) {
                    r.subscription = m_Events.On(r.whenName, [this, ownerId, i](const Event&) {
                        Entity o = FindByUUID(ownerId);
                        if (!o || !IsActiveInHierarchy(o)) return;
                        auto* c = o.TryGet<EventTriggerComponent>();
                        if (c && i < c->rules.size()) FireRule(o, c->rules[i]);
                    }, ownerId);
                }
                break;
            default: break;
            }
        }
    });
    for (auto& [id, i] : toFire) {
        Entity o(id, this);
        if (auto* c = o.TryGet<EventTriggerComponent>(); c && i < c->rules.size()) FireRule(o, c->rules[i]);
    }
}

void Scene::DispatchContactRules(Entity self, Entity other, TriggerWhen when) {
    auto* et = self.TryGet<EventTriggerComponent>();
    if (!et || !IsActiveInHierarchy(self)) return;
    const std::string& tag = other.Get<TagComponent>().tag;
    for (auto& r : et->rules)
        if (r.when == when && (r.tagFilter.empty() || r.tagFilter == tag)) FireRule(self, r);
}

void Scene::FireRule(Entity owner, EventRule& rule) {
    if (rule.once && rule.fired) return;
    rule.fired = true;
    UUID ownerId = owner.GetUUID();
    if (rule.delay > 0.0f) {
        EventRule copy = rule;
        m_Events.Invoke(rule.delay, [this, ownerId, copy] { RunAction(ownerId, copy); }, ownerId);
    } else {
        RunAction(ownerId, rule);
    }
}

void Scene::RunAction(UUID ownerId, const EventRule& r) {
    Entity owner = FindByUUID(ownerId);
    Entity target = r.target ? FindByUUID(r.target) : owner;
    switch (r.action) {
    case TriggerDo::EmitEvent:
        if (!r.param.empty()) m_Events.Emit(r.param, json::object(), ownerId);
        break;
    case TriggerDo::Move:
    case TriggerDo::Rotate: {
        if (!target) break;
        auto& t = target.Transform();
        Tween tw;
        tw.target = target.GetUUID();
        tw.rotate = r.action == TriggerDo::Rotate;
        tw.fromP = t.position;
        tw.toP = t.position + r.vector;
        tw.fromR = t.rotation;
        tw.toR = glm::normalize(t.rotation * quat(glm::radians(r.vector)));
        tw.duration = r.duration;
        if (r.duration <= 0.0f) {
            if (tw.rotate) t.rotation = tw.toR; else t.position = tw.toP;
        } else {
            // A new tween on the same target and channel replaces the old one
            m_Tweens.erase(std::remove_if(m_Tweens.begin(), m_Tweens.end(),
                                          [&](const Tween& o) { return o.target == tw.target && o.rotate == tw.rotate; }), m_Tweens.end());
            m_Tweens.push_back(tw);
        }
        break;
    }
    case TriggerDo::SetActive:
        if (!target) break;
        {
            bool& a = target.Get<TagComponent>().active;
            a = r.param == "toggle" ? !a : r.param != "false";
        }
        break;
    case TriggerDo::Destroy:
        if (target) DestroyEntity(target);
        break;
    case TriggerDo::PlaySound:
        if (!r.param.empty()) {
            if (target) Audio::PlayOneShotAt(r.param, WorldPosition(target));
            else Audio::PlayOneShot(r.param);
        }
        break;
    case TriggerDo::SendMessage:
        if (target) {
            if (auto* sc = target.TryGet<ScriptComponent>())
                for (auto& s : sc->scripts) if (s.instance) s.instance->OnMessage(r.param);
        }
        break;
    case TriggerDo::LoadScene:
        if (!r.param.empty()) SceneManager::LoadScene(r.param);
        break;
    case TriggerDo::Log:
        EU_INFO("[{}] {}", owner ? owner.Name() : std::string("Event Trigger"), r.param);
        break;
    }
}

void Scene::UpdateTweens(float dt) {
    for (auto& tw : m_Tweens) {
        tw.t = std::min(tw.t + dt, tw.duration);
        Entity e = FindByUUID(tw.target);
        if (!e) { tw.t = tw.duration; continue; }
        float k = tw.t / std::max(tw.duration, 1e-4f);
        k = k * k * (3.0f - 2.0f * k);   // ease in-out
        auto& t = e.Transform();
        if (tw.rotate) t.rotation = glm::slerp(tw.fromR, tw.toR, k);
        else t.position = glm::mix(tw.fromP, tw.toP, k);
    }
    m_Tweens.erase(std::remove_if(m_Tweens.begin(), m_Tweens.end(), [](const Tween& t) { return t.t >= t.duration; }), m_Tweens.end());
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

} // namespace eu
