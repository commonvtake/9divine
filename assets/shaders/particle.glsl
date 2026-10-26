// Camera-facing particles. aInstance[0] = (position, size), aInstance[1] = color, aInstance[2].x = rotation
#type vertex
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;
layout(location = 5) in mat4 aInstance;
uniform mat4 uViewProj;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
out vec2 vUV;
out vec4 vColor;
out vec3 vWorldPos;
void main() {
    float size = aInstance[0].w;
    float r = aInstance[2].x;
    vec2 q = vec2(cos(r) * aPos.x - sin(r) * aPos.y, sin(r) * aPos.x + cos(r) * aPos.y);
    vec3 world = aInstance[0].xyz + (uCamRight * q.x + uCamUp * q.y) * size;
    vUV = aUV;
    vColor = aInstance[1];
    vWorldPos = world;
    gl_Position = uViewProj * vec4(world, 1.0);
}

#type fragment
#include "lighting.glsl"
in vec2 vUV;
in vec4 vColor;
in vec3 vWorldPos;
layout(location = 0) out vec4 oColor;
void main() {
    float d = length(vUV - 0.5) * 2.0;
    float a = smoothstep(1.0, 0.0, d);
    a *= a;
    vec3 col = ToLinear(vColor.rgb) * 2.0;
    oColor = vec4(Tonemap(col), vColor.a * a);
}
