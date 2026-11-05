// Standard lit material. Variants: SKINNED (GPU skinning), INSTANCED (per-instance matrix),
// TOON (banded cel lighting from toon.glsl instead of smooth Blinn-Phong),
// BATCHED (automatic instancing: per-instance matrix + entity ID; implies INSTANCED).
#type vertex
#if defined(BATCHED) && !defined(INSTANCED)
#define INSTANCED
#endif
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in ivec4 aJoints;
layout(location = 4) in vec4 aWeights;
layout(location = 11) in vec4 aTangent;
#ifdef INSTANCED
layout(location = 5) in mat4 aInstance;
#endif
#ifdef BATCHED
layout(location = 10) in int aEntityID;
flat out int vEntityID;
#endif
uniform mat4 uModel;
uniform mat4 uViewProj;
#ifdef SKINNED
#define MAX_BONES 100
uniform mat4 uBones[MAX_BONES];
#endif
out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUV;
void main() {
    mat4 model = uModel;
#ifdef INSTANCED
    model = uModel * aInstance;
#endif
    vec4 pos = vec4(aPos, 1.0);
    vec3 nrm = aNormal;
    vec3 tan = aTangent.xyz;
#ifdef SKINNED
    float wsum = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
    if (wsum > 0.0) {
        mat4 skin = aWeights.x * uBones[aJoints.x] + aWeights.y * uBones[aJoints.y]
                  + aWeights.z * uBones[aJoints.z] + aWeights.w * uBones[aJoints.w];
        pos = skin * pos;
        nrm = mat3(skin) * nrm;
        tan = mat3(skin) * tan;
    }
#endif
    vec4 world = model * pos;
    vWorldPos = world.xyz;
    vNormal = mat3(transpose(inverse(model))) * nrm;
    vTangent = vec4(mat3(model) * tan, aTangent.w);
    vUV = aUV;
#ifdef BATCHED
    vEntityID = aEntityID;
#endif
    gl_Position = uViewProj * world;
}

#type fragment
#include "lighting.glsl"
#ifdef TOON
#include "toon.glsl"
#endif
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUV;
#ifdef BATCHED
flat in int vEntityID;
#endif
uniform vec4 uColor;
uniform sampler2D uAlbedoMap;
uniform float uTiling;
uniform float uMetallic;
uniform float uRoughness;
uniform sampler2D uNormalMap;      // tangent space; flat 1x1 when unused
uniform float uNormalScale;        // 0 = ignore the normal map
uniform sampler2D uMetalRoughMap;  // G = roughness, B = metallic (multiplies the factors)
uniform sampler2D uOcclusionMap;   // R = ambient occlusion
uniform vec3 uEmissive;
uniform int uEntityID;
layout(location = 0) out vec4 oColor;
layout(location = 1) out int oID;
void main() {
    vec4 tex = texture(uAlbedoMap, vUV * uTiling);
    vec4 base = vec4(ToLinear(uColor.rgb) * ToLinear(tex.rgb), uColor.a * tex.a);
    if (base.a < 0.35) discard;
    vec2 uv = vUV * uTiling;
    vec3 N = normalize(vNormal);
    if (uNormalScale > 0.0 && dot(vTangent.xyz, vTangent.xyz) > 1e-8) {
        vec3 T = normalize(vTangent.xyz - N * dot(N, vTangent.xyz));
        vec3 B = cross(N, T) * (vTangent.w < 0.0 ? -1.0 : 1.0);
        vec3 tn = texture(uNormalMap, uv).xyz * 2.0 - 1.0;
        tn.xy *= uNormalScale;
        N = normalize(mat3(T, B, N) * tn);
    }
    if (!gl_FrontFacing) N = -N;
    vec4 mr = texture(uMetalRoughMap, uv);
    float metallic = clamp(uMetallic * mr.b, 0.0, 1.0);
    float roughness = clamp(uRoughness * mr.g, 0.03, 1.0);
    float ao = texture(uOcclusionMap, uv).r;
#ifdef TOON
    // Cel shading keeps its hard highlight; size it from roughness, strength from metalness.
    float a = roughness * roughness;
    float shininess = clamp(2.0 / max(a * a, 1e-4) - 2.0, 4.0, 512.0);
    float specular = mix(0.5 * (1.0 - roughness), 1.0, metallic);
    vec3 c = ComputeToonLighting(base.rgb, N, vWorldPos, specular, shininess) * mix(1.0, ao, 0.5) + uEmissive;
#else
    vec3 c = ComputePBR(base.rgb, metallic, roughness, ao, N, vWorldPos) + uEmissive;
#endif
    c = ApplyFog(c, vWorldPos);
    oColor = vec4(Tonemap(c), 1.0);
#ifdef BATCHED
    oID = vEntityID;
#else
    oID = uEntityID;
#endif
}
