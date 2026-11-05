#include "euphoria/scene/CameraShake.h"
#include <algorithm>
#include <cmath>

namespace eu {

float CameraShake::maxAngle = 3.0f;
float CameraShake::maxOffset = 0.12f;
float CameraShake::frequency = 24.0f;
float CameraShake::decay = 1.7f;
float CameraShake::s_Trauma = 0.0f;
float CameraShake::s_Time = 0.0f;

void CameraShake::Add(float trauma) { s_Trauma = std::clamp(s_Trauma + trauma, 0.0f, 1.0f); }

void CameraShake::Update(float dt) {
    s_Time += dt;
    s_Trauma = std::max(0.0f, s_Trauma - decay * dt);
}

// Smooth pseudo-noise in [-1, 1]: a few incommensurate sines per channel.
static float Wobble(float t, float seed) {
    return (std::sin(t * 1.0f + seed) * 0.5f + std::sin(t * 2.31f + seed * 1.7f) * 0.3f + std::sin(t * 4.77f + seed * 2.9f) * 0.2f);
}

void CameraShake::Apply(mat4& world) {
    if (s_Trauma <= 0.0f) return;
    float s = s_Trauma * s_Trauma;
    float t = s_Time * frequency;
    float yaw = glm::radians(maxAngle * s * Wobble(t, 0.0f));
    float pitch = glm::radians(maxAngle * s * Wobble(t, 11.0f));
    float roll = glm::radians(maxAngle * 0.6f * s * Wobble(t, 23.0f));
    vec3 offset = maxOffset * s * vec3(Wobble(t, 31.0f), Wobble(t, 47.0f), 0.0f);
    world = world * glm::translate(mat4(1.0f), offset) * glm::mat4_cast(quat(vec3(pitch, yaw, roll)));
}

} // namespace eu
