#include "nd/animation/Animation.h"
#include "nd/scene/Components.h"
#include "nd/assets/Model.h"
#include <cmath>

namespace nd {

int Skeleton::FindJoint(const std::string& name) const {
    for (size_t i = 0; i < joints.size(); i++) if (joints[i].name == name) return (int)i;
    return -1;
}

void BindPose(const Skeleton& s, Pose& out) {
    out.resize(s.joints.size());
    for (size_t i = 0; i < s.joints.size(); i++) out[i] = { s.joints[i].bindT, s.joints[i].bindR, s.joints[i].bindS };
}

template <typename T, typename Lerp>
static T SampleKeys(const Keyframes<T>& k, float t, const T& fallback, Lerp lerp) {
    if (k.times.empty() || k.values.empty()) return fallback;
    size_t n = std::min(k.times.size(), k.values.size());
    if (t <= k.times[0]) return k.values[0];
    if (t >= k.times[n - 1]) return k.values[n - 1];
    size_t hi = std::upper_bound(k.times.begin(), k.times.begin() + n, t) - k.times.begin();
    size_t lo = hi - 1;
    float span = k.times[hi] - k.times[lo];
    float f = span > 0 ? (t - k.times[lo]) / span : 0.0f;
    return lerp(k.values[lo], k.values[hi], f);
}

void SampleClip(const AnimationClip& clip, const Skeleton& s, float time, Pose& out) {
    BindPose(s, out);
    for (const auto& ch : clip.channels) {
        if (ch.joint < 0 || ch.joint >= (int)out.size()) continue;
        JointPose& p = out[ch.joint];
        p.t = SampleKeys(ch.translation, time, p.t, [](const vec3& a, const vec3& b, float f) { return glm::mix(a, b, f); });
        p.r = SampleKeys(ch.rotation, time, p.r, [](const quat& a, const quat& b, float f) { return glm::normalize(glm::slerp(a, b, f)); });
        p.s = SampleKeys(ch.scale, time, p.s, [](const vec3& a, const vec3& b, float f) { return glm::mix(a, b, f); });
    }
}

void BlendPoses(const Pose& a, const Pose& b, float t, Pose& out) {
    size_t n = std::min(a.size(), b.size());
    out.resize(n);
    for (size_t i = 0; i < n; i++) {
        out[i].t = glm::mix(a[i].t, b[i].t, t);
        quat qb = b[i].r;
        if (glm::dot(a[i].r, qb) < 0.0f) qb = -qb;
        out[i].r = glm::normalize(glm::slerp(a[i].r, qb, t));
        out[i].s = glm::mix(a[i].s, b[i].s, t);
    }
}

void ComputeSkinMatrices(const Skeleton& s, const Pose& pose, std::vector<mat4>& globals, std::vector<mat4>& skin) {
    size_t n = s.joints.size();
    globals.resize(n);
    skin.resize(n);
    for (size_t i = 0; i < n; i++) {
        mat4 local = ComposeTRS(pose[i].t, pose[i].r, pose[i].s);
        int p = s.joints[i].parent;
        globals[i] = p >= 0 ? globals[p] * local : local;
        skin[i] = globals[i] * s.joints[i].inverseBind;
    }
}

// ------------------------------------------------------------ AnimatorRuntime
const AnimState* AnimatorRuntime::FindState(const AnimatorComponent& anim, const std::string& name) const {
    for (auto& s : anim.states) if (s.name == name) return &s;
    return nullptr;
}

void AnimatorRuntime::EvaluateState(const AnimatorComponent& anim, const Model& model, const AnimState& st, float time, Pose& out, float* duration) {
    const Skeleton& skel = *model.skeleton;
    auto sample = [&](const std::string& clipName, float t, Pose& p) -> float {
        const AnimationClip* clip = model.FindClip(clipName);
        if (!clip && !model.clips.empty() && clipName.empty()) clip = model.clips[0].get();
        if (!clip) { BindPose(skel, p); return 0.0f; }
        float d = clip->duration;
        float ct = d > 0 ? (st.loop ? std::fmod(t, d) : std::min(t, d)) : 0.0f;
        SampleClip(*clip, skel, ct, p);
        return d;
    };
    if (st.blend.empty()) {
        float d = sample(st.clip, time, out);
        if (duration) *duration = d;
        return;
    }
    // 1D blend space: find the two entries around the parameter value.
    float v = 0.0f;
    for (auto& p : anim.params) if (p.name == st.blendParam) v = p.value;
    std::vector<AnimBlendEntry> entries = st.blend;
    std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) { return a.threshold < b.threshold; });
    size_t hi = 0;
    while (hi < entries.size() && entries[hi].threshold < v) hi++;
    if (hi == 0 || hi == entries.size()) {
        const auto& e = entries[hi == 0 ? 0 : entries.size() - 1];
        float d = sample(e.clip, time, out);
        if (duration) *duration = d;
        return;
    }
    const auto& a = entries[hi - 1];
    const auto& b = entries[hi];
    float f = (v - a.threshold) / std::max(1e-5f, b.threshold - a.threshold);
    // Normalized-time sync so feet stay in phase between walk and run.
    const AnimationClip* ca = model.FindClip(a.clip);
    const AnimationClip* cb = model.FindClip(b.clip);
    float da = ca ? ca->duration : 1.0f, db = cb ? cb->duration : 1.0f;
    float blendedDur = glm::mix(da, db, f);
    float phase = blendedDur > 0 ? std::fmod(time, blendedDur) / blendedDur : 0.0f;
    Pose pa, pb;
    sample(a.clip, phase * da, pa);
    sample(b.clip, phase * db, pb);
    BlendPoses(pa, pb, f, out);
    if (duration) *duration = blendedDur;
}

void AnimatorRuntime::Play(AnimatorComponent& anim, const std::string& state, float blend) {
    if (!FindState(anim, state)) return;
    if (state == currentState) return;
    m_PrevState = currentState;
    m_PrevTime = stateTime;
    currentState = state;
    stateTime = 0.0f;
    m_FadeDuration = blend;
    m_Fade = blend > 0 ? 0.0f : 1.0f;
}

void AnimatorRuntime::Update(AnimatorComponent& anim, const Model& model, float dt) {
    if (!model.IsSkinned()) return;
    if (!m_Initialized) {
        m_Initialized = true;
        currentState = anim.defaultState;
        if (!FindState(anim, currentState) && !anim.states.empty()) currentState = anim.states[0].name;
        m_Fade = 1.0f;
    }
    dt *= anim.speed;

    // Transitions
    for (auto& tr : anim.transitions) {
        if (tr.from != "*" && tr.from != currentState) continue;
        if (tr.to == currentState) continue;
        if (tr.exitTime >= 0.0f && NormalizedTime() < tr.exitTime) continue;
        bool ok = true;
        for (auto& c : tr.conditions) {
            AnimParam* p = anim.FindParam(c.param);
            float v = p ? p->value : 0.0f;
            switch (c.op) {
            case AnimCondOp::Greater: ok &= v > c.threshold; break;
            case AnimCondOp::Less: ok &= v < c.threshold; break;
            case AnimCondOp::IsTrue: case AnimCondOp::Trigger: ok &= v > 0.5f; break;
            case AnimCondOp::IsFalse: ok &= v <= 0.5f; break;
            }
        }
        if (tr.conditions.empty() && tr.exitTime < 0.0f) ok = false;   // would fire forever
        if (!ok) continue;
        for (auto& c : tr.conditions)
            if (c.op == AnimCondOp::Trigger) if (AnimParam* p = anim.FindParam(c.param)) p->value = 0.0f;
        Play(anim, tr.to, tr.duration);
        break;
    }

    const AnimState* cur = FindState(anim, currentState);
    AnimState fallback;
    if (!cur) { fallback.name = currentState; cur = &fallback; }

    stateTime += dt * cur->speed;
    EvaluateState(anim, model, *cur, stateTime, m_PoseA, &m_CurrentDuration);

    if (m_Fade < 1.0f && !m_PrevState.empty()) {
        m_Fade = m_FadeDuration > 0 ? std::min(1.0f, m_Fade + dt / m_FadeDuration) : 1.0f;
        const AnimState* prev = FindState(anim, m_PrevState);
        if (prev) {
            m_PrevTime += dt * prev->speed;
            EvaluateState(anim, model, *prev, m_PrevTime, m_PoseB, nullptr);
            BlendPoses(m_PoseB, m_PoseA, m_Fade, m_Final);
        } else m_Final = m_PoseA;
    } else {
        m_Final = m_PoseA;
    }

    // Root motion: take the root joint's horizontal movement out of the pose
    // and hand it to the entity instead.
    rootMotionDelta = vec3(0.0f);
    const Skeleton& skel = *model.skeleton;
    if (anim.rootMotion && skel.rootJoint < (int)m_Final.size()) {
        vec3 rootPos = m_Final[skel.rootJoint].t;
        if (m_HasLastRoot) {
            vec3 d = rootPos - m_LastRootPos;
            // Clip looped (big jump backwards): ignore that frame
            if (glm::length(vec2(d.x, d.z)) < 1.0f * std::max(1.0f, glm::length(skel.joints[skel.rootJoint].bindS))) rootMotionDelta = vec3(d.x, 0.0f, d.z);
        }
        m_LastRootPos = rootPos;
        m_HasLastRoot = true;
        m_Final[skel.rootJoint].t.x = skel.joints[skel.rootJoint].bindT.x;
        m_Final[skel.rootJoint].t.z = skel.joints[skel.rootJoint].bindT.z;
        // Express in model space (account for the root joint's parents)
        int p = skel.joints[skel.rootJoint].parent;
        if (p >= 0 && !globalMatrices.empty()) rootMotionDelta = vec3(globalMatrices[p] * vec4(rootMotionDelta, 0.0f));
    }

    ComputeSkinMatrices(skel, m_Final, globalMatrices, skinMatrices);
}

} // namespace nd
