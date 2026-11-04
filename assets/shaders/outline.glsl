// Ink outline via the inverted-hull technique.
// The mesh is drawn a second time with front faces culled, every vertex pushed outward along
// its smoothed outline normal (attribute 9). Only the back faces of this slightly larger shell
// survive, and they peek out around the silhouette as a solid outline.
// The push is done in clip space, so the width is measured in pixels, not world units.
// Variants: SKINNED.
#type vertex
layout(location = 0) in vec3 aPos;
layout(location = 3) in ivec4 aJoints;
layout(location = 4) in vec4 aWeights;
layout(location = 9) in vec3 aOutlineNormal;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform vec2 uViewportSize;    // render target size in pixels
uniform float uOutlineWidth;   // pixels at close range
#ifdef SKINNED
#define MAX_BONES 100
uniform mat4 uBones[MAX_BONES];
#endif
out vec3 vWorldPos;
void main() {
    vec4 pos = vec4(aPos, 1.0);
    vec3 nrm = aOutlineNormal;
#ifdef SKINNED
    float wsum = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
    if (wsum > 0.0) {
        mat4 skin = aWeights.x * uBones[aJoints.x] + aWeights.y * uBones[aJoints.y]
                  + aWeights.z * uBones[aJoints.z] + aWeights.w * uBones[aJoints.w];
        pos = skin * pos;
        nrm = mat3(skin) * nrm;
    }
#endif
    vec4 world = uModel * pos;
    vec3 worldN = normalize(mat3(transpose(inverse(uModel))) * nrm);
    vWorldPos = world.xyz;

    vec4 clip = uViewProj * world;
    // Direction of the normal on screen, measured in pixels. (Normalizing in NDC instead would
    // skew it on non-square screens, where one NDC unit is more pixels wide than tall.)
    vec2 dirPx = (uViewProj * vec4(worldN, 0.0)).xy * uViewportSize;
    float len = length(dirPx);
    if (len > 1e-6) {
        // Full width up close, tapering to 35% far away so distant objects don't turn to ink.
        float taper = clamp(12.0 / max(clip.w, 1e-3), 0.35, 1.0);
        vec2 offsetNdc = (dirPx / len) * (uOutlineWidth * taper) * 2.0 / uViewportSize;
        clip.xy += offsetNdc * clip.w;   // multiply by w so the offset survives the perspective divide
    }
    gl_Position = clip;
}

#type fragment
#include "lighting.glsl"
in vec3 vWorldPos;
uniform vec4 uOutlineColor;    // sRGB
uniform int uEntityID;
layout(location = 0) out vec4 oColor;
layout(location = 1) out int oID;
void main() {
    // Outlines are unlit ink, but still fade into fog like everything else.
    vec3 c = ApplyFog(ToLinear(uOutlineColor.rgb), vWorldPos);
    oColor = vec4(Tonemap(c), 1.0);
    oID = uEntityID;   // clicking the outline selects the entity in the editor
}
