// Instanced grass clumps. aInstance[0] = (position.xyz, height), aInstance[1] = (yaw, tint, width, 0)
#type vertex
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;
layout(location = 5) in mat4 aInstance;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform float uTime;
uniform vec3 uCameraPosV;
uniform float uMaxDistance;
out vec3 vWorldPos;
out float vHeight;
out float vTint;
out float vFade;
void main() {
    vec3 base = aInstance[0].xyz;
    float height = aInstance[0].w;
    float yaw = aInstance[1].x;
    float width = aInstance[1].z;
    float c = cos(yaw), s = sin(yaw);
    vec3 p = vec3(aPos.x * width, aPos.y * height, aPos.z * width);
    p = vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z);
    vec4 world = uModel * vec4(base + p, 1.0);
    float bend = aUV.y * aUV.y;
    world.x += sin(uTime * 1.8 + world.x * 0.35 + world.z * 0.2) * 0.18 * bend;
    world.z += cos(uTime * 1.4 + world.z * 0.3) * 0.12 * bend;
    float dist = length(world.xyz - uCameraPosV);
    vFade = 1.0 - smoothstep(uMaxDistance * 0.7, uMaxDistance, dist);
    world.y -= (1.0 - vFade) * height;     // sink distant blades
    vWorldPos = world.xyz;
    vHeight = aUV.y;
    vTint = aInstance[1].y;
    gl_Position = uViewProj * world;
}

#type fragment
#include "lighting.glsl"
in vec3 vWorldPos;
in float vHeight;
in float vTint;
in float vFade;
uniform vec3 uGrassColor;
layout(location = 0) out vec4 oColor;
void main() {
    if (vFade <= 0.01) discard;
    vec3 base = ToLinear(uGrassColor);
    vec3 col = mix(base * 0.35, base * (0.95 + vTint * 0.2), vHeight);
    col *= 0.85 + vTint * 0.3;
    vec3 N = vec3(0.0, 1.0, 0.0);
    vec3 lit = col * AmbientLight(N);
    if (uHasSun != 0) {
        float ndl = max(dot(N, -uSunDir), 0.0) * 0.8 + 0.2;
        lit += col * ndl * uSunColor * ShadowFactor(vWorldPos, N);
    }
    lit = ApplyFog(lit, vWorldPos);
    oColor = vec4(Tonemap(lit), 1.0);
}
