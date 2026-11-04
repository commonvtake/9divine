// Screen-space ambient occlusion from the depth buffer alone (normals are reconstructed from
// depth derivatives), 16 hemisphere samples rotated per pixel. Output: R = visibility (1 = open).
#type vertex
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
in vec2 vUV;
uniform sampler2D uDepth;
uniform mat4 uProj;
uniform mat4 uInvProj;
uniform float uRadius;
layout(location = 0) out vec4 oAO;

vec3 ViewPos(vec2 uv) {
    float d = texture(uDepth, uv).r;
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
float Hash(float n) { return fract(sin(n) * 43758.5453); }
float IGN(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
    float d = texture(uDepth, vUV).r;
    if (d >= 1.0) { oAO = vec4(1.0); return; }   // sky
    vec3 P = ViewPos(vUV);
    vec3 N = normalize(cross(dFdx(P), dFdy(P)));
    if (dot(N, P) > 0.0) N = -N;
    float a = 6.2831853 * IGN(gl_FragCoord.xy);
    vec3 rnd = vec3(cos(a), sin(a), 0.0);
    vec3 T = normalize(rnd - N * dot(rnd, N));
    mat3 TBN = mat3(T, cross(N, T), N);
    float occ = 0.0;
    const int kSamples = 16;
    for (int i = 0; i < kSamples; i++) {
        // Points in the hemisphere, denser near the center
        float fi = float(i);
        vec3 s = normalize(vec3(Hash(fi * 3.1) * 2.0 - 1.0, Hash(fi * 7.7) * 2.0 - 1.0, Hash(fi * 1.3) * 0.9 + 0.1));
        float scale = (fi + 1.0) / float(kSamples);
        s *= mix(0.1, 1.0, scale * scale) * uRadius;
        vec3 q = P + TBN * s;
        vec4 o = uProj * vec4(q, 1.0);
        vec2 uv = o.xy / o.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) continue;
        float sceneZ = ViewPos(uv).z;
        float range = smoothstep(0.0, 1.0, uRadius / max(abs(P.z - sceneZ), 1e-4));
        occ += (sceneZ >= q.z + 0.02 * uRadius ? 1.0 : 0.0) * range;
    }
    oAO = vec4(1.0 - occ / float(kSamples));
}
