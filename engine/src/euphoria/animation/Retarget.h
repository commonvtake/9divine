#pragma once
#include "euphoria/animation/Animation.h"

namespace eu {

// Plays animations made for one skeleton on another.
//
//  - Same rig (Quaternius' Universal Animation Library on Universal Base Characters, Mixamo
//    clips on a Mixamo character): joints are matched by name and the keys are copied.
//  - Different humanoid rigs (Mixamo "mixamorig:LeftUpLeg", Unreal "thigh_l", Blender
//    "thigh.L" / "DEF-thigh.L", "Thigh_L", ...): joints are matched through a common humanoid
//    skeleton (hips, spine, chest, neck, head, shoulders, arms, hands, legs, feet, toes) and each
//    bone's rotation *relative to its rest pose* is transferred in model space, so rigs with
//    different rest orientations, bone rolls and proportions still move the same way. Hip
//    height is scaled by the ratio of the two rigs' hip heights.
// Both rigs should rest in a similar pose (both T-pose or both A-pose) and face the same way.
enum class HumanBone {
    Hips, Spine, Chest, UpperChest, Neck, Head,
    LeftShoulder, LeftUpperArm, LeftLowerArm, LeftHand,
    RightShoulder, RightUpperArm, RightLowerArm, RightHand,
    LeftUpperLeg, LeftLowerLeg, LeftFoot, LeftToes,
    RightUpperLeg, RightLowerLeg, RightFoot, RightToes,
    Count, None = Count
};
const char* HumanBoneName(HumanBone b);

// "mixamorig:LeftForeArm" -> "leftforearm", "DEF-upper_arm.L" -> "upper_arm_l"
std::string NormalizeJointName(const std::string& name);

// Humanoid bone of every joint (None for fingers, twist bones, IK targets and the like).
std::vector<HumanBone> ClassifyHumanoid(const Skeleton& skeleton);

struct RetargetMap {
    std::vector<int> sourceOf;   // per target joint: the source joint that drives it, or -1
    bool sameRig = false;        // copy keys as they are
    int mapped = 0;              // how many target joints are driven
    int sourceHips = -1, targetHips = -1;
};
RetargetMap MapSkeletons(const Skeleton& source, const Skeleton& target);

// Model-space joint matrices in the pose the mesh was bound in (from the inverse bind matrices),
// which is the T- or A-pose rigs are made in, even when the file's scene pose is different.
void BindPoseGlobals(const Skeleton& skeleton, std::vector<mat4>& out);

// The clip re-expressed on `target`'s joints. Returns nullptr when nothing maps.
Ref<AnimationClip> RetargetClip(const AnimationClip& clip, const Skeleton& source, const Skeleton& target, const RetargetMap& map);

} // namespace eu
