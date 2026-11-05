#include "Combat.h"
#include <euphoria/assets/Model.h>
#include <algorithm>

using namespace eu;

namespace combat {

std::vector<Combatant*> All(Scene& scene, Team team) {
    std::vector<Combatant*> out;
    scene.Reg().View<ScriptComponent>([&](EntityId id, ScriptComponent& sc) {
        Entity e(id, &scene);
        if (!e.IsActive()) return;
        for (auto& s : sc.scripts)
            if (auto* c = dynamic_cast<Combatant*>(s.instance.get()))
                if (c->team == team && !c->IsDead()) out.push_back(c);
    });
    return out;
}

std::vector<Combatant*> Sweep(Scene& scene, Team attackerTeam, const vec3& center, float radius, const HitInfo& hit,
                              std::set<uint64_t>& alreadyHit) {
    std::vector<Combatant*> hits;
    for (Entity e : scene.Physics().OverlapSphere(center, radius, false)) {
        auto* c = ScriptableEntity::GetScriptOn<Combatant>(e);
        if (!c || c->team == attackerTeam || c->IsDead()) continue;
        uint64_t id = e.GetUUID();
        if (alreadyHit.count(id)) continue;
        alreadyHit.insert(id);
        HitInfo h = hit;
        // Sparks fly where the blade meets the body: the target's surface facing the attacker
        vec3 aim = c->AimPoint();
        vec3 toward = hit.from - aim;
        toward.y = 0.0f;
        h.point = aim + (glm::length(toward) > 1e-3f ? glm::normalize(toward) * 0.35f : vec3(0.0f));
        if (c->ReceiveHit(h)) hits.push_back(c);
    }
    return hits;
}

void Burst(Scene& scene, const vec3& at, const vec4& color, int count, float speed, float size, float life) {
    Entity fx = scene.CreateEntity("Hit FX");
    fx.Transform().position = at;
    auto& pe = fx.Add<ParticleEmitterComponent>();
    pe.loop = false;
    pe.rate = 0.0f;
    pe.burst = count;
    pe.lifetime = life;
    pe.lifetimeVariance = life * 0.4f;
    pe.shapeSize = vec3(0.1f);
    pe.velocity = vec3(0.0f);
    pe.spread = speed;
    pe.drag = 3.0f;
    pe.gravity = { 0, -4.0f, 0 };
    pe.startColor = color;
    pe.endColor = vec4(vec3(color) * 0.6f, 0.0f);
    pe.startSize = size;
    pe.endSize = size * 0.2f;
    pe.additive = true;
    scene.Events().Invoke(life * 2.0f + 0.2f, [&scene, id = fx.GetUUID()] {
        if (Entity e = scene.FindByUUID(id)) scene.DestroyEntity(e);
    });
}

// Tokens live per scene; a new scene starts with none handed out.
namespace {
struct Tokens { Scene* scene = nullptr; std::set<uint64_t> holders; };
Tokens& TokensFor(Scene& scene) {
    static Tokens t;
    if (t.scene != &scene) { t.scene = &scene; t.holders.clear(); }
    return t;
}
} // namespace

bool RequestToken(Scene& scene, uint64_t enemy, int max) {
    auto& t = TokensFor(scene);
    // Drop holders that no longer exist (destroyed mid-attack)
    for (auto it = t.holders.begin(); it != t.holders.end();) it = scene.FindByUUID(*it) ? std::next(it) : t.holders.erase(it);
    if (t.holders.count(enemy)) return true;
    if ((int)t.holders.size() >= max) return false;
    t.holders.insert(enemy);
    return true;
}
void ReleaseToken(Scene& scene, uint64_t enemy) { TokensFor(scene).holders.erase(enemy); }
bool HasToken(Scene& scene, uint64_t enemy) { return TokensFor(scene).holders.count(enemy) > 0; }

float TurnTowards(float current, float target, float maxStep) {
    float d = std::remainder(target - current, 2.0f * PI);
    return current + glm::clamp(d, -maxStep, maxStep);
}

float YawOf(const vec3& dir) { return std::atan2(-dir.x, -dir.z); }

// ---------------------------------------------------------------- style
static const float kThreshold[StyleMeter::kRanks + 1] = { 30, 100, 190, 300, 430, 580, 750, 950 };

int StyleMeter::Rank() const {
    for (int r = kRanks - 1; r >= 0; r--) if (m_Points >= kThreshold[r]) return r;
    return -1;
}
float StyleMeter::RankProgress() const {
    int r = std::max(Rank(), 0);
    return glm::clamp((m_Points - kThreshold[r]) / (kThreshold[r + 1] - kThreshold[r]), 0.0f, 1.0f);
}
const char* StyleMeter::Letter(int r) {
    static const char* l[] = { "D", "C", "B", "A", "S", "SS", "SSS" };
    return r >= 0 && r < kRanks ? l[r] : "";
}
const char* StyleMeter::Word(int r) {
    static const char* w[] = { "Decent", "Cool", "Bravura", "Acrobatic", "Savage!", "SSuperb!!", "SSSensational!!!" };
    return r >= 0 && r < kRanks ? w[r] : "";
}
vec4 StyleMeter::Color(int r) {
    static const vec4 c[] = { { 0.75f, 0.75f, 0.8f, 1 }, { 0.45f, 0.8f, 1.0f, 1 }, { 0.5f, 1.0f, 0.55f, 1 }, { 1.0f, 0.85f, 0.3f, 1 },
                              { 1.0f, 0.55f, 0.2f, 1 }, { 1.0f, 0.3f, 0.3f, 1 }, { 1.0f, 0.25f, 0.85f, 1 } };
    return r >= 0 && r < kRanks ? c[r] : vec4(1.0f);
}

void StyleMeter::Gain(float p) {
    int before = Rank();
    m_Points = std::min(m_Points + p, kThreshold[kRanks] - 1.0f);
    m_Hold = 1.2f;
    if (Rank() > before) m_Pop = 1.0f;
    m_Peak = std::max(m_Peak, (float)Rank());
}

void StyleMeter::Add(const std::string& move, float points) {
    // Variety: each recent use of the same move cuts what it's worth
    int repeats = (int)std::count(m_Recent.begin(), m_Recent.end(), move);
    static const float factor[] = { 1.0f, 0.6f, 0.35f, 0.2f, 0.1f };
    Gain(points * factor[std::min(repeats, 4)]);
    m_Recent.push_back(move);
    if (m_Recent.size() > 5) m_Recent.pop_front();
}

void StyleMeter::Hurt() {
    int r = Rank();
    m_Points = r <= 0 ? 0.0f : kThreshold[r - 1];   // down a whole rank, to its very bottom
    m_Recent.clear();
}

void StyleMeter::Update(float dt) {
    m_Pop = std::max(0.0f, m_Pop - dt * 2.5f);
    if (m_Hold > 0.0f) { m_Hold -= dt; return; }
    float drain = 10.0f + 8.0f * std::max(Rank(), 0);
    m_Points = std::max(0.0f, m_Points - drain * dt);
}

} // namespace combat

// ---------------------------------------------------------------- AttachToBone
void AttachToBone::Reflect(FieldVisitor& v) {
    v.String("Bone", bone);
    v.Vec3("Offset", offset);
    v.Vec3("Rotation", rotation);
}

void AttachToBone::OnLateUpdate(float) {
    Entity parent = GetScene().GetParent(GetEntity());
    if (!parent) return;
    auto* anim = parent.TryGet<AnimatorComponent>();
    auto* mr = parent.TryGet<MeshRendererComponent>();
    if (!anim || !anim->runtime || !mr || !mr->model || !mr->model->skeleton) return;
    if (m_JointName != bone) { m_JointName = bone; m_Joint = -2; }
    if (m_Joint == -2) m_Joint = mr->model->skeleton->FindJoint(bone);
    const auto& globals = anim->runtime->globalMatrices;
    if (m_Joint < 0 || m_Joint >= (int)globals.size()) return;
    mat4 local = globals[m_Joint] * glm::translate(mat4(1.0f), offset) * glm::mat4_cast(quat(glm::radians(rotation)));
    auto& t = Transform();
    t.position = vec3(local[3]);
    t.rotation = glm::normalize(glm::quat_cast(mat3(local)));
}

EU_SCRIPT(AttachToBone)
