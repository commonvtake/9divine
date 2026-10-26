#type vertex
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform float uTime;
uniform float uWaveHeight;
uniform float uWaveScale;
uniform float uWaveSpeed;
out vec3 vWorldPos;
out vec3 vNormal;
float Wave(vec2 p, float t) {
    return sin(p.x * 1.0 + t) * 0.5 + sin(p.y * 1.3 + t * 1.1) * 0.35 + sin((p.x + p.y) * 2.1 + t * 1.7) * 0.15;
}
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vec2 p = world.xz * uWaveScale;
    float t = uTime * uWaveSpeed;
    float h = Wave(p, t);
    float e = 0.1;
    float hx = Wave(p + vec2(e, 0.0), t), hz = Wave(p + vec2(0.0, e), t);
    world.y += h * uWaveHeight;
    vec3 n = normalize(vec3(-(hx - h) / e * uWaveHeight * uWaveScale * 4.0, 1.0, -(hz - h) / e * uWaveHeight * uWaveScale * 4.0));
    vWorldPos = world.xyz;
    vNormal = n;
    gl_Position = uViewProj * world;
}

#type fragment
#include "lighting.glsl"
in vec3 vWorldPos;
in vec3 vNormal;
uniform vec4 uShallow;
uniform vec4 uDeep;
layout(location = 0) out vec4 oColor;
void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 4.0);
    float facing = max(dot(N, V), 0.0);
    vec3 water = mix(ToLinear(uDeep.rgb), ToLinear(uShallow.rgb), facing * 0.6);
    vec3 sky = mix(ToLinear(uSkyHorizon), ToLinear(uSkyTop), 0.4);
    vec3 col = mix(water * (AmbientLight(vec3(0, 1, 0)) + 0.55), sky, fresnel * 0.45);
    if (uHasSun != 0) {
        vec3 L = normalize(-uSunDir);
        vec3 H = normalize(L + V);
        col += pow(max(dot(N, H), 0.0), 220.0) * uSunColor * 1.5 * ShadowFactor(vWorldPos, N);
        col += water * max(dot(N, L), 0.0) * uSunColor * 0.25;
    }
    col = ApplyFog(col, vWorldPos);
    float alpha = mix(uShallow.a, uDeep.a, 1.0 - facing) + fresnel * 0.2;
    oColor = vec4(Tonemap(col), clamp(alpha, 0.0, 1.0));
}
