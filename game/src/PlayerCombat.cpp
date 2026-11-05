#include "PlayerCombat.h"
#include "PlayerController.h"
#include "GameManager.h"
#include <cstdlib>

using namespace eu;

PlayerCombat* PlayerCombat::s_Local = nullptr;

namespace {

// Times are seconds from the start of the move.
//   name           state        clip        speed length cancel hit from..to  rehit  dmg  reach radius knock launch spike  kdown  air    lunge spd/time  style shake hitstop
const PlayerCombat::Move kMoves[PlayerCombat::MoveCount] = {
    { "Slash",         "Slash1",    "Slash1",    1.0f,  0.40f, 0.22f, 0.08f, 0.20f, 0.0f,   10,  1.2f, 1.25f,  2.5f,  0.0f,  0.0f, false, false, 4.0f, 0.12f,  10, 0.16f, 0.045f },
    { "Backslash",     "Slash2",    "Slash2",    1.0f,  0.40f, 0.22f, 0.08f, 0.20f, 0.0f,   10,  1.2f, 1.25f,  2.5f,  0.0f,  0.0f, false, false, 4.0f, 0.12f,  10, 0.16f, 0.045f },
    { "Chop",          "Slash3",    "Slash3",    1.0f,  0.48f, 0.30f, 0.14f, 0.28f, 0.0f,   14,  1.3f, 1.25f,  3.5f,  0.0f,  0.0f, false, false, 5.0f, 0.16f,  14, 0.22f, 0.06f },
    { "Whirlwind",     "Finisher",  "Finisher",  1.0f,  0.78f, 0.62f, 0.12f, 0.48f, 0.17f,  12,  0.3f, 2.1f,   9.0f,  0.0f,  0.0f, true,  false, 2.0f, 0.10f,  26, 0.40f, 0.08f },
    { "Flurry",        "Flurry",    "Flurry",    1.0f,  0.90f, 0.86f, 0.05f, 0.86f, 0.085f, 3.5f, 1.4f, 1.1f,   0.5f,  0.0f,  0.0f, false, false, 1.5f, 0.90f,  5,  0.08f, 0.02f },
    { "Lunge",         "Stinger",   "Stinger",   1.0f,  0.60f, 0.50f, 0.06f, 0.45f, 0.0f,   18,  1.0f, 1.2f,  11.0f,  0.0f,  0.0f, false, false, 14.0f, 0.38f, 30, 0.45f, 0.10f },
    { "Launcher",      "Launcher",  "Launcher",  1.0f,  0.55f, 0.30f, 0.08f, 0.25f, 0.0f,   10,  1.1f, 1.3f,   1.0f, 11.5f,  0.0f, false, false, 1.0f, 0.10f,  22, 0.25f, 0.06f },
    { "Cleave",        "Cleave",    "Slash3",    0.72f, 0.65f, 0.45f, 0.19f, 0.39f, 0.0f,   20,  1.6f, 1.9f,   7.0f,  0.0f,  0.0f, true,  false, 3.0f, 0.20f,  24, 0.45f, 0.09f },
    { "Air Slash",     "AirSlash1", "AirSlash1", 1.0f,  0.33f, 0.16f, 0.04f, 0.14f, 0.0f,    9,  1.2f, 1.3f,   1.0f,  0.0f,  0.0f, false, true,  0.0f, 0.0f,   12, 0.14f, 0.04f },
    { "Air Backslash", "AirSlash2", "AirSlash2", 1.0f,  0.33f, 0.16f, 0.04f, 0.14f, 0.0f,    9,  1.2f, 1.3f,   1.0f,  0.0f,  0.0f, false, true,  0.0f, 0.0f,   12, 0.14f, 0.04f },
    { "Air Flip",      "AirSlash3", "AirSlash3", 1.0f,  0.45f, 0.32f, 0.10f, 0.28f, 0.0f,   13,  1.2f, 1.4f,   2.0f,  0.0f, 16.0f, false, true,  0.0f, 0.0f,   20, 0.30f, 0.07f },
};
// The plunge's landing (not in the table: it starts on touchdown, not on a button)
const PlayerCombat::Move kSlam = { "Plunge", "DiveSlam", "DiveSlam", 1.0f, 0.45f, 0.30f, 0.0f, 0.0f, 0.0f, 18, 0.0f, 3.0f, 8.0f, 0.0f, 0.0f, true, false, 0, 0, 28, 0.6f, 0.1f };

vec3 Flat(vec3 v) { v.y = 0.0f; return glm::length(v) > 1e-4f ? glm::normalize(v) : vec3(0.0f); }
float Rand01() { return (float)std::rand() / (float)RAND_MAX; }

void PlaySfx(const std::string& path, float volume, float pitchSpread = 0.1f) {
    SoundParams p;
    p.volume = volume;
    p.pitch = 1.0f + (Rand01() * 2.0f - 1.0f) * pitchSpread;
    Audio::Play(path, p);
}

} // namespace

const PlayerCombat::Move& PlayerCombat::GetMove(MoveId id) { return kMoves[id]; }

void PlayerCombat::Reflect(FieldVisitor& v) {
    v.EntityRef("Model", modelEntity);
    v.EntityRef("Sword", swordEntity);
    v.EntityRef("Trail", trailEntity);
    v.Float("Damage Scale", damageScale);
    v.Bool("Auto Fight (demo)", autoFight);
}

AnimatorComponent* PlayerCombat::Anim() {
    Entity m = Model();
    return m ? m.TryGet<AnimatorComponent>() : nullptr;
}

void PlayerCombat::OnCreate() {
    team = Team::Player;
    if (Network::IsLocal(GetEntity())) s_Local = this;
    if (auto* a = Anim()) {
        for (auto& m : kMoves) a->AddClipState(m.state, m.clip, false, m.speed);
        for (const char* c : { "DiveRaise", "DiveSlam", "Dodge", "Hit" }) a->AddClipState(c, c);
    }
}

void PlayerCombat::OnStart() {
    SetSwordDrawn(false);   // after every OnCreate, so the sword's AttachToBone exists
}

void PlayerCombat::OnDestroy() {
    if (s_Local == this) s_Local = nullptr;
}

void PlayerCombat::Press(Command c) {
    m_Buffer = c == Command::Attack ? Buffered::Attack : c == Command::Heavy ? Buffered::Heavy : Buffered::Dodge;
    m_BufferAge = 0.0f;
}

std::string PlayerCombat::CurrentAction() const {
    switch (m_Action) {
    case Action::Attack: return kMoves[m_Move].name;
    case Action::Dodge: return "Dodge";
    case Action::Dive: return "Plunge";
    case Action::Slam: return "Plunge";
    case Action::Hurt: return "Hurt";
    default: return "";
    }
}

void PlayerCombat::OnMessage(const std::string& message) {
    if (message == "Attack") Press(Command::Attack);
    else if (message == "Heavy") Press(Command::Heavy);
    else if (message == "Dodge") Press(Command::Dodge);
}

// Attract mode: walk up to the nearest enemy and play through routines that show off the
// moveset. Letters: A attack, H heavy, L launcher (back + attack), S lunge (forward + attack),
// D dodge sideways, '.' a short beat, '_' the pause for the pause combo.
void PlayerCombat::UpdateAutoFight(float dt) {
    static const char* kRoutines[] = { "A.A.A.A", "L..A.A.A.H", "A.A_A", "S.A.A.A.A", "H", "D.L..A.A.A", "A.A.A.A_D" };
    m_AutoDir = vec3(0.0f);
    Combatant* target = nullptr;
    float best = 1e9f;
    vec3 pos = Transform().position;
    for (Combatant* c : combat::All(GetScene(), Team::Enemy)) {
        float d = glm::distance(GetScene().WorldPosition(c->GetEntity()), pos);
        if (d < best) { best = d; target = c; }
    }
    if (!target) return;
    m_Lock = target->GetEntity().GetUUID();
    vec3 to = Flat(GetScene().WorldPosition(target->GetEntity()) - pos);
    const std::string routine = kRoutines[m_AutoRoutine % (sizeof(kRoutines) / sizeof(kRoutines[0]))];
    char next = m_AutoStep < routine.size() ? routine[m_AutoStep] : 0;
    bool ranged = next == 'S' || next == 'D' || next == '.' || next == '_';
    if (best > 2.0f && !ranged && m_Action == Action::None && Get<RigidBodyComponent>().grounded) m_AutoDir = to;   // close in
    m_AutoTimer -= dt;
    if (m_AutoTimer > 0.0f || (!ranged && best > 2.4f && Get<RigidBodyComponent>().grounded)) return;
    if (next == 0) { m_AutoRoutine++; m_AutoStep = 0; m_AutoTimer = 0.5f; return; }
    m_AutoStep++;
    m_AutoTimer = 0.12f;
    switch (next) {
    case 'A': Press(Command::Attack); break;
    case 'H': Press(Command::Heavy); break;
    case 'L': m_AutoDir = -to; Press(Command::Attack); break;
    case 'S': m_AutoDir = to; Press(Command::Attack); break;
    case 'D': m_AutoDir = vec3(-to.z, 0, to.x); Press(Command::Dodge); break;
    case '.': m_AutoTimer = 0.22f; break;
    case '_': m_AutoTimer = 0.55f; break;
    }
}

bool PlayerCombat::IsDead() const {
    auto* self = const_cast<PlayerCombat*>(this);
    auto* pc = self->GetScript<PlayerController>();
    return pc && pc->IsDead();
}

bool PlayerCombat::Airborne() { return !Get<RigidBodyComponent>().grounded; }

bool PlayerCombat::Invulnerable() const {
    return (m_Action == Action::Dodge && m_Time > 0.02f && m_Time < 0.36f) || m_Action == Action::Dive;
}

vec3 PlayerCombat::InputDirection() {
    auto* pc = GetScript<PlayerController>();
    return pc ? pc->MoveInputWorld() : vec3(0.0f);
}

bool PlayerCombat::WantsJump() const {
    auto* self = const_cast<PlayerCombat*>(this);
    return m_Action == Action::None && !(self->LockTarget() && glm::length(self->InputDirection()) > 0.3f);
}

Entity PlayerCombat::LockTarget() {
    Entity e = FindByUUID(m_Lock);
    if (!e || !e.IsActive()) return {};
    auto* c = GetScriptOn<Combatant>(e);
    return c && !c->IsDead() ? e : Entity();
}

void PlayerCombat::FaceTowards(const vec3& dir, bool snap) {
    vec3 d = Flat(dir);
    if (glm::length(d) < 0.5f) return;
    quat want = glm::angleAxis(combat::YawOf(d), vec3(0, 1, 0));
    auto& t = Transform();
    t.rotation = snap ? want : glm::normalize(glm::slerp(t.rotation, want, 0.25f));
}

void PlayerCombat::SetSwordDrawn(bool drawn) {
    m_SwordDrawn = drawn;
    Entity s = FindByUUID(swordEntity);
    auto* at = s ? GetScriptOn<AttachToBone>(s) : nullptr;
    if (!at) return;
    if (drawn) {
        at->bone = "Hand.R";
        at->offset = { 0.0f, -0.07f, 0.02f };
        at->rotation = vec3(0.0f);
    } else {
        // Across the back, hilt over the right shoulder
        at->bone = "Chest";
        at->offset = { -0.1f, 0.3f, -0.17f };
        at->rotation = { 100.0f, 0.0f, -28.0f };
    }
}

// ---------------------------------------------------------------- lock-on
void PlayerCombat::UpdateLockOn() {
    GameManager* gm = GameManager::Instance();
    if (autoFight) return;   // UpdateAutoFight picks the target
    bool held = Input::GetAction("LockOn") && !(gm && gm->IsPaused()) && !ScreenFade::Busy();
    if (!held) { m_Lock = 0; return; }
    m_DrawnTimer = std::max(m_DrawnTimer, 3.0f);
    vec3 pos = Transform().position;
    if (Entity cur = LockTarget()) {
        if (glm::distance(GetScene().WorldPosition(cur), pos) < 22.0f) return;
    }
    // Pick the enemy closest to where the camera looks
    vec3 look(0, 0, -1);
    if (auto* pc = GetScript<PlayerController>()) if (Entity cam = FindByUUID(pc->cameraEntity)) look = Flat(cam.Transform().Forward());
    float best = 1e9f;
    m_Lock = 0;
    for (Combatant* c : combat::All(GetScene(), Team::Enemy)) {
        vec3 to = GetScene().WorldPosition(c->GetEntity()) - pos;
        float d = glm::length(to);
        if (d > 16.0f) continue;
        float score = d - 6.0f * glm::dot(Flat(to), look);
        if (score < best) { best = score; m_Lock = c->GetEntity().GetUUID(); }
    }
}

// ---------------------------------------------------------------- moves
void PlayerCombat::StartMove(MoveId id) {
    const Move& m = kMoves[id];
    auto& rb = Get<RigidBodyComponent>();
    m_Action = Action::Attack;
    m_Move = id;
    m_Time = 0.0f;
    m_Landed = false;
    m_HitSet.clear();
    m_RehitTimer = 0.0f;
    m_Step = id <= Finisher ? (int)id : -1;
    m_SinceString = 99.0f;
    SetSwordDrawn(true);
    m_DrawnTimer = 8.0f;

    // Aim: the lock-on target, else the stick, else the nearest enemy roughly in front
    vec3 pos = Transform().position;
    vec3 aim(0.0f);
    if (Entity t = LockTarget()) aim = GetScene().WorldPosition(t) - pos;
    else {
        vec3 in = InputDirection();
        vec3 ref = glm::length(in) > 0.3f ? in : Transform().Forward();
        float best = 4.5f;
        for (Combatant* c : combat::All(GetScene(), Team::Enemy)) {
            vec3 to = GetScene().WorldPosition(c->GetEntity()) - pos;
            float d = glm::length(vec2(to.x, to.z));
            if (d < best && glm::dot(Flat(to), Flat(ref)) > 0.35f) { best = d; aim = to; }
        }
        if (glm::length(aim) < 1e-3f) aim = in;
    }
    FaceTowards(aim, true);

    if (auto* a = Anim()) a->Play(m.state, 0.05f, true);
    PlaySfx(id == Cleave ? "Audio/sword_heavy.wav" : "Audio/sword_swing.wav", 0.55f, 0.12f);
    m_LastMoveName = m.name;
    m_MoveNameTimer = 1.2f;
    if (m.air) {
        rb.velocity.y = std::max(rb.velocity.y, 1.2f);
        rb.gravityScale = 0.12f;
        rb.velocity.x = rb.velocity.z = 0.0f;
    }
}

void PlayerCombat::StartDodge(const vec3& dir) {
    vec3 d = Flat(dir);
    if (glm::length(d) < 0.5f) d = -Flat(Transform().Forward());
    m_Action = Action::Dodge;
    m_Time = 0.0f;
    m_DodgeDir = d;
    m_JustDodged = false;
    FaceTowards(d, true);
    if (auto* a = Anim()) a->Play("Dodge", 0.04f, true);
    PlaySfx("Audio/dodge.wav", 0.6f);
    Get<RigidBodyComponent>().gravityScale = 1.0f;
}

void PlayerCombat::StartDive() {
    m_Action = Action::Dive;
    m_Time = 0.0f;
    SetSwordDrawn(true);
    m_DrawnTimer = 8.0f;
    if (auto* a = Anim()) a->Play("DiveRaise", 0.05f, true);
    PlaySfx("Audio/sword_swing.wav", 0.5f);
    auto& rb = Get<RigidBodyComponent>();
    rb.gravityScale = 0.0f;
    rb.velocity = vec3(0, 1.0f, 0);
}

void PlayerCombat::EndAction() {
    auto& rb = Get<RigidBodyComponent>();
    if (m_Action == Action::Attack && m_Move <= Finisher) m_SinceString = 0.0f;
    if (m_Action == Action::Attack && m_Move == Finisher) m_Step = -1;
    m_Action = Action::None;
    rb.gravityScale = 1.0f;
    if (Entity tr = FindByUUID(trailEntity)) if (auto* pe = tr.TryGet<ParticleEmitterComponent>()) pe->playing = false;
    if (auto* a = Anim()) a->Play(rb.grounded ? "Locomotion" : "Airborne", 0.15f);
}

void PlayerCombat::OnLanded(const std::vector<Combatant*>& hits, const Move& m) {
    m_Landed = true;
    Time::HitStop(m.hitStop);
    CameraShake::Add(m.shake);
    bool juggle = false;
    vec3 pos = Transform().position + vec3(0, 1.0f, 0);
    for (Combatant* c : hits) {
        vec3 aim = c->AimPoint();
        vec3 at = aim + Flat(pos - aim) * 0.35f;
        combat::Burst(GetScene(), at, { 1.0f, 0.9f, 0.6f, 1.0f }, 18, 7.0f, 0.1f, 0.3f);
        combat::Burst(GetScene(), at, { 1.0f, 0.15f, 0.1f, 1.0f }, 8, 3.0f, 0.18f, 0.45f);
        juggle |= c->Airborne();
    }
    float points = m.style * (1.0f + 0.3f * (float)(hits.size() - 1)) * (juggle ? 1.3f : 1.0f);
    m_Style.Add(m.name, points);
    m_Combo += (int)hits.size();
    m_ComboTimer = 2.5f;
    m_DrawnTimer = 8.0f;
    PlaySfx(m.knockdown || m.launch > 0.0f || m.spike > 0.0f ? "Audio/sword_heavy_hit.wav" : "Audio/sword_hit.wav", 0.75f, 0.12f);
}

void PlayerCombat::UpdateAttack(float dt) {
    const Move& m = kMoves[m_Move];
    auto& rb = Get<RigidBodyComponent>();
    m_Time += dt;
    vec3 fwd = Flat(Transform().Forward());
    vec3 pos = Transform().position;

    // Forward motion: a step into each swing, or the lunge's dash
    float speed = m_Time < m.lungeTime ? m.lungeSpeed : 0.0f;
    if (m_Move == Stinger) {
        if (m_Time < 0.05f || m_Landed) speed = 0.0f;
        if (Entity t = LockTarget()) if (glm::distance(GetScene().WorldPosition(t), pos) < 1.3f) speed = 0.0f;
    }
    rb.velocity.x = fwd.x * speed;
    rb.velocity.z = fwd.z * speed;
    if (m.air) {
        rb.gravityScale = 0.12f;
        rb.velocity.y = std::max(rb.velocity.y, -1.0f);
    }

    // Blade trail just around the live frames
    bool trail = m_Time >= m.hitStart - 0.04f && m_Time <= m.hitEnd + 0.03f;
    if (Entity tr = FindByUUID(trailEntity)) if (auto* pe = tr.TryGet<ParticleEmitterComponent>()) pe->playing = trail;

    if (m_Time >= m.hitStart && m_Time <= m.hitEnd) {
        if (m.rehit > 0.0f) {
            m_RehitTimer -= dt;
            if (m_RehitTimer <= 0.0f) { m_HitSet.clear(); m_RehitTimer = m.rehit; }
        }
        HitInfo h;
        h.damage = m.damage * damageScale;
        h.from = pos;
        h.knockback = m.knockback;
        h.launch = m.launch;
        h.spike = m.spike;
        h.knockdown = m.knockdown;
        h.airHit = m.air;
        h.stagger = m_Move == Flurry ? 0.22f : 0.45f;
        h.move = m.name;
        vec3 center = pos + fwd * m.reach + vec3(0, 1.0f, 0);
        auto hits = combat::Sweep(GetScene(), Team::Player, center, m.radius, h, m_HitSet);
        if (!hits.empty()) OnLanded(hits, m);
    }

    // Launcher: keep holding Attack to jump up after the enemy
    if (m_Move == Launcher && m_Time >= m.hitEnd && (Input::GetAction("Attack") || autoFight) && rb.grounded) {
        EndAction();
        rb.velocity.y = 11.5f;
        rb.grounded = false;
        if (auto* a = Anim()) a->Play("Airborne", 0.1f);
        return;
    }
    if (m_Time >= m.length) EndAction();
}

// ---------------------------------------------------------------- frame
void PlayerCombat::OnUpdate(float dt) {
    auto* pc = GetScript<PlayerController>();
    if (!pc || !Network::IsLocal(GetEntity()) || pc->IsDead()) return;
    GameManager* gm = GameManager::Instance();
    bool blocked = (gm && gm->IsPaused()) || ScreenFade::Busy();
    auto& rb = Get<RigidBodyComponent>();
    bool grounded = rb.grounded;

    m_Style.Update(dt);
    m_ComboTimer -= dt;
    if (m_ComboTimer <= 0.0f) m_Combo = 0;
    m_MoveNameTimer -= dt;
    m_CloseCallTimer -= Time::UnscaledDelta();
    m_LockPulse += dt;
    if (grounded) { m_AirStep = 0; m_AirStringUsed = false; }
    if (autoFight && !blocked) UpdateAutoFight(dt);
    UpdateLockOn();

    // Input is buffered for a moment, so a press slightly early still chains
    if (!blocked) {
        if (Input::GetActionPressed("Attack")) { m_Buffer = Buffered::Attack; m_BufferAge = 0.0f; }
        if (Input::GetActionPressed("Heavy")) { m_Buffer = Buffered::Heavy; m_BufferAge = 0.0f; }
        bool rollJump = LockTarget() && Input::GetActionPressed("Jump") && glm::length(InputDirection()) > 0.3f;
        if (Input::GetActionPressed("Dodge") || rollJump) { m_Buffer = Buffered::Dodge; m_BufferAge = 0.0f; }
    }
    m_BufferAge += dt;
    if (m_BufferAge > 0.35f) m_Buffer = Buffered::None;
    if (m_Action == Action::None) m_SinceString += dt;

    switch (m_Action) {
    case Action::None: break;
    case Action::Attack: UpdateAttack(dt); break;
    case Action::Dodge: {
        m_Time += dt;
        float speed = m_Time < 0.36f ? 9.0f : 9.0f * std::max(0.0f, 1.0f - (m_Time - 0.36f) / 0.14f);
        rb.velocity.x = m_DodgeDir.x * speed;
        rb.velocity.z = m_DodgeDir.z * speed;
        if (m_Time >= 0.5f) EndAction();
        break;
    }
    case Action::Dive: {
        m_Time += dt;
        if (m_Time < 0.2f) { rb.velocity = vec3(0, 0.8f, 0); rb.gravityScale = 0.0f; }
        else { rb.velocity = vec3(0, -26.0f, 0); rb.gravityScale = 1.0f; }
        if (Entity tr = FindByUUID(trailEntity)) if (auto* pe = tr.TryGet<ParticleEmitterComponent>()) pe->playing = m_Time >= 0.2f;
        if (m_Time > 0.2f && (grounded || m_Time > 4.0f)) {
            // Touchdown: slam everything around
            m_Action = Action::Slam;
            m_Time = 0.0f;
            rb.velocity = vec3(0.0f);
            if (auto* a = Anim()) a->Play("DiveSlam", 0.02f, true);
            vec3 pos = Transform().position;
            HitInfo h;
            h.damage = kSlam.damage * damageScale;
            h.from = pos;
            h.knockback = kSlam.knockback;
            h.knockdown = true;
            h.move = kSlam.name;
            m_HitSet.clear();
            auto hits = combat::Sweep(GetScene(), Team::Player, pos + vec3(0, 0.6f, 0), kSlam.radius, h, m_HitSet);
            if (!hits.empty()) OnLanded(hits, kSlam);
            CameraShake::Add(0.35f);
            combat::Burst(GetScene(), pos + vec3(0, 0.1f, 0), { 0.7f, 0.65f, 0.6f, 0.8f }, 40, 9.0f, 0.3f, 0.5f);
            PlaySfx("Audio/sword_heavy_hit.wav", 0.8f, 0.05f);
        }
        break;
    }
    case Action::Slam:
        m_Time += dt;
        rb.velocity.x = rb.velocity.z = 0.0f;
        if (Entity tr = FindByUUID(trailEntity)) if (auto* pe = tr.TryGet<ParticleEmitterComponent>()) pe->playing = false;
        if (m_Time >= kSlam.length) EndAction();
        break;
    case Action::Hurt:
        m_Time += dt;
        rb.velocity.x *= 1.0f / (1.0f + 6.0f * dt);
        rb.velocity.z *= 1.0f / (1.0f + 6.0f * dt);
        if (m_Time >= 0.42f) EndAction();
        break;
    }

    // Act on what's buffered when free, or when the current move allows a cancel
    bool canAct = m_Action == Action::None ||
                  (m_Action == Action::Attack && m_Time >= kMoves[m_Move].cancel) ||
                  (m_Action == Action::Slam && m_Time >= kSlam.cancel) ||
                  (m_Action == Action::Dodge && m_Time >= 0.36f);
    if (canAct && m_Buffer != Buffered::None && !blocked) {
        Buffered b = m_Buffer;
        m_Buffer = Buffered::None;
        bool inString = m_Action == Action::Attack && m_Step >= 0 && m_Step < 3;
        if (b == Buffered::Dodge) {
            if (grounded) StartDodge(InputDirection());
        } else if (b == Buffered::Heavy) {
            if (grounded) StartMove(Cleave);
            else StartDive();
        } else if (!grounded) {
            if (!m_AirStringUsed) {
                StartMove(MoveId(Air1 + m_AirStep));
                if (++m_AirStep >= 3) m_AirStringUsed = true;
            }
        } else {
            Entity lock = LockTarget();
            vec3 in = InputDirection();
            float along = 0.0f;
            if (lock && glm::length(in) > 0.4f) along = glm::dot(Flat(in), Flat(GetScene().WorldPosition(lock) - Transform().position));
            if (along < -0.5f) StartMove(Launcher);
            else if (along > 0.5f) StartMove(Stinger);
            else if (inString) StartMove(MoveId(m_Step + 1));
            else if (m_Step >= 0 && m_Step < 3 && m_SinceString < 0.3f) StartMove(MoveId(m_Step + 1));
            else if (m_Step == 1 && m_SinceString < 1.0f) StartMove(Flurry);    // the pause combo
            else StartMove(Slash1);
        }
    }

    // Put the sword away once things have been quiet for a while
    m_DrawnTimer -= dt;
    if (m_SwordDrawn && m_DrawnTimer <= 0.0f && m_Action == Action::None) {
        bool enemiesNear = false;
        for (Combatant* c : combat::All(GetScene(), Team::Enemy))
            if (glm::distance(GetScene().WorldPosition(c->GetEntity()), Transform().position) < 15.0f) enemiesNear = true;
        if (!enemiesNear) SetSwordDrawn(false);
        else m_DrawnTimer = 2.0f;
    }
}

bool PlayerCombat::ReceiveHit(const HitInfo& hit) {
    auto* pc = GetScript<PlayerController>();
    if (!pc || pc->IsDead()) return false;
    if (Invulnerable()) {
        // Dodged through an attack at the last moment: style bonus and a beat of slow motion
        if (m_Action == Action::Dodge && !m_JustDodged) {
            m_JustDodged = true;
            m_Style.Bonus(30.0f);
            m_CloseCallTimer = 1.2f;
            Time::HitStop(0.25f, 0.25f);
        }
        return false;
    }
    float before = pc->health;
    pc->TakeDamage(hit.damage, hit.from);
    if (pc->health >= before) return false;   // still in post-hit invulnerability
    m_Style.Hurt();
    m_Combo = 0;
    m_Action = Action::Hurt;
    m_Time = 0.0f;
    m_Buffer = Buffered::None;
    auto& rb = Get<RigidBodyComponent>();
    rb.gravityScale = 1.0f;
    vec3 away = Flat(Transform().position - hit.from);
    rb.velocity = away * 5.0f + vec3(0, 2.5f, 0);
    if (Entity tr = FindByUUID(trailEntity)) if (auto* pe = tr.TryGet<ParticleEmitterComponent>()) pe->playing = false;
    if (auto* a = Anim()) a->Play("Hit", 0.04f, true);
    CameraShake::Add(0.4f);
    Time::HitStop(0.06f);
    combat::Burst(GetScene(), AimPoint(), { 1.0f, 0.1f, 0.1f, 1.0f }, 14, 4.0f, 0.15f, 0.4f);
    return true;
}

// ---------------------------------------------------------------- HUD
void PlayerCombat::OnGUI() {
    if (!Network::IsLocal(GetEntity())) return;
    GameManager* gm = GameManager::Instance();
    if (gm && gm->IsPaused()) return;
    vec2 c = UI::CanvasSize();

    // Lock-on reticle and the target's health
    if (Entity t = LockTarget()) {
        auto* tc = GetScriptOn<Combatant>(t);
        vec3 aim = tc->AimPoint();
        vec2 sp;
        if (UI::WorldToCanvas(aim, sp)) {
            float r = 30.0f + 4.0f * std::sin(m_LockPulse * 7.0f);
            vec4 red(1.0f, 0.2f, 0.15f, 0.95f);
            for (int i = 0; i < 4; i++) {
                vec2 dir((i % 2) ? 1.0f : -1.0f, (i / 2) ? 1.0f : -1.0f);
                vec2 corner = sp + dir * r;
                UI::Panel({ corner - vec2(dir.x > 0 ? 14 : 0, dir.y > 0 ? 3 : 0), { 14, 3 } }, red);
                UI::Panel({ corner - vec2(dir.x > 0 ? 3 : 0, dir.y > 0 ? 14 : 0), { 3, 14 } }, red);
            }
            UI::WorldProgressBar(aim + vec3(0, 1.0f, 0), tc->HealthFraction(), { 110, 10 }, { 0.9f, 0.15f, 0.15f, 1.0f });
        }
    }

    // Style rank: a big letter that punches in on every rank up, its word, and the gauge
    int rank = m_Style.Rank();
    if (rank >= 0) {
        vec4 col = combat::StyleMeter::Color(rank);
        float pop = m_Style.RankPop();
        float size = 150.0f * (1.0f + 0.4f * pop * pop);
        vec2 at(c.x - 330.0f, c.y * 0.3f);
        std::string letter = combat::StyleMeter::Letter(rank);
        UI::Text(at + vec2(5, 5), letter, size, { 0, 0, 0, 0.55f });
        UI::Text(at, letter, size, col);
        UI::Text(at + vec2(4, 150.0f * 0.95f + 4), combat::StyleMeter::Word(rank), 36, { 0, 0, 0, 0.55f });
        UI::Text(at + vec2(0, 150.0f * 0.95f), combat::StyleMeter::Word(rank), 36, col);
        UI::ProgressBar({ at + vec2(0, 150.0f + 48.0f), { 240, 10 } }, m_Style.RankProgress(), col);
    }
    if (m_Combo >= 2) {
        float a = glm::clamp(m_ComboTimer, 0.0f, 1.0f);
        UI::Text({ c.x - 330.0f, c.y * 0.3f - 64.0f }, std::to_string(m_Combo) + " HITS", 44, { 1, 1, 1, a });
    }
    if (m_MoveNameTimer > 0.0f && rank >= 0)
        UI::Text({ c.x - 330.0f, c.y * 0.3f + 230.0f }, m_LastMoveName, 28, { 1, 1, 1, glm::clamp(m_MoveNameTimer, 0.0f, 1.0f) * 0.8f });
    if (m_CloseCallTimer > 0.0f)
        UI::Text({ c.x * 0.5f, c.y * 0.5f - 160.0f }, "CLOSE CALL!", 54, { 1.0f, 0.9f, 0.3f, glm::clamp(m_CloseCallTimer, 0.0f, 1.0f) }, UI::Align::Center);
}

EU_SCRIPT(PlayerCombat)
