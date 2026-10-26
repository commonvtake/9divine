// Standard lit material. Variants: SKINNED (GPU skinning), INSTANCED (per-instance matrix).
#type vertex
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in ivec4 aJoints;
layout(location = 4) in vec4 aWeights;
#ifdef INSTANCED
layout(location = 5) in mat4 aInstance;
#endif
uniform mat4 uModel;
uniform mat4 uViewProj;
#ifdef SKINNED
#define MAX_BONES 100
uniform mat4 uBones[MAX_BONES];
#endif
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
void main() {
    mat4 model = uModel;
#ifdef INSTANCED
    model = uModel * aInstance;
#endif
    vec4 pos = vec4(aPos, 1.0);
    vec3 nrm = aNormal;
#ifdef SKINNED
    float wsum = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
    if (wsum > 0.0) {
        mat4 skin = aWeights.x * uBones[aJoints.x] + aWeights.y * uBones[aJoints.y]
                  + aWeights.z * uBones[aJoints.z] + aWeights.w * uBones[aJoints.w];
        pos = skin * pos;
        nrm = mat3(skin) * nrm;
    }
#endif
    vec4 world = model * pos;
    vWorldPos = world.xyz;
    vNormal = mat3(transpose(inverse(model))) * nrm;
    vUV = aUV;
    gl_Position = uViewProj * world;
}

#type fragment
#include "lighting.glsl"
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
uniform vec4 uColor;
uniform sampler2D uAlbedoMap;
uniform float uTiling;
uniform float uSpecular;
uniform float uShininess;
uniform vec3 uEmissive;
uniform int uEntityID;
layout(location = 0) out vec4 oColor;
layout(location = 1) out int oID;
void main() {
    vec4 tex = texture(uAlbedoMap, vUV * uTiling);
    vec4 base = vec4(ToLinear(uColor.rgb) * ToLinear(tex.rgb), uColor.a * tex.a);
    if (base.a < 0.35) discard;
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 c = ComputeLighting(base.rgb, N, vWorldPos, uSpecular, uShininess) + uEmissive;
    c = ApplyFog(c, vWorldPos);
    oColor = vec4(Tonemap(c), 1.0);
    oID = uEntityID;
}
