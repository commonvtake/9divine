#pragma once
#include "euphoria/core/Base.h"

namespace eu {

// Trauma-based screen shake for the game camera. Add trauma on impacts (0..1; it adds up and
// is capped at 1); the shake is trauma squared, so small bumps are subtle and big hits are
// violent, and it fades out over about a second. Applied to whatever camera the running
// scene renders with, so gameplay code never has to touch the camera.
//
//   CameraShake::Add(0.25f);   // a sword hit
//   CameraShake::Add(0.7f);    // a ground slam
class CameraShake {
public:
    static void Add(float trauma);
    static float Trauma() { return s_Trauma; }
    static void Reset() { s_Trauma = 0.0f; }

    // Engine hooks: Update runs once per frame on real time (it keeps going through
    // hit-stop), Apply offsets and turns the camera's world matrix.
    static void Update(float unscaledDt);
    static void Apply(mat4& cameraWorld);

    static float maxAngle;      // degrees at full trauma
    static float maxOffset;     // metres at full trauma
    static float frequency;     // how fast the shake wobbles
    static float decay;         // trauma lost per second

private:
    static float s_Trauma, s_Time;
};

} // namespace eu
