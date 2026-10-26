#pragma once
#include <nd/9divine.h>

// Third-person character: camera-relative movement, sprint, jump, health and respawning.
class PlayerController : public nd::ScriptableEntity {
public:
    float moveSpeed = 4.5f;
    float sprintSpeed = 8.5f;
    float jumpSpeed = 8.0f;
    float acceleration = 14.0f;
    float maxHealth = 100.0f;
    float health = 100.0f;
    uint64_t cameraEntity = 0;
    uint64_t modelEntity = 0;      // child with the Animator

    void Reflect(nd::FieldVisitor& v) override;
    void OnCreate() override;
    void OnUpdate(float dt) override;

    void TakeDamage(float amount, const nd::vec3& from);
    void Heal(float amount) { health = std::min(maxHealth, health + amount); }
    bool IsDead() const { return m_Dead; }
    float HurtFlash() const { return m_HurtFlash; }

private:
    void Die();
    float m_Invulnerable = 0.0f;
    float m_HurtFlash = 0.0f;
    bool m_Dead = false;
    nd::vec3 m_LastPos{ 0.0f };
};
