// Small world behaviours: collectibles, campfire checkpoints, portals, platforms.
#include <nd/9divine.h>
#include "GameManager.h"
#include "PlayerController.h"

using namespace nd;

// Floating ember: spins, bobs, and is collected when the player touches it.
class Ember : public ScriptableEntity {
public:
    float spinSpeed = 90.0f;
    float bobHeight = 0.25f;
    void Reflect(FieldVisitor& v) override { v.Float("Spin Speed", spinSpeed); v.Float("Bob Height", bobHeight); }
    void OnCreate() override { m_Base = Transform().position.y; m_Phase = Transform().position.x * 0.37f; }
    void OnUpdate(float dt) override {
        m_Time += dt;
        auto& t = Transform();
        t.rotation = glm::normalize(glm::angleAxis(glm::radians(spinSpeed * dt), vec3(0, 1, 0)) * t.rotation);
        t.position.y = m_Base + std::sin(m_Time * 2.0f + m_Phase) * bobHeight;
    }
    void OnTriggerEnter(Entity other) override {
        if (m_Collected || other.Get<TagComponent>().tag != "Player") return;
        m_Collected = true;
        if (auto* gm = GameManager::Instance()) gm->CollectEmber();
        DestroySelf();
    }
private:
    float m_Base = 0.0f, m_Time = 0.0f, m_Phase = 0.0f;
    bool m_Collected = false;
};
ND_SCRIPT(Ember)

// Campfire: flickering light; standing next to it heals and saves a checkpoint.
class Campfire : public ScriptableEntity {
public:
    uint64_t light = 0;
    float baseIntensity = 3.0f;
    void Reflect(FieldVisitor& v) override { v.EntityRef("Light", light); v.Float("Base Intensity", baseIntensity); }
    void OnUpdate(float dt) override {
        m_Time += dt;
        if (Entity l = FindByUUID(light)) {
            float f = std::sin(m_Time * 11.0f) * 0.15f + std::sin(m_Time * 23.0f + 1.3f) * 0.1f + std::sin(m_Time * 5.0f) * 0.1f;
            l.Get<LightComponent>().intensity = baseIntensity * (1.0f + f);
        }
        if (m_PlayerInside) {
            if (auto* pc = GetScriptOn<PlayerController>(m_Player)) pc->Heal(20.0f * dt);
        }
    }
    void OnTriggerEnter(Entity other) override {
        if (other.Get<TagComponent>().tag != "Player") return;
        m_PlayerInside = true;
        m_Player = other;
        if (auto* pc = GetScriptOn<PlayerController>(other)) pc->Heal(1000.0f);
        if (SaveGame::Checkpoint(GetScene()))
            if (auto* gm = GameManager::Instance()) gm->Notify("Checkpoint saved - health restored", 2.5f);
    }
    void OnTriggerExit(Entity other) override {
        if (other.Get<TagComponent>().tag == "Player") m_PlayerInside = false;
    }
private:
    float m_Time = 0.0f;
    bool m_PlayerInside = false;
    Entity m_Player;
};
ND_SCRIPT(Campfire)

// Loads another scene when the player walks in.
class Portal : public ScriptableEntity {
public:
    std::string targetScene = "Scenes/Arena.scene";
    void Reflect(FieldVisitor& v) override { v.String("Target Scene", targetScene); }
    void OnUpdate(float dt) override {
        Transform().rotation = glm::normalize(glm::angleAxis(dt * 1.5f, vec3(0, 1, 0)) * Transform().rotation);
    }
    void OnTriggerEnter(Entity other) override {
        if (other.Get<TagComponent>().tag == "Player" && !targetScene.empty()) SceneManager::LoadScene(targetScene);
    }
};
ND_SCRIPT(Portal)

// Rotates an object continuously.
class Spinner : public ScriptableEntity {
public:
    vec3 axis{ 0, 1, 0 };
    float degreesPerSecond = 45.0f;
    void Reflect(FieldVisitor& v) override { v.Vec3("Axis", axis); v.Float("Degrees/Second", degreesPerSecond); }
    void OnUpdate(float dt) override {
        if (glm::length(axis) < 1e-4f) return;
        Transform().rotation = glm::normalize(Transform().rotation * glm::angleAxis(glm::radians(degreesPerSecond * dt), glm::normalize(axis)));
    }
};
ND_SCRIPT(Spinner)

// Kinematic platform that moves back and forth (pushes rigid bodies standing on it).
class MovingPlatform : public ScriptableEntity {
public:
    vec3 offset{ 0, 0, 8 };
    float period = 6.0f;
    void Reflect(FieldVisitor& v) override { v.Vec3("Offset", offset); v.Float("Period", period); }
    void OnCreate() override { m_Start = Transform().position; }
    void OnFixedUpdate(float dt) override {
        m_Time += dt;
        float s = (1.0f - std::cos(m_Time / period * 2.0f * PI)) * 0.5f;
        vec3 target = m_Start + offset * s;
        auto& rb = Get<RigidBodyComponent>();
        rb.velocity = (target - Transform().position) / dt;
    }
private:
    vec3 m_Start{ 0.0f };
    float m_Time = 0.0f;
};
ND_SCRIPT(MovingPlatform)

// Arena toy: fires physics balls where the camera looks (Fire action).
class BallLauncher : public ScriptableEntity {
public:
    uint64_t ballPrefab = 0;
    uint64_t camera = 0;
    float speed = 22.0f;
    void Reflect(FieldVisitor& v) override { v.EntityRef("Ball Prefab", ballPrefab); v.EntityRef("Camera", camera); v.Float("Speed", speed); }
    void OnUpdate(float dt) override {
        m_Cooldown -= dt;
        GameManager* gm = GameManager::Instance();
        if ((gm && gm->IsPaused()) || m_Cooldown > 0.0f || !Input::GetAction("Fire")) return;
        Entity prefab = FindByUUID(ballPrefab);
        Entity cam = FindByUUID(camera);
        if (!prefab || !cam) return;
        m_Cooldown = 0.18f;
        vec3 dir = cam.Transform().Forward();
        vec3 origin = GetScene().WorldPosition(cam) + dir * 1.5f;
        Entity ball = Instantiate(prefab, origin);
        ball.SetActive(true);
        ball.Get<TagComponent>().name = "Ball";
        if (auto* rb = ball.TryGet<RigidBodyComponent>()) rb->velocity = dir * speed;
        m_Spawned.push_back(ball);
        if (m_Spawned.size() > 40) { Destroy(m_Spawned.front()); m_Spawned.erase(m_Spawned.begin()); }
        Audio::PlayOneShot("Audio/jump.wav", 0.25f);
    }
private:
    float m_Cooldown = 0.0f;
    std::vector<Entity> m_Spawned;
};
ND_SCRIPT(BallLauncher)
