#include "PlayerController.h"
#include "GameManager.h"

using namespace nd;

void PlayerController::Reflect(FieldVisitor& v) {
    v.Float("Move Speed", moveSpeed);
    v.Float("Sprint Speed", sprintSpeed);
    v.Float("Jump Speed", jumpSpeed);
    v.Float("Acceleration", acceleration);
    v.Float("Max Health", maxHealth);
    v.Float("Health", health);
    v.EntityRef("Camera", cameraEntity);
    v.EntityRef("Model", modelEntity);
}

void PlayerController::OnCreate() {
    m_LastPos = Transform().position;
}

void PlayerController::OnUpdate(float dt) {
    m_Invulnerable = std::max(0.0f, m_Invulnerable - dt);
    m_HurtFlash = std::max(0.0f, m_HurtFlash - dt * 2.0f);
    auto& t = Transform();

    // Remote players (multiplayer) are moved by the network; only animate them.
    if (!Network::IsLocal(GetEntity())) {
        float speed = glm::length(vec2(t.position.x - m_LastPos.x, t.position.z - m_LastPos.z)) / std::max(dt, 1e-4f);
        m_LastPos = t.position;
        if (Entity model = FindByUUID(modelEntity)) if (auto* a = model.TryGet<AnimatorComponent>()) a->SetFloat("Speed", speed);
        return;
    }
    m_LastPos = t.position;
    if (m_Dead) return;

    auto& rb = Get<RigidBodyComponent>();
    GameManager* gm = GameManager::Instance();
    bool blocked = gm && gm->IsPaused();

    vec2 input(Input::GetAxis("MoveX"), Input::GetAxis("MoveY"));
    if (glm::length(input) > 1.0f) input = glm::normalize(input);
    if (blocked) input = vec2(0.0f);

    // Camera-relative directions on the ground plane
    vec3 fwd(0, 0, -1), right(1, 0, 0);
    if (Entity cam = FindByUUID(cameraEntity)) {
        vec3 f = cam.Transform().Forward();
        f.y = 0.0f;
        if (glm::length(f) > 1e-3f) {
            fwd = glm::normalize(f);
            right = glm::normalize(glm::cross(fwd, vec3(0, 1, 0)));
        }
    }
    bool sprint = Input::GetAction("Sprint") && !blocked;
    float speed = sprint ? sprintSpeed : moveSpeed;
    vec3 wish = (right * input.x + fwd * input.y) * speed;

    vec2 hv(rb.velocity.x, rb.velocity.z);
    vec2 target(wish.x, wish.z);
    float accel = acceleration * (rb.grounded ? 1.0f : 0.35f);
    vec2 delta = target - hv;
    float maxDelta = accel * speed * dt;
    if (glm::length(delta) > maxDelta) delta = glm::normalize(delta) * maxDelta;
    hv += delta;
    rb.velocity.x = hv.x;
    rb.velocity.z = hv.y;

    if (rb.grounded && Input::GetActionPressed("Jump") && !blocked) {
        rb.velocity.y = jumpSpeed;
        Audio::PlayOneShot("Audio/jump.wav", 0.5f);
    }

    // Face the direction of travel
    if (glm::length(target) > 0.1f) {
        float yaw = std::atan2(-target.x, -target.y);
        quat want = glm::angleAxis(yaw, vec3(0, 1, 0));
        t.rotation = glm::normalize(glm::slerp(t.rotation, want, 1.0f - std::exp(-12.0f * dt)));
    }

    // Drive the animator: 0 = idle, ~4.5 = walk, ~8 = run
    if (Entity model = FindByUUID(modelEntity))
        if (auto* anim = model.TryGet<AnimatorComponent>()) anim->SetFloat("Speed", glm::length(hv));

    if (t.position.y < -15.0f) Die();
}

void PlayerController::TakeDamage(float amount, const vec3& from) {
    if (m_Invulnerable > 0.0f || m_Dead) return;
    health -= amount;
    ND_INFO("Player hit for {} (health {})", amount, health);
    m_Invulnerable = 0.8f;
    m_HurtFlash = 1.0f;
    Audio::PlayOneShot("Audio/hit.wav", 0.8f);
    vec3 away = Transform().position - from;
    away.y = 0.0f;
    if (glm::length(away) > 1e-3f) away = glm::normalize(away);
    auto& rb = Get<RigidBodyComponent>();
    rb.velocity = away * 9.0f + vec3(0, 5.0f, 0);
    if (health <= 0.0f) Die();
}

void PlayerController::Die() {
    if (m_Dead) return;
    m_Dead = true;
    ND_INFO("Player died - respawning");
    if (SaveGame::SlotExists(SaveGame::kCheckpointSlot) && SaveGame::LoadCheckpoint()) return;
    SceneManager::ReloadActive();
}

ND_SCRIPT(PlayerController)
