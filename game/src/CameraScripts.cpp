// Camera behaviours.
#include <nd/9divine.h>
#include "GameManager.h"

using namespace nd;

// Orbit camera that follows a target, avoids clipping through geometry, and zooms.
class ThirdPersonCamera : public ScriptableEntity {
public:
    uint64_t target = 0;
    float distance = 6.0f;
    float minDistance = 2.0f, maxDistance = 14.0f;
    float height = 1.4f;
    float sensitivity = 1.0f;
    float yaw = 0.0f, pitch = 18.0f;

    void Reflect(FieldVisitor& v) override {
        v.EntityRef("Target", target);
        v.Float("Distance", distance);
        v.Float("Min Distance", minDistance);
        v.Float("Max Distance", maxDistance);
        v.Float("Height", height);
        v.Float("Sensitivity", sensitivity);
        v.Float("Yaw", yaw);
        v.Float("Pitch", pitch);
    }

    void OnLateUpdate(float dt) override {
        Entity t = FindByUUID(target);
        if (!t) return;
        GameManager* gm = GameManager::Instance();
        bool look = Input::GetCursorMode() == CursorMode::Locked && !(gm && gm->IsPaused());
        if (look) {
            yaw -= Input::GetAxis("LookX") * sensitivity * (Input::IsGamepadConnected() ? dt * 60.0f : 1.0f);
            pitch += Input::GetAxis("LookY") * sensitivity * (Input::IsGamepadConnected() ? dt * 60.0f : 1.0f);
            pitch = glm::clamp(pitch, -20.0f, 75.0f);
            distance = glm::clamp(distance - Input::GetAxis("Zoom") * 0.8f, minDistance, maxDistance);
        }
        vec3 pivot = GetScene().WorldPosition(t) + vec3(0, height, 0);
        float y = glm::radians(yaw), p = glm::radians(pitch);
        vec3 dir(std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p));
        float d = distance;
        RaycastHit hit;
        if (GetScene().Physics().Raycast({ pivot, dir }, distance, hit, t.Id()) && hit.distance < d) d = std::max(0.5f, hit.distance - 0.3f);
        vec3 pos = pivot + dir * d;
        float ground;
        if (GetScene().Physics().TerrainHeight(pos, ground) && pos.y < ground + 0.4f) pos.y = ground + 0.4f;
        auto& tr = Transform();
        tr.position = glm::mix(tr.position, pos, 1.0f - std::exp(-25.0f * std::max(dt, 1.0f / 240.0f)));
        tr.LookAt(pivot);
    }
};
ND_SCRIPT(ThirdPersonCamera)

// Free-fly camera for spectating / debugging (WASD + mouse, Q/E up/down).
class FlyCamera : public ScriptableEntity {
public:
    float speed = 10.0f;
    float sensitivity = 0.15f;
    void Reflect(FieldVisitor& v) override { v.Float("Speed", speed); v.Float("Sensitivity", sensitivity); }
    void OnUpdate(float dt) override {
        auto& t = Transform();
        if (Input::IsMouseDown(Mouse::Right)) {
            vec2 d = Input::MouseDelta();
            vec3 e = glm::eulerAngles(t.rotation);
            quat yawQ = glm::angleAxis(-d.x * sensitivity * 0.01745f, vec3(0, 1, 0));
            quat pitchQ = glm::angleAxis(-d.y * sensitivity * 0.01745f, t.Right());
            t.rotation = glm::normalize(yawQ * pitchQ * t.rotation);
            (void)e;
        }
        vec3 move(0.0f);
        if (Input::IsKeyDown(Key::W)) move += t.Forward();
        if (Input::IsKeyDown(Key::S)) move -= t.Forward();
        if (Input::IsKeyDown(Key::D)) move += t.Right();
        if (Input::IsKeyDown(Key::A)) move -= t.Right();
        if (Input::IsKeyDown(Key::E)) move += vec3(0, 1, 0);
        if (Input::IsKeyDown(Key::Q)) move -= vec3(0, 1, 0);
        float s = Input::IsKeyDown(Key::LeftShift) ? speed * 3.0f : speed;
        t.position += move * s * dt;
    }
};
ND_SCRIPT(FlyCamera)
