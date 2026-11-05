#pragma once
// Hack-and-slash combat shared by the player and enemies: hits, hit feedback, attack tokens
// and the style meter. Engine tools used here: Physics::OverlapSphere for hit volumes,
// Time::HitStop and CameraShake for impact, particles for sparks.
#include <euphoria/Euphoria.h>
#include <deque>
#include <set>

enum class Team { Player, Enemy };

// What one hit does to its target.
struct HitInfo {
    float damage = 10.0f;
    eu::vec3 from{ 0.0f };          // attacker position (knockback direction)
    eu::vec3 point{ 0.0f };         // where it landed (sparks)
    float knockback = 2.0f;         // horizontal push, m/s
    float launch = 0.0f;            // upward speed: a launcher sends grounded targets flying
    float spike = 0.0f;             // downward speed: slams an airborne target into the ground
    bool knockdown = false;         // finishers and slams put grounded targets on the floor
    bool airHit = false;            // keeps an airborne target hanging there (juggling)
    float stagger = 0.4f;           // seconds of hit reaction
    std::string move;               // attack name, for the style meter's variety bonus
};

// Anything that can be hit: the player and every enemy.
class Combatant : public eu::ScriptableEntity {
public:
    Team team = Team::Enemy;

    // Returns false if the hit was ignored (already dead, dodging...).
    virtual bool ReceiveHit(const HitInfo& hit) = 0;
    virtual bool IsDead() const = 0;
    virtual bool Airborne() { return false; }
    virtual float HealthFraction() { return 1.0f; }   // for lock-on health bars
    // Chest height: where lock-on aims and sparks fly.
    virtual eu::vec3 AimPoint() { return GetScene().WorldPosition(GetEntity()) + eu::vec3(0, 1.1f, 0); }
};

namespace combat {

// Every living combatant of `team` in the scene.
std::vector<Combatant*> All(eu::Scene& scene, Team team);

// Hits every living combatant of the opposite team inside the sphere, skipping ones already in
// `alreadyHit` (one swing hits each target once). Returns the targets hit this call.
std::vector<Combatant*> Sweep(eu::Scene& scene, Team attackerTeam, const eu::vec3& center, float radius, const HitInfo& hit,
                              std::set<uint64_t>& alreadyHit);

// A one-off burst of particles (sparks, blood-mist, dust) that cleans itself up.
void Burst(eu::Scene& scene, const eu::vec3& at, const eu::vec4& color, int count, float speed = 6.0f, float size = 0.12f, float life = 0.35f);

// Attack tokens: at most `max` enemies may attack the player at the same moment, so a crowd
// takes turns instead of piling on (the others circle and wait).
bool RequestToken(eu::Scene& scene, uint64_t enemy, int max = 2);
void ReleaseToken(eu::Scene& scene, uint64_t enemy);
bool HasToken(eu::Scene& scene, uint64_t enemy);

// Turns `current` toward `target` (both yaw in radians) by at most `maxStep`.
float TurnTowards(float current, float target, float maxStep);
float YawOf(const eu::vec3& dir);   // yaw whose forward (-Z) points along dir

// ---------------------------------------------------------------- style
// The style meter: varied attacks fill it, repeating the same move fills it less and less,
// taking a hit drops a whole rank, and it drains over time (faster at higher ranks).
class StyleMeter {
public:
    static constexpr int kRanks = 7;
    void Add(const std::string& move, float points);
    void Bonus(float points) { Gain(points); }   // dodges and other flair
    void Hurt();
    void Update(float dt);

    int Rank() const;                    // 0 = D ... 6 = SSS, -1 = no rank yet
    float RankProgress() const;          // 0..1 within the current rank
    float Points() const { return m_Points; }
    static const char* Letter(int rank);
    static const char* Word(int rank);
    static eu::vec4 Color(int rank);
    float RankPop() const { return m_Pop; }   // 1 right after a rank up, fades to 0
    float Peak() const { return m_Peak; }
    void Reset() { *this = StyleMeter(); }

private:
    void Gain(float p);
    float m_Points = 0.0f;
    float m_Hold = 0.0f;                 // no drain for a moment after gaining points
    float m_Pop = 0.0f;
    float m_Peak = 0.0f;
    std::deque<std::string> m_Recent;
};

} // namespace combat

// Keeps an entity (a sword, glowing eyes) stuck to a bone of its parent's animated model.
// The parent must be the entity with the Animator. Offset / Rotation are in the bone's space.
class AttachToBone : public eu::ScriptableEntity {
public:
    std::string bone = "Hand.R";
    eu::vec3 offset{ 0.0f };
    eu::vec3 rotation{ 0.0f };   // degrees

    void Reflect(eu::FieldVisitor& v) override;
    void OnLateUpdate(float dt) override;

private:
    int m_Joint = -2;            // -2 = not looked up yet, -1 = missing
    std::string m_JointName;
};
