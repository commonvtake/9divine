#pragma once
#include "nd/core/Base.h"
#include <string>
#include <vector>

namespace nd {

// ---------------------------------------------------------------- Skeleton data
struct Joint {
    std::string name;
    int parent = -1;                 // index into Skeleton::joints (parents come first)
    mat4 inverseBind{ 1.0f };
    vec3 bindT{ 0.0f };
    quat bindR{ 1, 0, 0, 0 };
    vec3 bindS{ 1.0f };
};

struct Skeleton {
    std::vector<Joint> joints;
    int rootJoint = 0;               // used for root motion
    int FindJoint(const std::string& name) const;
};

template <typename T>
struct Keyframes {
    std::vector<float> times;
    std::vector<T> values;
};

struct JointChannel {
    int joint = -1;
    Keyframes<vec3> translation;
    Keyframes<quat> rotation;
    Keyframes<vec3> scale;
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    std::vector<JointChannel> channels;
};

struct JointPose {
    vec3 t{ 0.0f };
    quat r{ 1, 0, 0, 0 };
    vec3 s{ 1.0f };
};
using Pose = std::vector<JointPose>;

void BindPose(const Skeleton& skeleton, Pose& out);
void SampleClip(const AnimationClip& clip, const Skeleton& skeleton, float time, Pose& out);
void BlendPoses(const Pose& a, const Pose& b, float t, Pose& out);
void ComputeSkinMatrices(const Skeleton& skeleton, const Pose& pose, std::vector<mat4>& globals, std::vector<mat4>& skin);

// ---------------------------------------------------------------- Controller description
enum class AnimParamType { Float, Bool, Trigger };
struct AnimParam {
    std::string name;
    AnimParamType type = AnimParamType::Float;
    float value = 0.0f;
};

struct AnimBlendEntry {
    std::string clip;
    float threshold = 0.0f;
};

// A state plays one clip, or (if `blend` is non-empty) a 1D blend space driven by `blendParam`.
struct AnimState {
    std::string name = "State";
    std::string clip;
    float speed = 1.0f;
    bool loop = true;
    std::string blendParam;
    std::vector<AnimBlendEntry> blend;
};

enum class AnimCondOp { Greater, Less, IsTrue, IsFalse, Trigger };
struct AnimCondition {
    std::string param;
    AnimCondOp op = AnimCondOp::Greater;
    float threshold = 0.0f;
};

struct AnimTransition {
    std::string from = "*";          // "*" = any state
    std::string to;
    std::vector<AnimCondition> conditions;
    float duration = 0.2f;           // cross-fade seconds
    float exitTime = -1.0f;          // normalized time that must be reached (-1 = none)
};

struct AnimatorComponent;
class Model;

// Per-entity playback state. Created when the animator first updates.
class AnimatorRuntime {
public:
    void Update(AnimatorComponent& anim, const Model& model, float dt);
    void Play(AnimatorComponent& anim, const std::string& state, float blend);

    std::vector<mat4> skinMatrices;
    std::vector<mat4> globalMatrices;
    vec3 rootMotionDelta{ 0.0f };     // model-space translation since last update
    std::string currentState;
    float stateTime = 0.0f;           // seconds in current state
    float NormalizedTime() const { return m_CurrentDuration > 0 ? stateTime / m_CurrentDuration : 0.0f; }

private:
    void EvaluateState(const AnimatorComponent& anim, const Model& model, const AnimState& state, float time, Pose& out, float* duration);
    const AnimState* FindState(const AnimatorComponent& anim, const std::string& name) const;

    std::string m_PrevState;
    float m_PrevTime = 0.0f;
    float m_Fade = 0.0f, m_FadeDuration = 0.0f;
    float m_CurrentDuration = 0.0f;
    Pose m_PoseA, m_PoseB, m_Final;
    vec3 m_LastRootPos{ 0.0f };
    bool m_HasLastRoot = false;
    bool m_Initialized = false;
};

} // namespace nd
