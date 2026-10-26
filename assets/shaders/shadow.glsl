// Depth-only pass for the directional shadow map.
#type vertex
layout(location = 0) in vec3 aPos;
layout(location = 3) in ivec4 aJoints;
layout(location = 4) in vec4 aWeights;
#ifdef INSTANCED
layout(location = 5) in mat4 aInstance;
#endif
uniform mat4 uModel;
uniform mat4 uLightSpace;
#ifdef SKINNED
#define MAX_BONES 100
uniform mat4 uBones[MAX_BONES];
#endif
#ifdef GRASS
uniform float uTime;
#endif
void main() {
    mat4 model = uModel;
#ifdef INSTANCED
    model = uModel * aInstance;
#endif
    vec4 pos = vec4(aPos, 1.0);
#ifdef SKINNED
    float wsum = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
    if (wsum > 0.0) {
        mat4 skin = aWeights.x * uBones[aJoints.x] + aWeights.y * uBones[aJoints.y]
                  + aWeights.z * uBones[aJoints.z] + aWeights.w * uBones[aJoints.w];
        pos = skin * pos;
    }
#endif
    gl_Position = uLightSpace * model * pos;
}
#type fragment
void main() {}
