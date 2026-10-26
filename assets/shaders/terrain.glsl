#type vertex
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocal;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vLocal = aPos;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uViewProj * world;
}

#type fragment
#include "lighting.glsl"
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vLocal;
uniform vec3 uSand;
uniform vec3 uGrass;
uniform vec3 uRock;
uniform vec3 uSnow;
uniform float uSandHeight;
uniform float uSnowHeight;
uniform int uEntityID;
layout(location = 0) out vec4 oColor;
layout(location = 1) out int oID;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}

void main() {
    vec3 N = normalize(vNormal);
    float h = vLocal.y;
    float n = noise(vLocal.xz * 0.35) * 0.6 + noise(vLocal.xz * 2.1) * 0.4;
    float slope = 1.0 - N.y;
    vec3 grass = mix(uGrass * 0.85, uGrass * 1.12, n);
    vec3 c = grass;
    c = mix(ToLinear(uSand) * (0.9 + 0.2 * n), ToLinear(c), smoothstep(uSandHeight - 0.3, uSandHeight + 0.4, h + n * 0.3));
    vec3 rock = ToLinear(uRock) * (0.8 + 0.4 * n);
    c = mix(c, rock, smoothstep(0.28, 0.45, slope + (n - 0.5) * 0.1));
    c = mix(c, ToLinear(uSnow), smoothstep(uSnowHeight - 1.0, uSnowHeight + 1.0, h + n * 1.5) * (1.0 - smoothstep(0.4, 0.6, slope)));
    vec3 lit = ComputeLighting(c, N, vWorldPos, 0.05, 16.0);
    lit = ApplyFog(lit, vWorldPos);
    oColor = vec4(Tonemap(lit), 1.0);
    oID = uEntityID;
}
