#pragma once
#include "euphoria/core/Base.h"
#include <string>
#include <vector>

namespace eu {

struct AnimatorComponent;

// Clips picked out of a model by name, for characters authored in Blender (or Mixamo, etc.).
struct LocomotionClips {
    std::string idle, walk, run, jump, fall;
    bool Usable() const { return !idle.empty() || !walk.empty() || !run.empty(); }
};

// Matches clip names case-insensitively, ignoring exporter prefixes like "Armature|":
//   idle: idle / survey / stand / breath     walk: walk     run: run / jog / sprint
//   jump: jump                               fall: fall / air / falling
// An exact name ("Walk") wins over a partial one ("Walk_Old").
LocomotionClips FindLocomotionClips(const std::vector<std::string>& clipNames);

// Rebuilds `anim` as a ready-to-drive locomotion controller:
//   - "Locomotion": a 1D blend space on the Speed parameter (idle 0 -> walk -> run),
//   - "Airborne" (only if a jump or fall clip exists), entered when the Grounded bool goes false
//     and left when it comes back. Grounded starts true so an undriven animator stays on the ground.
// `walkSpeed` / `runSpeed` are the movement speeds (m/s) the walk and run clips were animated for,
// so feet don't slide. Returns false (and leaves `anim` untouched) if no usable clip was found.
bool SetupLocomotion(AnimatorComponent& anim, const std::vector<std::string>& clipNames,
                     float walkSpeed = 2.0f, float runSpeed = 6.0f);

} // namespace eu
