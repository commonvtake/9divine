#pragma once
#include "Combat.h"

// The player's sword moveset, in the style of Devil May Cry:
//
//   Attack x4                  slash, slash, overhead chop, spinning finisher (knocks down)
//   Attack x2, pause, Attack   rapid stab flurry
//   Lock-on + back + Attack    launcher: knocks the enemy into the air; hold Attack to jump after it
//   Lock-on + fwd + Attack     lunge across the room into the target
//   Heavy                      a slow cleave that knocks down everything in front
//   Attack in the air          three-hit air string; you hang in the air while it lands
//   Heavy in the air           plunge straight down and slam the ground
//   Dodge (or lock-on + Jump + a direction)   a roll with invincibility frames
//
// Hits freeze the game for a few frames (hit-stop), shake the camera and throw sparks. A style
// meter ranks how well you're fighting: varied moves raise it, repeating one move barely does,
// getting hit drops it a rank.
class PlayerCombat : public Combatant {
public:
    uint64_t modelEntity = 0;
    uint64_t swordEntity = 0;      // has AttachToBone; moved between hand and back
    uint64_t trailEntity = 0;      // particle emitter at the blade tip
    float damageScale = 1.0f;
    // Attract mode: the player fights on its own, cycling through the whole moveset. For
    // demos, screenshots and benchmarking busy fights. Off in normal play.
    bool autoFight = false;

    static PlayerCombat* Local() { return s_Local; }

    void Reflect(eu::FieldVisitor& v) override;
    void OnCreate() override;
    void OnStart() override;
    void OnDestroy() override;
    void OnUpdate(float dt) override;
    void OnGUI() override;

    bool ReceiveHit(const HitInfo& hit) override;
    bool IsDead() const override;
    bool Airborne() override;

    // For PlayerController / cameras
    bool ControlsMovement() const { return m_Action != Action::None; }
    bool Invulnerable() const;
    bool WantsJump() const;        // false while lock-on + direction turns Jump into a dodge
    eu::Entity LockTarget();
    combat::StyleMeter& Style() { return m_Style; }
    int ComboHits() const { return m_Combo; }

    // Presses a combat button as if from the input map (for tests, replays, demos).
    enum class Command { Attack, Heavy, Dodge };
    void Press(Command c);
    // The move being performed ("Slash", "Launcher", "Dodge", ...), or "" when free.
    std::string CurrentAction() const;
    // Event Triggers / scripts: Send Message "Attack", "Heavy" or "Dodge".
    void OnMessage(const std::string& message) override;
    eu::vec3 AutoDirection() const { return m_AutoDir; }

    // Moves (public so tests and tools can look at the table)
    enum MoveId { Slash1, Slash2, Slash3, Finisher, Flurry, Stinger, Launcher, Cleave, Air1, Air2, Air3, MoveCount };
    struct Move {
        const char* name;          // style variety key and the HUD's name for it
        const char* state;         // animator state (added at startup)
        const char* clip;
        float speed;               // clip playback speed
        float length;              // seconds until control returns
        float cancel;              // from here a queued attack or dodge may cut in
        float hitStart, hitEnd;    // the blade is live between these
        float rehit;               // > 0: multi-hit, targets can be hit again this often
        float damage, reach, radius, knockback, launch, spike;
        bool knockdown, air;
        float lungeSpeed, lungeTime;
        float style, shake, hitStop;
    };
    static const Move& GetMove(MoveId id);

private:
    enum class Action { None, Attack, Dodge, Dive, Slam, Hurt };
    void StartMove(MoveId id);
    void StartDodge(const eu::vec3& dir);
    void StartDive();
    void EndAction();
    void UpdateAttack(float dt);
    void UpdateLockOn();
    void FaceTowards(const eu::vec3& dir, bool snap);
    void OnLanded(const std::vector<Combatant*>& hits, const Move& m);
    void SetSwordDrawn(bool drawn);
    eu::vec3 InputDirection();     // camera-relative stick direction on the ground plane (may be 0)
    eu::Entity Model() { return FindByUUID(modelEntity); }
    eu::AnimatorComponent* Anim();

    static PlayerCombat* s_Local;
    Action m_Action = Action::None;
    MoveId m_Move = Slash1;
    float m_Time = 0.0f;
    float m_RehitTimer = 0.0f;
    bool m_Landed = false;          // the current move has hit something
    std::set<uint64_t> m_HitSet;

    int m_Step = -1;                // position in the ground string (-1 = none)
    float m_SinceString = 99.0f;    // seconds since the last ground string move finished
    int m_AirStep = 0;
    bool m_AirStringUsed = false;
    enum class Buffered { None, Attack, Heavy, Dodge } m_Buffer = Buffered::None;
    float m_BufferAge = 0.0f;
    bool m_FollowLaunch = false;

    eu::vec3 m_DodgeDir{ 0.0f };
    bool m_JustDodged = false;
    float m_CloseCallTimer = 0.0f;
    uint64_t m_Lock = 0;
    float m_LockPulse = 0.0f;
    bool m_SwordDrawn = false;
    float m_DrawnTimer = 0.0f;

    combat::StyleMeter m_Style;
    int m_Combo = 0;
    float m_ComboTimer = 0.0f;
    void UpdateAutoFight(float dt);
    eu::vec3 m_AutoDir{ 0.0f };
    float m_AutoTimer = 1.0f;
    size_t m_AutoStep = 0, m_AutoRoutine = 0;
    std::string m_LastMoveName;
    float m_MoveNameTimer = 0.0f;
};
