#include "euphoria/animation/Locomotion.h"
#include "euphoria/scene/Components.h"
#include <algorithm>
#include <cctype>

namespace eu {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// "Armature|Walk" -> "walk", "mixamo.com|Run" -> "run"
std::string BaseName(const std::string& clip) {
    std::string s = Lower(clip);
    auto bar = s.find_last_of('|');
    if (bar != std::string::npos) s = s.substr(bar + 1);
    return s;
}

std::string Pick(const std::vector<std::string>& clips, std::initializer_list<const char*> keys,
                 std::initializer_list<const char*> avoid = {}) {
    // Pass 1: exact base name. Pass 2: substring.
    for (int pass = 0; pass < 2; pass++) {
        for (const char* key : keys) {
            for (auto& c : clips) {
                std::string b = BaseName(c);
                bool hit = pass == 0 ? b == key : b.find(key) != std::string::npos;
                if (!hit) continue;
                bool skip = false;
                for (const char* a : avoid) if (b.find(a) != std::string::npos) skip = true;
                if (!skip) return c;
            }
        }
    }
    return {};
}

} // namespace

LocomotionClips FindLocomotionClips(const std::vector<std::string>& clips) {
    LocomotionClips r;
    r.idle = Pick(clips, { "idle", "survey", "stand", "breath" });
    r.walk = Pick(clips, { "walk" });
    r.run = Pick(clips, { "run", "jog", "sprint" }, { "walk" });
    r.jump = Pick(clips, { "jump" });
    r.fall = Pick(clips, { "fall", "falling", "air" }, { "jump" });
    return r;
}

bool SetupLocomotion(AnimatorComponent& anim, const std::vector<std::string>& clipNames, float walkSpeed, float runSpeed) {
    LocomotionClips c = FindLocomotionClips(clipNames);
    if (!c.Usable()) return false;

    anim.params = { { "Speed", AnimParamType::Float, 0.0f }, { "Grounded", AnimParamType::Bool, 1.0f } };
    anim.states.clear();
    anim.transitions.clear();

    AnimState loco;
    loco.name = "Locomotion";
    loco.blendParam = "Speed";
    if (!c.idle.empty()) loco.blend.push_back({ c.idle, 0.0f });
    if (!c.walk.empty()) loco.blend.push_back({ c.walk, c.idle.empty() ? 0.0f : walkSpeed });
    if (!c.run.empty()) loco.blend.push_back({ c.run, (c.idle.empty() && c.walk.empty()) ? 0.0f : runSpeed });
    if (loco.blend.size() == 1) { loco.clip = loco.blend[0].clip; loco.blend.clear(); loco.blendParam.clear(); }
    anim.states.push_back(loco);
    anim.defaultState = loco.name;

    std::string air = !c.fall.empty() ? c.fall : c.jump;
    if (!air.empty()) {
        AnimState airborne;
        airborne.name = "Airborne";
        airborne.clip = air;
        airborne.loop = air == c.fall;      // a jump plays once and holds its last (tucked) pose
        anim.states.push_back(airborne);

        AnimTransition up;
        up.from = loco.name;
        up.to = airborne.name;
        up.conditions = { { "Grounded", AnimCondOp::IsFalse, 0.0f } };
        up.duration = 0.1f;
        AnimTransition land;
        land.from = airborne.name;
        land.to = loco.name;
        land.conditions = { { "Grounded", AnimCondOp::IsTrue, 0.0f } };
        land.duration = 0.15f;
        anim.transitions = { up, land };
    }
    return true;
}

} // namespace eu
