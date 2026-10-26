// Procedural gradient sky with a sun disc, drawn as a fullscreen triangle.
#type vertex
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 1.0, 1.0);
}

#type fragment
#include "lighting.glsl"
in vec2 vNdc;
uniform mat4 uInvViewProj;
layout(location = 0) out vec4 oColor;
void main() {
    vec4 far = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec4 near = uInvViewProj * vec4(vNdc, -1.0, 1.0);
    vec3 dir = normalize(far.xyz / far.w - near.xyz / near.w);
    float y = dir.y;
    vec3 sky = mix(ToLinear(uSkyHorizon), ToLinear(uSkyTop), pow(clamp(y, 0.0, 1.0), 0.6));
    vec3 ground = mix(ToLinear(uSkyHorizon), ToLinear(uGroundColor), clamp(-y * 3.0, 0.0, 1.0));
    vec3 col = y >= 0.0 ? sky : ground;
    if (uHasSun != 0) {
        vec3 L = normalize(-uSunDir);
        float d = max(dot(dir, L), 0.0);
        col += uSunColor * (pow(d, 900.0) * 6.0 + pow(d, 12.0) * 0.12);
    }
    oColor = vec4(Tonemap(col), 1.0);
}
