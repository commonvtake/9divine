// Puppet: a melee enemy for hack-and-slash fights.
//
// A crowd of puppets circles the player and takes turns: only enemies holding one of the
// two attack tokens close in. An attack is telegraphed (the arm goes up and the eyes flare)
// so it can be dodged. Hits stagger them, launchers throw them in the air where they can be
// juggled, and finishers knock them flat. A "Heavy" puppet (the Brute) has super armour:
// light hits don't interrupt it until its poise breaks.
#include "Combat.h"
#include "PlayerCombat.h"
#include <cstdlib>

using namespace eu;

namespace {
vec3 Flat(vec3 v) { v.y = 0.0f; return glm::length(v) > 1e-4f ? glm::normalize(v) : vec3(0.0f); }
float Rand(float a, float b) { return a + (b - a) * (float)std::rand() / (float)RAND_MAX; }
}

class Puppet : public Combatant {
public:
    uint64_t modelEntity = 0;
    uint64_t eyesEntity = 0;
    float maxHealth = 90.0f;
    float moveSpeed = 3.0f;
    float damage = 12.0f;
    bool heavy = false;            // Brute: super armour, slower, harder hitting
    float poiseMax = 40.0f;

    void Reflect(FieldVisitor& v) override {
        v.EntityRef("Model", modelEntity);
        v.EntityRef("Eyes", eyesEntity);
        v.Float("Max Health", maxHealth);
        v.Float("Move Speed", moveSpeed);
        v.Float("Damage", damage);
        v.Bool("Heavy", heavy);
        v.Float("Poise", poiseMax);
    }

    void OnCreate() override {
        team = Team::Enemy;
        m_Health = maxHealth;
        m_Poise = poiseMax;
        m_Strafe = Rand(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
        m_Cooldown = Rand(0.6f, 2.0f);
        if (auto* a = Anim()) {
            a->AddClipState("Windup", "Windup");
            a->AddClipState("Strike", "Slash3", false, heavy ? 0.85f : 1.0f);
            for (const char* c : { "Hit", "Launched", "Knockdown", "GetUp" }) a->AddClipState(c, c);
        }
        Enter(State::Spawn);
    }

    void OnDestroy() override { combat::ReleaseToken(GetScene(), GetEntity().GetUUID()); }

    bool IsDead() const override { return m_State == State::Dead; }
    bool Airborne() override { return m_State == State::Air; }
    float HealthFraction() override { return glm::clamp(m_Health / maxHealth, 0.0f, 1.0f); }
    vec3 AimPoint() override { return GetScene().WorldPosition(GetEntity()) + vec3(0, heavy ? 1.45f : 1.1f, 0); }

    void OnUpdate(float dt) override {
        auto& rb = Get<RigidBodyComponent>();
        auto& t = Transform();
        m_Time += dt;
        m_Flash = std::max(0.0f, m_Flash - Time::UnscaledDelta() * 10.0f);   // real time: fades through hit-stop
        m_Float = std::max(0.0f, m_Float - dt);
        UpdateLook();

        if (m_State == State::Dead) {
            rb.velocity.x *= 1.0f / (1.0f + 4.0f * dt);
            rb.velocity.z *= 1.0f / (1.0f + 4.0f * dt);
            if (m_Time > 1.1f && !m_Burst) {
                m_Burst = true;
                combat::Burst(GetScene(), AimPoint() - vec3(0, 0.5f, 0), { 0.5f, 0.05f, 0.1f, 1.0f }, 40, 4.0f, 0.3f, 0.7f);
                combat::Burst(GetScene(), AimPoint() - vec3(0, 0.5f, 0), { 1.0f, 0.4f, 0.3f, 1.0f }, 20, 6.0f, 0.12f, 0.5f);
                DestroySelf();
            }
            return;
        }

        Entity player = Player();
        vec3 pos = t.position;
        vec3 toPlayer = player ? player.Transform().position - pos : vec3(0, 0, 1);
        float dist = glm::length(vec2(toPlayer.x, toPlayer.z));
        vec3 dirPlayer = Flat(toPlayer);
        vec3 wish(0.0f);           // desired horizontal velocity
        bool steer = true;          // false: leave horizontal velocity to knockback

        switch (m_State) {
        case State::Spawn:
            steer = true;
            if (m_Time >= 0.8f) Enter(State::Stalk);
            break;
        case State::Stalk: {
            // Keep a fighting distance and circle; ask for a token when the cooldown is up
            Face(dirPlayer, 6.0f, dt);
            vec3 side(-dirPlayer.z, 0.0f, dirPlayer.x);
            if (dist > 7.0f) wish = dirPlayer * moveSpeed;
            else if (dist < 3.2f) wish = -dirPlayer * moveSpeed * 0.7f;
            else wish = side * m_Strafe * moveSpeed * 0.45f;
            if (Rand(0.0f, 1.0f) < dt * 0.3f) m_Strafe = -m_Strafe;
            m_Cooldown -= dt;
            if (m_Cooldown <= 0.0f && player && dist < 18.0f && combat::RequestToken(GetScene(), GetEntity().GetUUID())) Enter(State::Approach);
            break;
        }
        case State::Approach:
            Face(dirPlayer, 8.0f, dt);
            wish = dirPlayer * moveSpeed * (heavy ? 1.1f : 1.45f);
            if (dist < (heavy ? 2.3f : 1.9f)) Enter(State::Windup);
            else if (m_Time > 4.0f) { combat::ReleaseToken(GetScene(), GetEntity().GetUUID()); Enter(State::Stalk); }
            break;
        case State::Windup:
            Face(dirPlayer, 3.0f, dt);       // tracks a little: dodge sideways at the last moment
            if (m_Time >= (heavy ? 0.85f : 0.55f)) Enter(State::Strike);
            break;
        case State::Strike: {
            vec3 fwd = Flat(t.Forward());
            if (m_Time < 0.16f) wish = fwd * (heavy ? 3.0f : 5.0f);
            if (m_Time >= 0.14f && m_Time <= 0.3f) {
                HitInfo h;
                h.damage = damage;
                h.from = pos;
                h.knockback = heavy ? 9.0f : 6.0f;
                h.move = "Puppet Strike";
                auto hits = combat::Sweep(GetScene(), Team::Enemy, pos + fwd * (heavy ? 1.6f : 1.3f) + vec3(0, 1.0f, 0), heavy ? 1.5f : 1.05f, h, m_HitSet);
                if (!hits.empty()) PlaySfx("Audio/sword_hit.wav", 0.6f);
            }
            if (m_Time >= 0.55f) Enter(State::Recover);
            break;
        }
        case State::Recover:
            if (m_Time >= (heavy ? 0.9f : 0.7f)) {
                combat::ReleaseToken(GetScene(), GetEntity().GetUUID());
                m_Cooldown = Rand(1.4f, 3.0f);
                Enter(State::Stalk);
            }
            break;
        case State::Stagger:
            steer = false;
            Damp(rb, dt, 5.0f);
            if (m_Time >= m_StaggerTime) Enter(State::Stalk);
            break;
        case State::Air:
            steer = false;
            Damp(rb, dt, 1.0f);
            rb.gravityScale = m_Float > 0.0f ? 0.18f : 1.0f;
            if (rb.grounded && m_Time > 0.15f && rb.velocity.y <= 0.5f) Enter(State::Down);
            break;
        case State::Down:
            steer = false;
            Damp(rb, dt, 6.0f);
            if (m_Time >= 1.1f) Enter(State::GetUp);
            break;
        case State::GetUp:
            if (m_Time >= 0.6f) Enter(State::Stalk);
            break;
        case State::Dead: break;
        }

        if (steer) {
            vec2 hv(rb.velocity.x, rb.velocity.z), target(wish.x, wish.z);
            vec2 d = target - hv;
            float maxDelta = 30.0f * dt;
            if (glm::length(d) > maxDelta) d = glm::normalize(d) * maxDelta;
            rb.velocity.x += d.x;
            rb.velocity.z += d.y;
        }
        if (m_State != State::Air) rb.gravityScale = 1.0f;
        if (auto* a = Anim()) {
            a->SetFloat("Speed", glm::length(vec2(rb.velocity.x, rb.velocity.z)));
            a->SetBool("Grounded", rb.grounded);
        }
        if (t.position.y < -15.0f) DestroySelf();
    }

    bool ReceiveHit(const HitInfo& hit) override {
        if (m_State == State::Dead || m_State == State::Spawn) return false;
        auto& rb = Get<RigidBodyComponent>();
        m_Health -= hit.damage;
        m_Flash = 1.0f;
        vec3 away = Flat(Transform().position - hit.from);
        if (glm::length(away) < 0.5f) away = -Flat(Transform().Forward());
        Face(-away, 100.0f, 1.0f);   // turn to face whoever hit us
        if (m_Health <= 0.0f) { Die(away); return true; }

        bool inAir = m_State == State::Air || !rb.grounded;
        if (hit.launch > 0.0f && !inAir) {
            DropToken();
            rb.velocity = vec3(away.x, 0, away.z) * 0.8f + vec3(0, hit.launch, 0);
            m_Float = 0.0f;
            Enter(State::Air);
            return true;
        }
        if (inAir) {
            DropToken();
            if (hit.spike > 0.0f) { rb.velocity = vec3(0, -hit.spike, 0); m_Float = 0.0f; }
            else {
                // Juggle: each air hit holds them up a little longer
                rb.velocity = away * hit.knockback * 0.3f + vec3(0, std::max(rb.velocity.y, hit.airHit ? 2.2f : 3.5f), 0);
                m_Float = hit.airHit ? 0.5f : 0.25f;
            }
            Enter(State::Air, true);
            return true;
        }
        if (heavy && !hit.knockdown) {
            // Super armour: light hits only chip the poise
            m_Poise -= hit.damage;
            if (m_Poise > 0.0f) return true;
            m_Poise = poiseMax;
        }
        DropToken();
        if (hit.knockdown) {
            rb.velocity = away * hit.knockback + vec3(0, 4.5f, 0);
            m_Float = 0.0f;
            Enter(State::Air);
            return true;
        }
        rb.velocity = away * hit.knockback + vec3(0, rb.velocity.y, 0);
        m_StaggerTime = hit.stagger * (heavy ? 0.7f : 1.0f);
        Enter(State::Stagger, true);
        return true;
    }

private:
    enum class State { Spawn, Stalk, Approach, Windup, Strike, Recover, Stagger, Air, Down, GetUp, Dead };

    AnimatorComponent* Anim() {
        Entity m = FindByUUID(modelEntity);
        return m ? m.TryGet<AnimatorComponent>() : nullptr;
    }

    Entity Player() {
        if (PlayerCombat* pc = PlayerCombat::Local()) if (!pc->IsDead()) return pc->GetEntity();
        auto players = GetScene().FindAllWithTag("Player");
        return players.empty() ? Entity() : players.front();
    }

    void Enter(State s, bool restart = false) {
        State from = m_State;
        m_State = s;
        m_Time = 0.0f;
        auto* a = Anim();
        if (!a) return;
        switch (s) {
        case State::Spawn: break;
        case State::Stalk:
        case State::Approach:
            if (from != State::Stalk && from != State::Approach) a->Play("Locomotion", 0.2f);
            break;
        case State::Windup:
            a->Play("Windup", 0.08f, true);
            PlaySfx("Audio/enemy_windup.wav", 0.5f);
            break;
        case State::Strike:
            m_HitSet.clear();
            a->Play("Strike", 0.04f, true);
            PlaySfx("Audio/sword_swing.wav", 0.5f);
            break;
        case State::Recover: break;
        case State::Stagger: a->Play("Hit", 0.03f, restart); break;
        case State::Air: a->Play("Launched", 0.05f, restart); break;
        case State::Down: a->Play("Knockdown", 0.08f); PlaySfx("Audio/body_fall.wav", 0.5f); break;
        case State::GetUp: a->Play("GetUp", 0.1f); break;
        case State::Dead: a->Play("Knockdown", 0.05f, true); break;
        }
    }

    void Die(const vec3& away) {
        DropToken();
        m_Health = 0.0f;
        auto& rb = Get<RigidBodyComponent>();
        rb.velocity = away * 6.0f + vec3(0, 4.0f, 0);
        rb.gravityScale = 1.0f;
        Enter(State::Dead);
        PlaySfx("Audio/enemy_die.wav", 0.7f);
        Emit("EnemyDefeated", { { "heavy", heavy } });
    }

    void DropToken() { combat::ReleaseToken(GetScene(), GetEntity().GetUUID()); }

    void Face(const vec3& dir, float rate, float dt) {
        if (glm::length(dir) < 0.5f) return;
        auto& t = Transform();
        float cur = glm::eulerAngles(t.rotation).y;
        (void)cur;
        quat want = glm::angleAxis(combat::YawOf(dir), vec3(0, 1, 0));
        t.rotation = glm::normalize(glm::slerp(t.rotation, want, std::min(1.0f, rate * dt)));
    }

    static void Damp(RigidBodyComponent& rb, float dt, float k) {
        rb.velocity.x *= 1.0f / (1.0f + k * dt);
        rb.velocity.z *= 1.0f / (1.0f + k * dt);
    }

    // Hit flash on the body, eyes that flare while winding up, rising out of the ground on spawn
    void UpdateLook() {
        Entity model = FindByUUID(modelEntity);
        if (!model) return;
        if (auto* mr = model.TryGet<MeshRendererComponent>()) mr->emissive = vec3(m_Flash * m_Flash * 1.2f);
        float rise = m_State == State::Spawn ? glm::clamp(m_Time / 0.7f, 0.0f, 1.0f) : 1.0f;
        model.Transform().position.y = -1.9f * (1.0f - rise * rise * (3.0f - 2.0f * rise));
        if (Entity eyes = FindByUUID(eyesEntity)) {
            float glow = m_State == State::Windup ? 6.0f + 4.0f * std::sin(m_Time * 40.0f) : (m_State == State::Dead ? 0.0f : 2.5f);
            for (Entity e : GetScene().GetChildren(eyes))
                if (auto* mr = e.TryGet<MeshRendererComponent>()) mr->emissive = vec3(1.0f, 0.12f, 0.05f) * glow;
        }
    }

    static void PlaySfx(const std::string& path, float volume) {
        SoundParams p;
        p.volume = volume;
        p.pitch = Rand(0.9f, 1.1f);
        Audio::Play(path, p);
    }

    State m_State = State::Spawn;
    float m_Time = 0.0f;
    float m_Health = 90.0f;
    float m_Poise = 40.0f;
    float m_Flash = 0.0f;
    float m_Float = 0.0f;
    float m_StaggerTime = 0.4f;
    float m_Cooldown = 1.0f;
    float m_Strafe = 1.0f;
    bool m_Burst = false;
    std::set<uint64_t> m_HitSet;
};
EU_SCRIPT(Puppet)
