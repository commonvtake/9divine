#include "euphoria/animation/Retarget.h"
#include <algorithm>
#include <glm/gtx/quaternion.hpp>
#include <cctype>

namespace eu {

namespace {

bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

int Depth(const Skeleton& s, int j) {
    int d = 0;
    for (int p = s.joints[j].parent; p >= 0; p = s.joints[p].parent) d++;
    return d;
}

quat RotationOf(const mat4& m) {
    vec3 x = glm::normalize(vec3(m[0])), y = glm::normalize(vec3(m[1])), z = glm::normalize(vec3(m[2]));
    return glm::normalize(glm::quat_cast(mat3(x, y, z)));
}

void Globals(const Skeleton& s, const Pose& pose, std::vector<mat4>& g) {
    g.resize(s.joints.size());
    for (size_t i = 0; i < s.joints.size(); i++) {
        mat4 local = ComposeTRS(pose[i].t, pose[i].r, pose[i].s);
        int p = s.joints[i].parent;
        g[i] = p >= 0 ? g[p] * local : local;
    }
}

// Which side a normalized name is on: -1 left, +1 right, 0 none. Returns the name without it.
int SideOf(const std::string& n, std::string& core) {
    core = n;
    auto strip = [&](const std::string& token, size_t at) { core.erase(at, token.size()); };
    for (int side : { -1, 1 }) {
        const char* word = side < 0 ? "left" : "right";
        size_t w = core.find(word);
        if (w != std::string::npos) { strip(word, w); return side; }
        std::string suffix = side < 0 ? "_l" : "_r", prefix = side < 0 ? "l_" : "r_", mid = side < 0 ? "_l_" : "_r_";
        if (core.size() > 2 && core.compare(core.size() - 2, 2, suffix) == 0) { core.erase(core.size() - 2); return side; }
        if (core.rfind(prefix, 0) == 0) { core.erase(0, 2); return side; }
        size_t m = core.find(mid);
        if (m != std::string::npos) { core.erase(m, 2); return side; }
    }
    return 0;
}

// Left-to-right vector of a humanoid (shoulders and hips), flattened onto the ground.
vec3 Across(const Skeleton& s, const std::vector<mat4>& g) {
    auto human = ClassifyHumanoid(s);
    vec3 left(0.0f), right(0.0f);
    for (size_t i = 0; i < human.size(); i++) {
        HumanBone b = human[i];
        if (b == HumanBone::LeftUpperArm || b == HumanBone::LeftUpperLeg) left += vec3(g[i][3]);
        if (b == HumanBone::RightUpperArm || b == HumanBone::RightUpperLeg) right += vec3(g[i][3]);
    }
    vec3 d = left - right;
    d.y = 0.0f;
    return glm::length(d) > 1e-5f ? glm::normalize(d) : vec3(0.0f);
}

void FaceLikeScenePose(const Skeleton& s, std::vector<mat4>& bind, int hips) {
    Pose scene;
    BindPose(s, scene);
    std::vector<mat4> g;
    Globals(s, scene, g);
    vec3 a = Across(s, bind), b = Across(s, g);
    if (a == vec3(0.0f) || b == vec3(0.0f) || glm::dot(a, b) > 0.9999f) return;
    vec3 pivot = hips >= 0 ? vec3(bind[hips][3].x, 0.0f, bind[hips][3].z) : vec3(0.0f);
    mat4 turn = glm::translate(mat4(1.0f), pivot) * glm::mat4_cast(glm::rotation(a, b)) * glm::translate(mat4(1.0f), -pivot);
    for (auto& m : bind) m = turn * m;
}

} // namespace

void BindPoseGlobals(const Skeleton& s, std::vector<mat4>& g) {
    g.resize(s.joints.size());
    for (size_t i = 0; i < s.joints.size(); i++) {
        const Joint& j = s.joints[i];
        if (j.skinned) { g[i] = glm::inverse(j.inverseBind); continue; }
        mat4 local = ComposeTRS(j.bindT, j.bindR, j.bindS);
        g[i] = j.parent >= 0 ? g[j.parent] * local : local;
    }
}

const char* HumanBoneName(HumanBone b) {
    static const char* names[] = { "Hips", "Spine", "Chest", "UpperChest", "Neck", "Head",
                                   "LeftShoulder", "LeftUpperArm", "LeftLowerArm", "LeftHand",
                                   "RightShoulder", "RightUpperArm", "RightLowerArm", "RightHand",
                                   "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "LeftToes",
                                   "RightUpperLeg", "RightLowerLeg", "RightFoot", "RightToes" };
    return b < HumanBone::Count ? names[(int)b] : "None";
}

std::string NormalizeJointName(const std::string& name) {
    std::string n = name;
    size_t cut = n.find_last_of(":|");
    if (cut != std::string::npos) n = n.substr(cut + 1);
    std::string out;
    for (char c : n) {
        char l = (char)std::tolower((unsigned char)c);
        if (l == '.' || l == ' ' || l == '-') l = '_';
        out += l;
    }
    for (const char* prefix : { "mixamorig_", "mixamorig", "def_", "mch_", "org_", "bip001_", "bip01_", "b_", "bone_", "jnt_", "j_" })
        if (out.rfind(prefix, 0) == 0 && out.size() > std::string(prefix).size() + 1) { out = out.substr(std::string(prefix).size()); break; }
    std::string squeezed;
    for (char c : out) if (!(c == '_' && (squeezed.empty() || squeezed.back() == '_'))) squeezed += c;
    while (!squeezed.empty() && squeezed.back() == '_') squeezed.pop_back();
    return squeezed;
}

std::vector<HumanBone> ClassifyHumanoid(const Skeleton& s) {
    size_t n = s.joints.size();
    std::vector<HumanBone> raw(n, HumanBone::None);
    std::vector<int> spineFamily;
    for (size_t i = 0; i < n; i++) {
        std::string core;
        int side = SideOf(NormalizeJointName(s.joints[i].name), core);
        std::string c;
        for (char ch : core) if (ch != '_') c += ch;
        // Not part of the humanoid body: fingers, twist / IK / helper bones, face, hair, props
        static const char* skipAnywhere[] = { "twist", "roll", "pole", "target", "leaf", "socket", "weapon", "attach", "helper", "ctrl",
                                              "control", "corrective", "jiggle", "tail", "hair", "tongue", "teeth", "brow", "cheek",
                                              "nose", "thumb", "index", "middle", "ring", "pinky", "finger", "little", "metacarpal",
                                              "breast", "belly", "cloth", "cape", "headtop" };
        static const char* skipWords[] = { "ik", "end", "nub", "tip", "eye", "jaw", "lip", "ear", "prop" };   // whole words only ("forearm")
        bool skipped = false;
        for (const char* k : skipAnywhere) if (Has(c, k)) { skipped = true; break; }
        if (!skipped) {
            std::vector<std::string> words;
            std::string w;
            for (char ch : core + "_") {
                if (ch == '_' || std::isdigit((unsigned char)ch)) { if (!w.empty()) words.push_back(w); w.clear(); }
                else w += ch;
            }
            for (auto& word : words)
                for (const char* k : skipWords) if (word == k) skipped = true;
        }
        if (skipped) continue;
        auto sided = [&](HumanBone left, HumanBone right) { if (side != 0) raw[i] = side < 0 ? left : right; };
        if (Has(c, "hand") || Has(c, "wrist")) sided(HumanBone::LeftHand, HumanBone::RightHand);
        else if (Has(c, "forearm") || Has(c, "lowerarm") || Has(c, "elbow")) sided(HumanBone::LeftLowerArm, HumanBone::RightLowerArm);
        else if (Has(c, "clavicle") || Has(c, "shoulder") || Has(c, "collar")) sided(HumanBone::LeftShoulder, HumanBone::RightShoulder);
        else if (Has(c, "upperarm") || Has(c, "arm")) sided(HumanBone::LeftUpperArm, HumanBone::RightUpperArm);
        else if (Has(c, "toe") || Has(c, "ball")) sided(HumanBone::LeftToes, HumanBone::RightToes);
        else if (Has(c, "foot") || Has(c, "ankle")) sided(HumanBone::LeftFoot, HumanBone::RightFoot);
        else if (Has(c, "thigh") || Has(c, "upleg") || Has(c, "upperleg")) sided(HumanBone::LeftUpperLeg, HumanBone::RightUpperLeg);
        else if (Has(c, "calf") || Has(c, "shin") || Has(c, "lowerleg") || Has(c, "knee") || Has(c, "leg")) sided(HumanBone::LeftLowerLeg, HumanBone::RightLowerLeg);
        else if (side == 0 && (Has(c, "hips") || Has(c, "pelvis") || c == "hip")) raw[i] = HumanBone::Hips;
        else if (side == 0 && Has(c, "neck")) raw[i] = HumanBone::Neck;
        else if (side == 0 && Has(c, "head")) raw[i] = HumanBone::Head;
        else if (side == 0 && (Has(c, "spine") || Has(c, "chest") || Has(c, "torso") || Has(c, "abdomen"))) spineFamily.push_back((int)i);
    }
    // Keep the joint nearest the root for each bone (neck_01 over neck_02)
    std::vector<HumanBone> out(n, HumanBone::None);
    for (int b = 0; b < (int)HumanBone::Count; b++) {
        int best = -1;
        for (size_t i = 0; i < n; i++)
            if (raw[i] == (HumanBone)b && (best < 0 || Depth(s, (int)i) < Depth(s, best))) best = (int)i;
        if (best >= 0) out[best] = (HumanBone)b;
    }
    // Spine chain, root to neck: Spine, Chest, (..., ) UpperChest
    std::sort(spineFamily.begin(), spineFamily.end(), [&](int a, int b) { return Depth(s, a) < Depth(s, b); });
    if (!spineFamily.empty()) out[spineFamily.front()] = HumanBone::Spine;
    if (spineFamily.size() >= 2) out[spineFamily[1]] = HumanBone::Chest;
    if (spineFamily.size() >= 3) out[spineFamily.back()] = HumanBone::UpperChest;
    // No "hips"/"pelvis": the joint both thighs hang from
    if (std::find(out.begin(), out.end(), HumanBone::Hips) == out.end()) {
        int l = -1, r = -1;
        for (size_t i = 0; i < n; i++) {
            if (out[i] == HumanBone::LeftUpperLeg) l = (int)i;
            if (out[i] == HumanBone::RightUpperLeg) r = (int)i;
        }
        if (l >= 0 && r >= 0 && s.joints[l].parent == s.joints[r].parent && s.joints[l].parent >= 0 && out[s.joints[l].parent] == HumanBone::None)
            out[s.joints[l].parent] = HumanBone::Hips;
    }
    return out;
}

RetargetMap MapSkeletons(const Skeleton& src, const Skeleton& dst) {
    RetargetMap m;
    m.sourceOf.assign(dst.joints.size(), -1);
    std::vector<std::string> srcNames(src.joints.size());
    for (size_t i = 0; i < src.joints.size(); i++) srcNames[i] = NormalizeJointName(src.joints[i].name);
    int direct = 0;
    for (size_t j = 0; j < dst.joints.size(); j++) {
        std::string n = NormalizeJointName(dst.joints[j].name);
        if (n.empty()) continue;
        for (size_t i = 0; i < srcNames.size(); i++)
            if (srcNames[i] == n) { m.sourceOf[j] = (int)i; direct++; break; }
    }
    auto hs = ClassifyHumanoid(src), ht = ClassifyHumanoid(dst);
    for (size_t i = 0; i < hs.size(); i++) if (hs[i] == HumanBone::Hips) m.sourceHips = (int)i;
    for (size_t j = 0; j < ht.size(); j++) if (ht[j] == HumanBone::Hips) m.targetHips = (int)j;

    bool byName = direct >= (int)(0.6f * dst.joints.size()) && direct >= 3;
    if (!byName) {
        std::fill(m.sourceOf.begin(), m.sourceOf.end(), -1);
        for (size_t j = 0; j < ht.size(); j++) {
            if (ht[j] == HumanBone::None) continue;
            for (size_t i = 0; i < hs.size(); i++)
                if (hs[i] == ht[j]) { m.sourceOf[j] = (int)i; break; }
        }
    } else {
        // Humanoid bones the names missed
        for (size_t j = 0; j < ht.size(); j++) {
            if (m.sourceOf[j] >= 0 || ht[j] == HumanBone::None) continue;
            for (size_t i = 0; i < hs.size(); i++)
                if (hs[i] == ht[j]) { m.sourceOf[j] = (int)i; break; }
        }
    }
    for (int s : m.sourceOf) if (s >= 0) m.mapped++;
    // Same rig: matched by name and every matched joint rests the same way
    if (byName) {
        int same = 0, total = 0;
        for (size_t j = 0; j < dst.joints.size(); j++) {
            int i = m.sourceOf[j];
            if (i < 0) continue;
            total++;
            if (std::abs(glm::dot(src.joints[i].bindR, dst.joints[j].bindR)) > 0.999f) same++;
        }
        m.sameRig = total > 0 && same >= total * 95 / 100;
    }
    return m;
}

Ref<AnimationClip> RetargetClip(const AnimationClip& clip, const Skeleton& src, const Skeleton& dst, const RetargetMap& map) {
    if (map.mapped == 0) return nullptr;
    auto out = MakeRef<AnimationClip>();
    out->name = clip.name;
    out->duration = clip.duration;

    // Hip height ratio (model space), so a tall rig's walk doesn't make a short one hover
    Pose dstBind;
    BindPose(dst, dstBind);
    // Rest poses: the poses the meshes were bound in (the rigs' T- / A-poses), turned to face
    // the way the rig faces in the file's scene pose (some rigs are bound facing sideways)
    std::vector<mat4> Gs0, Gt0;
    BindPoseGlobals(src, Gs0);
    BindPoseGlobals(dst, Gt0);
    FaceLikeScenePose(src, Gs0, map.sourceHips);
    FaceLikeScenePose(dst, Gt0, map.targetHips);
    float ratio = 1.0f;
    if (map.sourceHips >= 0 && map.targetHips >= 0) {
        float hs = Gs0[map.sourceHips][3].y, ht = Gt0[map.targetHips][3].y;
        if (hs > 1e-3f && ht > 1e-3f) ratio = ht / hs;
    }

    if (map.sameRig) {
        // Copy rotations and scales; translations move by the source's offset from its rest,
        // scaled, so the target keeps its own bone lengths
        std::vector<int> targetOf(src.joints.size(), -1);
        for (size_t j = 0; j < map.sourceOf.size(); j++) if (map.sourceOf[j] >= 0) targetOf[map.sourceOf[j]] = (int)j;
        for (auto& ch : clip.channels) {
            if (ch.joint < 0 || ch.joint >= (int)targetOf.size() || targetOf[ch.joint] < 0) continue;
            int j = targetOf[ch.joint];
            JointChannel c = ch;
            c.joint = j;
            for (auto& t : c.translation.values) t = dst.joints[j].bindT + (t - src.joints[ch.joint].bindT) * ratio;
            out->channels.push_back(std::move(c));
        }
        return out->channels.empty() ? nullptr : out;
    }

    // Different rigs: transfer each bone's model-space rotation relative to its rest pose
    std::vector<quat> Qs0(src.joints.size()), Qt0(dst.joints.size());
    for (size_t i = 0; i < Qs0.size(); i++) Qs0[i] = RotationOf(Gs0[i]);
    for (size_t j = 0; j < Qt0.size(); j++) Qt0[j] = RotationOf(Gt0[j]);

    // Rest-pose alignment: rigs bound in different poses (T-pose vs A-pose, arms down) are lined
    // up bone by bone, so each target bone points where the source bone points.
    std::vector<quat> align(dst.joints.size(), quat(1, 0, 0, 0));
    for (size_t j = 0; j < dst.joints.size(); j++) {
        int sj = map.sourceOf[j];
        if (sj < 0) continue;
        // Nearest mapped descendant on both rigs
        int child = -1, best = 1 << 30;
        for (size_t k = 0; k < dst.joints.size(); k++) {
            if (map.sourceOf[k] < 0 || k == j) continue;
            int d = 0;
            int p = dst.joints[k].parent;
            for (; p >= 0 && p != (int)j; p = dst.joints[p].parent) d++;
            if (p == (int)j && d < best) { best = d; child = (int)k; }
        }
        if (child < 0) continue;
        vec3 dt = vec3(Gt0[child][3]) - vec3(Gt0[j][3]);
        vec3 ds = vec3(Gs0[map.sourceOf[child]][3]) - vec3(Gs0[sj][3]);
        if (glm::length(dt) < 1e-5f || glm::length(ds) < 1e-5f) continue;
        dt = glm::normalize(dt);
        ds = glm::normalize(ds);
        if (glm::dot(dt, ds) > 0.9999f) continue;
        align[j] = glm::rotation(dt, ds);
    }

    // The target's bind pose as local transforms. The clip is built on these (not on the file's
    // scene pose, which can differ - a rig saved with its arms down), and keys every joint whose
    // bind local differs from its scene-pose local, so playback starts from the same pose.
    std::vector<JointPose> bindLocal(dst.joints.size());
    for (size_t j = 0; j < dst.joints.size(); j++) {
        int p = dst.joints[j].parent;
        mat4 local = p >= 0 ? glm::inverse(Gt0[p]) * Gt0[j] : Gt0[j];
        DecomposeTRS(local, bindLocal[j].t, bindLocal[j].r, bindLocal[j].s);
        if (glm::dot(bindLocal[j].r, dstBind[j].r) < 0.0f) bindLocal[j].r = -bindLocal[j].r;
    }
    auto differs = [](const JointPose& a, const JointPose& b) {
        return glm::length(a.t - b.t) > 1e-4f * std::max(1.0f, glm::length(b.t)) || std::abs(glm::dot(a.r, b.r)) < 0.99999f ||
               glm::length(a.s - b.s) > 1e-4f;
    };

    std::vector<int> channelOf(dst.joints.size(), -1);
    for (size_t j = 0; j < dst.joints.size(); j++)
        if (map.sourceOf[j] >= 0 || differs(bindLocal[j], dstBind[j])) {
            channelOf[j] = (int)out->channels.size();
            JointChannel c;
            c.joint = (int)j;
            if (map.sourceOf[j] < 0) {
                c.rotation = { { 0.0f }, { bindLocal[j].r } };
                c.translation = { { 0.0f }, { bindLocal[j].t } };
                c.scale = { { 0.0f }, { bindLocal[j].s } };
            } else {
                c.translation = { { 0.0f }, { bindLocal[j].t } };
                c.scale = { { 0.0f }, { bindLocal[j].s } };
            }
            out->channels.push_back(c);
        }
    const float step = 1.0f / 30.0f;
    int frames = std::max(1, (int)std::ceil(clip.duration / step));
    Pose ps;
    std::vector<mat4> Gs, Gt(dst.joints.size());
    for (int f = 0; f <= frames; f++) {
        float t = std::min(clip.duration, f * step);
        SampleClip(clip, src, t, ps);
        Globals(src, ps, Gs);
        for (size_t j = 0; j < dst.joints.size(); j++) {
            int p = dst.joints[j].parent;
            mat4 parentG = p >= 0 ? Gt[p] : mat4(1.0f);
            JointPose jp = bindLocal[j];
            int s = map.sourceOf[j];
            if (s >= 0) {
                quat delta = RotationOf(Gs[s]) * glm::inverse(Qs0[s]);
                quat want = delta * align[j] * Qt0[j];
                jp.r = glm::normalize(glm::inverse(RotationOf(parentG)) * want);
                JointChannel& c = out->channels[channelOf[j]];
                if (!c.rotation.values.empty() && glm::dot(c.rotation.values.back(), jp.r) < 0.0f) jp.r = -jp.r;   // shortest path
                c.rotation.times.push_back(t);
                c.rotation.values.push_back(jp.r);
                if ((int)j == map.targetHips && map.sourceHips >= 0) {
                    vec3 move = (vec3(Gs[map.sourceHips][3]) - vec3(Gs0[map.sourceHips][3])) * ratio;
                    vec3 pos = vec3(Gt0[j][3]) + move;
                    jp.t = vec3(glm::inverse(parentG) * vec4(pos, 1.0f));
                    if (f == 0) { c.translation.times.clear(); c.translation.values.clear(); }
                    c.translation.times.push_back(t);
                    c.translation.values.push_back(jp.t);
                }
            }
            Gt[j] = parentG * ComposeTRS(jp.t, jp.r, jp.s);
        }
    }
    return out;
}

} // namespace eu
