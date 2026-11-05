// AI examples: a state-machine enemy and a behaviour-tree wildlife NPC.
#include <euphoria/Euphoria.h>
#include "GameManager.h"
#include "PlayerController.h"
#include "PlayerCombat.h"

using namespace eu;

// Shade: patrols, chases the player when it sees them, attacks up close. The sword can cut
// it down: it flinches back when hit and bursts apart when its health runs out.
// Demonstrates StateMachine + NavAgent pathfinding.
class EnemyAI : public Combatant {
public:
    uint64_t player = 0;
    float sightRange = 14.0f;
    float attackRange = 1.7f;
    float damage = 15.0f;
    float patrolRadius = 12.0f;
    bool debugDraw = false;
    float maxHealth = 40.0f;

    void Reflect(FieldVisitor& v) override {
        v.EntityRef("Player", player);
        v.Float("Sight Range", sightRange);
        v.Float("Attack Range", attackRange);
        v.Float("Damage", damage);
        v.Float("Patrol Radius", patrolRadius);
        v.Bool("Debug Draw", debugDraw);
        v.Float("Max Health", maxHealth);
    }

    bool IsDead() const override { return m_Health <= 0.0f; }
    float HealthFraction() override { return m_Health / maxHealth; }
    vec3 AimPoint() override { return Transform().position; }
    bool ReceiveHit(const HitInfo& hit) override {
        if (IsDead()) return false;
        m_Health -= hit.damage;
        m_Flash = 1.0f;
        vec3 away = Transform().position - hit.from;
        away.y = 0.0f;
        if (glm::length(away) > 1e-3f) m_Knock = glm::normalize(away) * (2.0f + hit.knockback);
        m_FSM.Change("Chase");
        if (m_Health <= 0.0f) {
            combat::Burst(GetScene(), Transform().position, { 0.35f, 0.1f, 0.6f, 1.0f }, 40, 5.0f, 0.3f, 0.7f);
            Audio::PlayOneShot("Audio/enemy_die.wav", 0.6f);
            Emit("EnemyDefeated");
            DestroySelf();
        }
        return true;
    }

    void OnCreate() override {
        team = Team::Enemy;
        m_Health = maxHealth;
        m_Home = Transform().position;
        m_Seed = (uint32_t)(GetEntity().GetUUID() & 0xFFFFFFFF);
        auto& agent = Get<NavAgentComponent>();

        m_FSM.Add("Patrol", {
            [&] { Get<NavAgentComponent>().speed = 2.2f; PickPatrolPoint(); },
            [&](float) {
                if (CanSeePlayer()) { m_FSM.Change("Chase"); return; }
                auto& a = Get<NavAgentComponent>();
                if (a.Arrived() && m_FSM.TimeInState() > 1.0f) { m_Wait -= Time::Delta(); if (m_Wait <= 0) PickPatrolPoint(); }
            },
            nullptr });
        m_FSM.Add("Chase", {
            [&] { Get<NavAgentComponent>().speed = 4.6f; },
            [&](float) {
                Entity p = FindByUUID(player);
                if (!p || Distance(p) > sightRange * 1.5f) { m_FSM.Change("Patrol"); return; }
                if (Distance(p) < attackRange) { m_FSM.Change("Attack"); return; }
                Get<NavAgentComponent>().SetDestination(p.Transform().position);
            },
            nullptr });
        m_FSM.Add("Attack", {
            [&] { Get<NavAgentComponent>().Stop(); m_AttackTimer = 0.35f; },
            [&](float dt) {
                Entity p = FindByUUID(player);
                if (!p) { m_FSM.Change("Patrol"); return; }
                if (Distance(p) > attackRange * 1.4f) { m_FSM.Change("Chase"); return; }
                m_AttackTimer -= dt;
                if (m_AttackTimer <= 0.0f) {
                    m_AttackTimer = 1.0f;
                    if (auto* pcom = GetScriptOn<PlayerCombat>(p)) {
                        HitInfo h;
                        h.damage = damage;
                        h.from = Transform().position;
                        h.move = "Shade";
                        pcom->ReceiveHit(h);
                    } else if (auto* pc = GetScriptOn<PlayerController>(p)) pc->TakeDamage(damage, Transform().position);
                }
            },
            nullptr });
        (void)agent;
        m_FSM.Change("Patrol");
    }

    void OnUpdate(float dt) override {
        // Knocked back by a hit: slide away, then carry on
        if (glm::length(m_Knock) > 0.05f) {
            Transform().position += m_Knock * dt;
            m_Knock *= 1.0f / (1.0f + 6.0f * dt);
        }
        m_Flash = std::max(0.0f, m_Flash - dt * 6.0f);
        if (auto* mr = TryGet<MeshRendererComponent>()) mr->emissive = vec3(0.08f, 0.0f, 0.12f) + vec3(m_Flash * 1.5f);
        m_FSM.Update(dt);
        // Pulse while chasing
        m_Pulse += dt * (m_FSM.Current() == "Patrol" ? 2.0f : 8.0f);
        float s = 1.0f + std::sin(m_Pulse) * 0.06f;
        Transform().scale = vec3(s);
        if (debugDraw) {
            DebugDraw::Circle(Transform().position + vec3(0, 0.1f, 0), sightRange, { 0, 1, 0 }, { 1, 0.3f, 0.3f, 1 });
            DebugDraw::Path(Get<NavAgentComponent>().path, { 1, 0.4f, 0.2f, 1 });
        }
    }

    void OnGUI() override {
        if (m_FSM.Current() == "Patrol") return;
        UI::WorldText(Transform().position + vec3(0, 1.6f, 0), m_FSM.Current() == "Attack" ? "!!" : "!", 34, { 1, 0.35f, 0.3f, 1 });
    }

private:
    float Distance(Entity e) { return glm::distance(Transform().position, e.Transform().position); }
    bool CanSeePlayer() {
        Entity p = FindByUUID(player);
        if (!p || Distance(p) > sightRange) return false;
        // Line of sight check
        vec3 eye = Transform().position + vec3(0, 0.6f, 0);
        vec3 to = p.Transform().position + vec3(0, 0.6f, 0) - eye;
        RaycastHit hit;
        if (GetScene().Physics().Raycast({ eye, to }, glm::length(to), hit, GetEntity().Id()))
            return hit.entity == p;
        return true;
    }
    void PickPatrolPoint() {
        m_Seed = m_Seed * 1664525u + 1013904223u;
        float a = (m_Seed >> 8) / 16777216.0f * 2.0f * PI;
        m_Seed = m_Seed * 1664525u + 1013904223u;
        float r = (m_Seed >> 8) / 16777216.0f * patrolRadius;
        Get<NavAgentComponent>().SetDestination(m_Home + vec3(std::cos(a) * r, 0, std::sin(a) * r));
        m_Wait = 1.5f;
    }

    StateMachine m_FSM;
    vec3 m_Home{ 0.0f };
    uint32_t m_Seed = 1;
    float m_Wait = 0.0f, m_AttackTimer = 0.0f, m_Pulse = 0.0f;
    float m_Health = 40.0f, m_Flash = 0.0f;
    vec3 m_Knock{ 0.0f };
};
EU_SCRIPT(EnemyAI)

// Fox: wanders between random spots, sits and looks around, and runs away from a
// sprinting player. Demonstrates behaviour trees + animation parameters.
class FoxWanderer : public ScriptableEntity {
public:
    uint64_t player = 0;
    uint64_t model = 0;
    float wanderRadius = 18.0f;
    float fleeDistance = 7.0f;

    void Reflect(FieldVisitor& v) override {
        v.EntityRef("Player", player);
        v.EntityRef("Model", model);
        v.Float("Wander Radius", wanderRadius);
        v.Float("Flee Distance", fleeDistance);
    }

    void OnCreate() override {
        m_Home = Transform().position;
        m_Seed = (uint32_t)(GetEntity().GetUUID() >> 16);
        auto playerClose = [this] {
            Entity p = FindByUUID(player);
            return p && glm::distance(p.Transform().position, Transform().position) < fleeDistance;
        };
        auto flee = [this](float) {
            Entity p = FindByUUID(player);
            if (!p) return BTStatus::Failure;
            vec3 away = Transform().position - p.Transform().position;
            away.y = 0;
            if (glm::length(away) < 1e-3f) away = vec3(1, 0, 0);
            auto& a = Get<NavAgentComponent>();
            a.speed = 6.5f;
            a.SetDestination(Transform().position + glm::normalize(away) * 8.0f);
            return BTStatus::Running;
        };
        auto pickSpot = [this](float) {
            m_Seed = m_Seed * 1664525u + 1013904223u;
            float ang = (m_Seed >> 8) / 16777216.0f * 2.0f * PI;
            m_Seed = m_Seed * 1664525u + 1013904223u;
            float r = 3.0f + (m_Seed >> 8) / 16777216.0f * wanderRadius;
            auto& a = Get<NavAgentComponent>();
            a.speed = 2.0f;
            a.SetDestination(m_Home + vec3(std::cos(ang) * r, 0, std::sin(ang) * r));
            return BTStatus::Success;
        };
        auto walkThere = [this](float) {
            return Get<NavAgentComponent>().Arrived() ? BTStatus::Success : BTStatus::Running;
        };
        m_Tree = BT::Selector({
            BT::Sequence({ BT::Condition(playerClose), BT::Action(flee) }),
            BT::Sequence({ BT::Action(pickSpot), BT::Action(walkThere), BT::Wait(3.0f) }),
        });
    }

    void OnUpdate(float dt) override {
        m_Tree->Tick(dt);
        if (Entity m = FindByUUID(model))
            if (auto* anim = m.TryGet<AnimatorComponent>()) anim->SetFloat("Speed", Get<NavAgentComponent>().currentSpeed);
    }

private:
    BTNodeRef m_Tree;
    vec3 m_Home{ 0.0f };
    uint32_t m_Seed = 7;
};
EU_SCRIPT(FoxWanderer)
