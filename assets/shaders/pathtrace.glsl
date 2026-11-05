// Progressive path tracer. One camera path per pixel per frame through a BVH of world-space
// triangles stored in buffer textures (see RayTracer.cpp for the layouts):
//   - next-event estimation: every bounce samples the sun (soft, with a small angular size)
//     and every point/spot light with a shadow ray
//   - BSDF: GGX specular + Lambert diffuse, importance-sampled by Fresnel/metalness
//   - escaped rays see the procedural sky; Russian roulette after the second bounce
// Light units match the rasterizer, so switching between Lit and Ray Traced keeps the same
// brightness; the difference is the light transport (bounce light, true reflections, soft
// contact shadows).
#type vertex
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
#include "lighting.glsl"
in vec2 vUV;
uniform samplerBuffer uTris;    // 9 texels per triangle
uniform samplerBuffer uNodes;   // 2 texels per BVH node
uniform samplerBuffer uMats;    // 3 texels per material
uniform sampler2D uPrev;        // previous accumulation
uniform float uBlend;           // weight of this frame's sample
uniform int uFrame;
uniform int uMaxBounces;
uniform int uHasGeometry;
uniform mat4 uViewProj;
uniform mat4 uInvViewProj;
uniform vec2 uResolution;
layout(location = 0) out vec4 oColor;
layout(location = 1) out int oID;

// ---------------------------------------------------------------- random numbers (PCG)
uint gSeed;
uint PCG(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float Rand() { gSeed = PCG(gSeed); return float(gSeed) * (1.0 / 4294967296.0); }

// ---------------------------------------------------------------- scene access
struct Hit {
    float t;
    int tri;
    vec2 bary;
};

float IntersectTri(vec3 o, vec3 d, vec3 a, vec3 b, vec3 c, out vec2 bary) {
    vec3 e1 = b - a, e2 = c - a, p = cross(d, e2);
    float det = dot(e1, p);
    bary = vec2(0.0);
    if (abs(det) < 1e-10) return -1.0;
    float inv = 1.0 / det;
    vec3 s = o - a;
    float u = dot(s, p) * inv;
    if (u < 0.0 || u > 1.0) return -1.0;
    vec3 q = cross(s, e1);
    float v = dot(d, q) * inv;
    if (v < 0.0 || u + v > 1.0) return -1.0;
    bary = vec2(u, v);
    return dot(e2, q) * inv;
}

bool SlabTest(vec3 o, vec3 invD, vec3 mn, vec3 mx, float tmax) {
    vec3 t0 = (mn - o) * invD, t1 = (mx - o) * invD;
    vec3 tl = min(t0, t1), th = max(t0, t1);
    float a = max(max(tl.x, tl.y), max(tl.z, 0.0));
    float b = min(min(th.x, th.y), min(th.z, tmax));
    return a <= b;
}

// Closest hit (anyHit = true: stop at the first hit closer than tmax - for shadow rays).
Hit Trace(vec3 o, vec3 d, float tmax, bool anyHit) {
    Hit h;
    h.t = tmax;
    h.tri = -1;
    h.bary = vec2(0.0);
    if (uHasGeometry == 0) return h;
    vec3 invD = 1.0 / mix(d, vec3(1e-12), lessThan(abs(d), vec3(1e-12)));
    int stack[48];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        int n = stack[--sp];
        vec4 a = texelFetch(uNodes, n * 2), b = texelFetch(uNodes, n * 2 + 1);
        if (!SlabTest(o, invD, a.xyz, b.xyz, h.t)) continue;
        int leftFirst = floatBitsToInt(a.w), count = floatBitsToInt(b.w);
        if (count > 0) {
            for (int i = 0; i < count; i++) {
                int t = leftFirst + i;
                vec3 p0 = texelFetch(uTris, t * 9).xyz, p1 = texelFetch(uTris, t * 9 + 1).xyz, p2 = texelFetch(uTris, t * 9 + 2).xyz;
                vec2 bc;
                float d0 = IntersectTri(o, d, p0, p1, p2, bc);
                if (d0 > 1e-4 && d0 < h.t) {
                    h.t = d0; h.tri = t; h.bary = bc;
                    if (anyHit) return h;
                }
            }
        } else if (sp < 46) {
            // Visit the nearer child first (better pruning)
            vec4 la = texelFetch(uNodes, leftFirst * 2), lb = texelFetch(uNodes, leftFirst * 2 + 1);
            vec4 ra = texelFetch(uNodes, (leftFirst + 1) * 2), rb = texelFetch(uNodes, (leftFirst + 1) * 2 + 1);
            float dl = dot((la.xyz + lb.xyz) * 0.5 - o, d), dr = dot((ra.xyz + rb.xyz) * 0.5 - o, d);
            if (dl < dr) { stack[sp++] = leftFirst + 1; stack[sp++] = leftFirst; }
            else { stack[sp++] = leftFirst; stack[sp++] = leftFirst + 1; }
        }
    }
    return h;
}

bool Visible(vec3 o, vec3 d, float dist) { return Trace(o, d, dist, true).tri < 0; }

struct Surface {
    vec3 P, N, Ng;
    vec3 albedo;
    float roughness, metallic, water;
    vec3 emissive;
    int entity;
};

Surface GetSurface(Hit h, vec3 o, vec3 d) {
    Surface s;
    int t = h.tri;
    vec4 t0 = texelFetch(uTris, t * 9), t1 = texelFetch(uTris, t * 9 + 1), t2 = texelFetch(uTris, t * 9 + 2);
    vec3 n0 = texelFetch(uTris, t * 9 + 3).xyz, n1 = texelFetch(uTris, t * 9 + 4).xyz, n2 = texelFetch(uTris, t * 9 + 5).xyz;
    vec3 c0 = texelFetch(uTris, t * 9 + 6).xyz, c1 = texelFetch(uTris, t * 9 + 7).xyz, c2 = texelFetch(uTris, t * 9 + 8).xyz;
    float w = 1.0 - h.bary.x - h.bary.y;
    s.P = o + d * h.t;
    s.Ng = normalize(cross(t1.xyz - t0.xyz, t2.xyz - t0.xyz));
    s.N = normalize(n0 * w + n1 * h.bary.x + n2 * h.bary.y);
    if (dot(s.Ng, d) > 0.0) s.Ng = -s.Ng;          // face the ray
    if (dot(s.N, s.Ng) < 0.0) s.N = -s.N;
    int m = int(t0.w);
    vec4 m0 = texelFetch(uMats, m * 3), m1 = texelFetch(uMats, m * 3 + 1), m2 = texelFetch(uMats, m * 3 + 2);
    s.albedo = m0.rgb * (c0 * w + c1 * h.bary.x + c2 * h.bary.y);
    s.roughness = clamp(m0.a, 0.03, 1.0);
    s.emissive = m1.rgb;
    s.metallic = m1.a;
    s.water = m2.x;
    s.entity = floatBitsToInt(t1.w);
    return s;
}

// ---------------------------------------------------------------- sky
vec3 SkyColor(vec3 dir, bool withSun) {
    float y = dir.y;
    vec3 sky = mix(ToLinear(uSkyHorizon), ToLinear(uSkyTop), pow(clamp(y, 0.0, 1.0), 0.6));
    vec3 ground = mix(ToLinear(uSkyHorizon), ToLinear(uGroundColor), clamp(-y * 3.0, 0.0, 1.0));
    vec3 col = y >= 0.0 ? sky : ground;
    if (withSun && uHasSun != 0) {
        float s = max(dot(dir, normalize(-uSunDir)), 0.0);
        col += uSunColor * (pow(s, 900.0) * 6.0 + pow(s, 12.0) * 0.12);
    }
    return col;
}

// ---------------------------------------------------------------- BSDF
mat3 Basis(vec3 n) {
    vec3 t = abs(n.y) < 0.999 ? normalize(cross(vec3(0, 1, 0), n)) : vec3(1, 0, 0);
    return mat3(t, cross(n, t), n);
}

// Physical BRDF value (no cosine): Lambert * (1 - F)(1 - metallic) + GGX specular.
vec3 EvalBRDF(Surface s, vec3 V, vec3 L) {
    float NdL = dot(s.N, L), NdV = max(dot(s.N, V), 1e-4);
    if (NdL <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float a = s.roughness * s.roughness;
    vec3 F0 = mix(vec3(s.water > 0.5 ? 0.02 : 0.04), s.albedo, s.metallic);
    vec3 F = F_Schlick(F0, max(dot(V, H), 0.0));
    vec3 spec = D_GGX(max(dot(s.N, H), 0.0), a) * V_SmithGGX(NdV, NdL, a) * F;
    vec3 diff = s.albedo * (1.0 - F) * (1.0 - s.metallic) / PI;
    return diff + spec;
}

// Direct light: sun + point + spot lights with shadow rays. Light colors are "artist units"
// (irradiance / PI), exactly as in the rasterizer: contribution = BRDF * PI * color * NdotL.
vec3 DirectLight(Surface s, vec3 V, int bounce) {
    vec3 sum = vec3(0.0);
    // Roughness regularization: a near-mirror lit by a tiny light gives rare, enormous
    // highlights (noise that never converges). Widening the lobe a little for light sampling
    // costs almost nothing visually; more after the first bounce, where nobody sees it sharp.
    s.roughness = max(s.roughness, bounce == 0 ? 0.12 : 0.35);
    vec3 origin = s.P + s.Ng * 2e-3;
    if (uHasSun != 0) {
        // Sample a cone ~1 degree wide around the sun direction: soft shadow edges
        vec3 L = normalize(-uSunDir);
        float r1 = Rand(), r2 = Rand();
        float cosMax = 0.99985;
        float ct = 1.0 - r1 * (1.0 - cosMax), st = sqrt(1.0 - ct * ct), ph = 6.2831853 * r2;
        L = normalize(Basis(L) * vec3(cos(ph) * st, sin(ph) * st, ct));
        float NdL = dot(s.N, L);
        if (NdL > 0.0 && Visible(origin, L, 1e5)) sum += EvalBRDF(s, V, L) * PI * uSunColor * NdL;
    }
    for (int i = 0; i < uPointCount; i++) {
        vec3 Lv = uPointPos[i] - s.P;
        float d = length(Lv);
        if (d > uPointRange[i]) continue;
        vec3 L = Lv / d;
        float NdL = dot(s.N, L);
        if (NdL <= 0.0) continue;
        float x = d / uPointRange[i];
        float atten = pow(clamp(1.0 - x * x * x * x, 0.0, 1.0), 2.0) / (1.0 + d * d * 0.25);
        if (Visible(origin, L, d - 0.05)) sum += EvalBRDF(s, V, L) * PI * uPointColor[i] * atten * NdL;
    }
    for (int i = 0; i < uSpotCount; i++) {
        vec3 Lv = uSpotPos[i] - s.P;
        float d = length(Lv);
        vec3 L = Lv / max(d, 1e-4);
        float NdL = dot(s.N, L);
        float cone = smoothstep(uSpotCos[i], uSpotCos[i] + 0.08, dot(-L, normalize(uSpotDir[i])));
        if (NdL <= 0.0 || cone <= 0.0 || d > uSpotRange[i]) continue;
        float x = d / uSpotRange[i];
        float atten = pow(clamp(1.0 - x * x * x * x, 0.0, 1.0), 2.0) / (1.0 + d * d * 0.25);
        if (Visible(origin, L, d - 0.05)) sum += EvalBRDF(s, V, L) * PI * uSpotColor[i] * atten * cone * NdL;
    }
    return sum;
}

// Samples the next direction; returns the throughput weight (BRDF * cos / pdf).
bool SampleBSDF(Surface s, vec3 V, out vec3 L, out vec3 weight) {
    float NdV = max(dot(s.N, V), 1e-4);
    vec3 F0 = mix(vec3(s.water > 0.5 ? 0.02 : 0.04), s.albedo, s.metallic);
    vec3 Fv = F_Schlick(F0, NdV);
    float pSpec = clamp(max(Fv.r, max(Fv.g, Fv.b)) * (1.0 - s.metallic) + s.metallic, 0.1, 0.95);
    mat3 B = Basis(s.N);
    float a = s.roughness * s.roughness;
    if (Rand() < pSpec) {
        // GGX half-vector sampling
        float r1 = Rand(), r2 = Rand();
        float ct = sqrt((1.0 - r1) / (1.0 + (a * a - 1.0) * r1)), st = sqrt(1.0 - ct * ct), ph = 6.2831853 * r2;
        vec3 H = B * vec3(cos(ph) * st, sin(ph) * st, ct);
        L = reflect(-V, H);
        float NdL = dot(s.N, L);
        if (NdL <= 0.0) return false;
        float NdH = max(dot(s.N, H), 1e-4), VdH = max(dot(V, H), 1e-4);
        vec3 F = F_Schlick(F0, VdH);
        // D * V * F * NdL / (D * NdH / (4 VdH))
        weight = F * V_SmithGGX(NdV, NdL, a) * NdL * 4.0 * VdH / NdH / pSpec;
    } else {
        float r1 = Rand(), r2 = Rand();
        float r = sqrt(r1), ph = 6.2831853 * r2;
        L = B * vec3(r * cos(ph), r * sin(ph), sqrt(max(0.0, 1.0 - r1)));
        vec3 H = normalize(V + L);
        vec3 F = F_Schlick(F0, max(dot(V, H), 0.0));
        weight = s.albedo * (1.0 - F) * (1.0 - s.metallic) / (1.0 - pSpec);
    }
    return true;
}

// ---------------------------------------------------------------- main
void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    gSeed = PCG(uint(px.x) + PCG(uint(px.y) + PCG(uint(uFrame))));
    // Jittered primary ray (accumulated anti-aliasing)
    vec2 ndc = (gl_FragCoord.xy + vec2(Rand(), Rand()) - 0.5) / uResolution * 2.0 - 1.0;
    vec4 nearP = uInvViewProj * vec4(ndc, -1.0, 1.0), farP = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 o = nearP.xyz / nearP.w;
    vec3 d = normalize(farP.xyz / farP.w - o);

    vec3 radiance = vec3(0.0), throughput = vec3(1.0);
    int primaryID = -1;
    float depth = 1.0;
    vec3 primaryP = vec3(0.0);
    bool primaryHit = false;
    for (int bounce = 0; bounce <= uMaxBounces; bounce++) {
        Hit h = Trace(o, d, 1e5, false);
        if (h.tri < 0) {
            // Primary rays see the sky as drawn; bounce rays receive it as ambient light.
            radiance += throughput * (bounce == 0 ? SkyColor(d, true) : SkyColor(d, false) * uAmbient);
            break;
        }
        Surface s = GetSurface(h, o, d);
        if (bounce == 0) {
            primaryID = s.entity;
            primaryHit = true;
            primaryP = s.P;
            vec4 clip = uViewProj * vec4(s.P, 1.0);
            depth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 1.0);
        }
        vec3 V = -d;
        vec3 contrib = throughput * (s.emissive + DirectLight(s, V, bounce));
        // Indirect light that happens to hit a small bright emitter is a firefly: clamp it.
        if (bounce > 0) contrib *= min(1.0, 2.0 / max(max(contrib.r, max(contrib.g, contrib.b)), 1e-4));
        radiance += contrib;
        vec3 L, w;
        if (!SampleBSDF(s, V, L, w)) break;
        throughput *= w;
        if (bounce >= 2) {   // Russian roulette
            float p = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
            if (Rand() > p) break;
            throughput /= p;
        }
        o = s.P + s.Ng * 2e-3 * (dot(L, s.Ng) > 0.0 ? 1.0 : -1.0);
        d = L;
    }
    if (primaryHit) radiance = ApplyFog(radiance, primaryP);
    radiance = min(radiance, vec3(64.0));   // clamp fireflies

    vec3 prev = texture(uPrev, vUV).rgb;
    oColor = vec4(mix(prev, radiance, uBlend), 1.0);
    oID = primaryID;
    gl_FragDepth = depth;
}
