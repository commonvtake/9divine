#include "PlayerController.h"
#include "GameManager.h"
#include "Interaction.h"
#include "PlayerCombat.h"

using namespace eu;

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
        float inv = 1.0f / std::max(dt, 1e-4f);
        float speed = glm::length(vec2(t.position.x - m_LastPos.x, t.position.z - m_LastPos.z)) * inv;
        float vy = (t.position.y - m_LastPos.y) * inv;
        m_LastPos = t.position;
        if (Entity model = FindByUUID(modelEntity)) if (auto* a = model.TryGet<AnimatorComponent>()) {
            a->SetFloat("Speed", speed);
            a->SetBool("Grounded", std::abs(vy) < 2.0f);   // replicated players: infer from vertical motion
        }
        return;
    }
    m_LastPos = t.position;
    if (m_Dead) return;

    auto& rb = Get<RigidBodyComponent>();
    GameManager* gm = GameManager::Instance();
    // No control while paused or while the screen is fading (a door transition is playing).
    bool blocked = (gm && gm->IsPaused()) || ScreenFade::Busy();
    PlayerCombat* combat = GetScript<PlayerCombat>();
    m_AirTime = rb.grounded ? 0.0f : m_AirTime + dt;
    if (combat && combat->ControlsMovement()) {
        // An attack, dodge or hit reaction is playing: PlayerCombat moves and animates us.
        if (Entity model = FindByUUID(modelEntity))
            if (auto* anim = model.TryGet<AnimatorComponent>()) {
                anim->SetFloat("Speed", glm::length(vec2(rb.velocity.x, rb.velocity.z)));
                anim->SetBool("Grounded", rb.grounded);
            }
        m_Focus = 0;
        if (t.position.y < -15.0f) Die();
        return;
    }

    vec2 input(Input::GetAxis("MoveX"), Input::GetAxis("MoveY"));
    if (Profiler::BenchmarkActive()) {
        // --benchmark: walk a slow figure-eight so the camera, shadows and reflections all
        // move the way they do in real play (an idle player would flatter the numbers).
        float t = (float)Time::Elapsed();
        input = vec2(std::sin(t * 0.5f), std::cos(t * 0.25f));
    }
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
    bool sprint = (Input::GetAction("Sprint") || (Profiler::BenchmarkActive() && std::fmod((float)Time::Elapsed(), 8.0f) > 5.0f)) && !blocked;
    float speed = sprint ? sprintSpeed : moveSpeed;
    vec3 wish = (right * input.x + fwd * input.y) * speed;
    if (combat && combat->autoFight && !blocked) wish = combat->AutoDirection() * moveSpeed * 1.3f;

    vec2 hv(rb.velocity.x, rb.velocity.z);
    vec2 target(wish.x, wish.z);
    float accel = acceleration * (rb.grounded ? 1.0f : 0.35f);
    vec2 delta = target - hv;
    float maxDelta = accel * speed * dt;
    if (glm::length(delta) > maxDelta) delta = glm::normalize(delta) * maxDelta;
    hv += delta;
    rb.velocity.x = hv.x;
    rb.velocity.z = hv.y;

    if (rb.grounded && Input::GetActionPressed("Jump") && !blocked && (!combat || combat->WantsJump())) {
        rb.velocity.y = jumpSpeed;
        Audio::PlayOneShot("Audio/jump.wav", 0.5f);
    }

    // Face the lock-on target (strafing around it), else the direction of travel
    Entity lockTarget = combat ? combat->LockTarget() : Entity();
    if (lockTarget) {
        vec3 to = GetScene().WorldPosition(lockTarget) - t.position;
        to.y = 0.0f;
        if (glm::length(to) > 0.1f) {
            quat want = glm::angleAxis(std::atan2(-to.x, -to.z), vec3(0, 1, 0));
            t.rotation = glm::normalize(glm::slerp(t.rotation, want, 1.0f - std::exp(-16.0f * dt)));
        }
    } else if (glm::length(target) > 0.1f) {
        float yaw = std::atan2(-target.x, -target.y);
        quat want = glm::angleAxis(yaw, vec3(0, 1, 0));
        t.rotation = glm::normalize(glm::slerp(t.rotation, want, 1.0f - std::exp(-12.0f * dt)));
    }

    // Drive the animator: Speed (m/s) feeds the idle/walk/run blend; Grounded switches to the
    // jump/fall state. Short drops (stairs, slopes) under 0.15 s don't count as airborne.
    bool airborne = !rb.grounded && (m_AirTime > 0.15f || rb.velocity.y > 1.0f);
    if (Entity model = FindByUUID(modelEntity))
        if (auto* anim = model.TryGet<AnimatorComponent>()) {
            anim->SetFloat("Speed", glm::length(hv));
            anim->SetBool("Grounded", !airborne);
        }

    // Doors, notes, items: the closest usable thing in front of us
    Interactable* focus = blocked ? nullptr : Interactable::FindBest(GetScene(), t.position, t.Forward());
    m_Focus = focus ? (uint64_t)focus->GetEntity().GetUUID() : 0;
    if (focus && Input::GetActionPressed("Interact")) focus->Interact(GetEntity());

    if (t.position.y < -15.0f) Die();
}

void PlayerController::OnGUI() {
    if (!m_Focus || m_Dead || ScreenFade::Busy()) return;
    Interactable* it = GetScriptOn<Interactable>(FindByUUID(m_Focus));
    if (!it) return;
    // "[E]  Open  Noodle Bar" just below the centre of the screen
    vec2 c = UI::CanvasSize();
    std::string key = Input::IsGamepadConnected() ? "X" : "E";
    std::string text = it->Verb() + (it->label.empty() ? "" : "   " + it->label);
    float tw = UI::MeasureText(text, 30);
    float w = 52 + 16 + tw + 28;
    vec2 at(c.x * 0.5f - w * 0.5f, c.y * 0.5f + 90);
    UI::Panel({ at, { w, 52 } }, { 0, 0, 0, 0.6f });
    UI::PanelBordered({ at + vec2(8, 8), { 36, 36 } }, { 1, 1, 1, 0.92f }, { 1, 1, 1, 1 });
    UI::Text(at + vec2(26, 11), key, 26, { 0.05f, 0.05f, 0.06f, 1 }, UI::Align::Center);
    UI::Text(at + vec2(60, 10), text, 30, { 1, 1, 1, 0.95f });
}

vec3 PlayerController::MoveInputWorld() {
    if (auto* combat = GetScript<PlayerCombat>(); combat && combat->autoFight) return combat->AutoDirection();
    vec2 input(Input::GetAxis("MoveX"), Input::GetAxis("MoveY"));
    if (glm::length(input) > 1.0f) input = glm::normalize(input);
    vec3 fwd(0, 0, -1), right(1, 0, 0);
    if (Entity cam = FindByUUID(cameraEntity)) {
        vec3 f = cam.Transform().Forward();
        f.y = 0.0f;
        if (glm::length(f) > 1e-3f) {
            fwd = glm::normalize(f);
            right = glm::normalize(glm::cross(fwd, vec3(0, 1, 0)));
        }
    }
    return right * input.x + fwd * input.y;
}

void PlayerController::TakeDamage(float amount, const vec3& from) {
    if (m_Invulnerable > 0.0f || m_Dead) return;
    if (auto* combat = GetScript<PlayerCombat>(); combat && combat->Invulnerable()) return;
    health -= amount;
    EU_INFO("Player hit for {} (health {})", amount, health);
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
    EU_INFO("Player died - respawning");
    if (SaveGame::SlotExists(SaveGame::kCheckpointSlot) && SaveGame::LoadCheckpoint()) return;
    SceneManager::ReloadActive();
}

EU_SCRIPT(PlayerController)
