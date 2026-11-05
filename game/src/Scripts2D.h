#pragma once
// Gameplay scripts for 2D games (scenes with Environment > 2D on). See docs/2D.md.
#include <euphoria/Euphoria.h>

// A 2D character for platformers (run, jump with coyote time, jump buffering and a shorter
// jump when the button is let go early) or top-down games (8-way movement).
//
// Needs a Dynamic RigidBody and a collider. Animations come from the SpriteAnimator by name:
// "idle", "run" (or "walk"), "jump", "fall" - with a direction suffix for top-down sheets
// ("walk_down", "walk_up", "walk_left", "walk_right"; a missing left/right is the other one
// mirrored). Names are matched without caring about case.
class PlayerController2D : public eu::ScriptableEntity {
public:
    bool topDown = false;
    float moveSpeed = 6.5f;             // units per second
    float acceleration = 60.0f;         // on the ground (top-down: always)
    float airAcceleration = 35.0f;
    float jumpHeight = 3.3f;            // units
    float coyoteTime = 0.1f;            // can still jump this long after running off a ledge
    float jumpBuffer = 0.12f;           // a jump pressed this long before landing still happens
    float fallGravity = 1.8f;           // falling is faster than rising (snappier jumps)
    float maxFallSpeed = 18.0f;
    float killY = -12.0f;               // below this height the player respawns (platformer)
    std::string idleAnimation = "idle", moveAnimation = "run", jumpAnimation = "jump", fallAnimation = "fall";
    bool artFacesLeft = false;          // side-view art drawn facing left
    std::string jumpSound = "Audio/jump.wav";
    std::string hurtSound = "Audio/hit.wav";

    void Reflect(eu::FieldVisitor& v) override;
    void OnCreate() override;
    void OnUpdate(float dt) override;
    void OnCollisionEnter(eu::Entity other) override;

    // Back to the start (or the last checkpoint), with a short blink.
    void Respawn();
    void SetCheckpoint(const eu::vec3& p) { m_Spawn = p; }
    eu::vec2 Facing() const { return m_Facing; }

private:
    void Animate(eu::RigidBodyComponent& rb, bool grounded, float dt);
    eu::vec3 m_Spawn{ 0.0f };
    eu::vec2 m_Facing{ 0.0f, -1.0f };
    float m_Coyote = 0.0f, m_Buffer = 0.0f, m_Blink = 0.0f;
    bool m_JumpHeld = false;
};

// Follows the player smoothly, looks ahead in the direction of travel and stops at the edges
// of the first tilemap in the scene. Put it on the camera.
class CameraFollow2D : public eu::ScriptableEntity {
public:
    uint64_t target = 0;                // empty = the entity tagged "Player"
    float smoothTime = 0.12f;           // seconds to catch up (0 = locked on)
    eu::vec3 offset{ 0.0f, 1.0f, 0.0f };
    float lookAhead = 1.5f;             // units ahead of a moving target (horizontal)
    bool clampToMap = true;

    void Reflect(eu::FieldVisitor& v) override;
    void OnStart() override;
    void OnLateUpdate(float dt) override;

private:
    eu::Entity Target();
    eu::vec3 Desired(float dt);
    void Clamp(eu::vec3& p);
    float m_Ahead = 0.0f;
};

// A pickup: the player touching its trigger collects it (emits "Collected" with its value).
// Bobs up and down. A Hud2D counts them.
class Collectible2D : public eu::ScriptableEntity {
public:
    int value = 1;
    std::string sound = "Audio/coin.wav";
    float bob = 0.12f;                  // units

    void Reflect(eu::FieldVisitor& v) override;
    void OnCreate() override;
    void OnUpdate(float dt) override;
    void OnTriggerEnter(eu::Entity other) override;

private:
    eu::vec3 m_Base{ 0.0f };
    float m_Time = 0.0f;
    bool m_Taken = false;
};

// An enemy walking back and forth, turning at walls and ledges. Touching it from the side
// hurts the player; landing on it from above squashes it.
class Patrol2D : public eu::ScriptableEntity {
public:
    float speed = 2.0f;
    bool turnAtLedges = true;
    std::string dieSound = "Audio/enemy_die.wav";

    void Reflect(eu::FieldVisitor& v) override;
    void OnCreate() override;
    void OnUpdate(float dt) override;
    void OnMessage(const std::string& message) override;

private:
    float m_Dir = -1.0f;
    float m_Dying = -1.0f;
};

// Title, collected count and controls hint for the 2D templates.
class Hud2D : public eu::ScriptableEntity {
public:
    std::string title = "My 2D Game";
    std::string itemName = "COINS";
    std::string icon;                   // image shown next to the count
    std::string controls = "A/D or arrows: move    Space: jump";

    void Reflect(eu::FieldVisitor& v) override;
    void OnStart() override;
    void OnUpdate(float dt) override { m_Time += dt; }
    void OnGUI() override;

private:
    int m_Total = 0, m_Collected = 0;
    float m_Time = 0.0f, m_DoneTime = -1.0f;
};
