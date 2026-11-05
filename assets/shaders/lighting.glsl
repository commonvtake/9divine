// Shared lighting, shadows, ambient, reflections and fog (included by fragment shaders).
// Everything here works in linear HDR: shaders write unclamped light, and the post pass
// (post_composite.glsl) applies exposure, tone mapping, grading and gamma.
uniform vec3 uCameraPos;
uniform vec3 uCameraForward;
uniform int uHasSun;
uniform vec3 uSunDir;          // direction the light travels
uniform vec3 uSunColor;        // linear color * intensity
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uGroundColor;
uniform float uAmbient;

#define MAX_POINT_LIGHTS 8
#define MAX_SPOT_LIGHTS 4
uniform int uPointCount;
uniform vec3 uPointPos[MAX_POINT_LIGHTS];
uniform vec3 uPointColor[MAX_POINT_LIGHTS];
uniform float uPointRange[MAX_POINT_LIGHTS];
uniform int uSpotCount;
uniform vec3 uSpotPos[MAX_SPOT_LIGHTS];
uniform vec3 uSpotDir[MAX_SPOT_LIGHTS];
uniform vec3 uSpotColor[MAX_SPOT_LIGHTS];
uniform float uSpotRange[MAX_SPOT_LIGHTS];
uniform float uSpotCos[MAX_SPOT_LIGHTS];

// Cascaded shadow maps: one layer per cascade, split by view depth.
#define MAX_CASCADES 4
uniform sampler2DArrayShadow uShadowMap;
uniform mat4 uCascadeMats[MAX_CASCADES];
uniform float uCascadeEnds[MAX_CASCADES];   // view-space depth where each cascade ends
uniform float uCascadeTexel[MAX_CASCADES];  // world size of one shadow texel
uniform int uCascadeCount;
uniform int uHasShadows;
uniform float uShadowSoftness;              // filter radius in texels

// Sky lighting as 3rd-order spherical harmonics (9 RGB coefficients, already convolved
// with the cosine lobe so evaluating gives irradiance / PI).
uniform vec3 uSkySH[9];
uniform int uHasSkySH;

// Reflection cubemap captured around the camera (HDR, mipmapped by roughness).
uniform samplerCube uEnvMap;
uniform int uHasEnvMap;
uniform float uEnvMaxLod;
uniform float uReflectionIntensity;

uniform int uFog;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uExposure;

vec3 ToLinear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }

// Per-pixel noise in [0,1) (Jimenez's interleaved gradient noise) to rotate filter kernels.
float IGN(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

const vec2 kPoisson[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870), vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464), vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790));

float SampleCascade(int c, vec3 worldPos, vec3 N, vec2 rot) {
    // Normal offset grows with the cascade's texel size so far cascades don't acne.
    vec4 ls = uCascadeMats[c] * vec4(worldPos + N * uCascadeTexel[c] * 1.5, 1.0);
    vec3 p = ls.xyz / ls.w * 0.5 + 0.5;
    if (p.z > 1.0) return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0).xy);
    float radius = max(uShadowSoftness, 0.5);
    float bias = 0.0006 * float(c + 1);
    float s = 0.0;
    for (int i = 0; i < 16; i++) {
        vec2 o = vec2(kPoisson[i].x * rot.x - kPoisson[i].y * rot.y, kPoisson[i].x * rot.y + kPoisson[i].y * rot.x);
        s += texture(uShadowMap, vec4(p.xy + o * texel * radius, float(c), p.z - bias));
    }
    return s / 16.0;
}

float ShadowFactor(vec3 worldPos, vec3 N) {
    if (uHasShadows == 0) return 1.0;
    float depth = dot(worldPos - uCameraPos, uCameraForward);
    int c = 0;
    while (c < uCascadeCount - 1 && depth > uCascadeEnds[c]) c++;
    if (depth > uCascadeEnds[uCascadeCount - 1]) return 1.0;
    float a = 6.2831853 * IGN(gl_FragCoord.xy);
    vec2 rot = vec2(cos(a), sin(a));
    float s = SampleCascade(c, worldPos, N, rot);
    // Blend into the next cascade over the last 10% of this one, so the seam is invisible.
    float start = c == 0 ? 0.0 : uCascadeEnds[c - 1];
    float t = (depth - start) / max(uCascadeEnds[c] - start, 1e-3);
    if (t > 0.9) {
        float next = c + 1 < uCascadeCount ? SampleCascade(c + 1, worldPos, N, rot) : 1.0;
        s = mix(s, next, (t - 0.9) / 0.1);
    }
    return s;
}

// Irradiance from the sky. With SH: what the real sky gradient + ground bounce look like
// from direction N. Without: a simple hemisphere blend.
vec3 AmbientLight(vec3 N) {
    if (uHasSkySH != 0) {
        vec3 r = uSkySH[0] * 0.282095
               + uSkySH[1] * 0.488603 * N.y + uSkySH[2] * 0.488603 * N.z + uSkySH[3] * 0.488603 * N.x
               + uSkySH[4] * 1.092548 * N.x * N.y + uSkySH[5] * 1.092548 * N.y * N.z
               + uSkySH[6] * 0.315392 * (3.0 * N.z * N.z - 1.0)
               + uSkySH[7] * 1.092548 * N.x * N.z + uSkySH[8] * 0.546274 * (N.x * N.x - N.y * N.y);
        return max(r, vec3(0.0)) * uAmbient;
    }
    float t = N.y * 0.5 + 0.5;
    vec3 sky = mix(ToLinear(uSkyHorizon), ToLinear(uSkyTop), 0.5);
    return mix(ToLinear(uGroundColor), sky, t) * uAmbient;
}

// ---------------------------------------------------------------- PBR (Cook-Torrance)
// GGX normal distribution, height-correlated Smith visibility, Schlick Fresnel. Light
// intensities are "artist units": a white Lambert surface facing a light of intensity I gets
// I, as with the old Blinn-Phong model, so existing scenes keep their brightness.
const float PI = 3.14159265;

float D_GGX(float NdH, float a) {
    float a2 = a * a;
    float d = NdH * NdH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float V_SmithGGX(float NdV, float NdL, float a) {
    float a2 = a * a;
    float gv = NdL * sqrt(NdV * NdV * (1.0 - a2) + a2);
    float gl = NdV * sqrt(NdL * NdL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}
vec3 F_Schlick(vec3 F0, float c) { return F0 + (1.0 - F0) * pow(1.0 - c, 5.0); }
vec3 F_SchlickRoughness(vec3 F0, float c, float r) { return F0 + (max(vec3(1.0 - r), F0) - F0) * pow(1.0 - c, 5.0); }

// One light's contribution. `radiance` = light color * intensity * attenuation * shadow.
vec3 BRDF(vec3 albedo, float metallic, float roughness, vec3 N, vec3 V, vec3 L, vec3 radiance) {
    float NdL = max(dot(N, L), 0.0);
    if (NdL <= 0.0) return vec3(0.0);
    vec3 H = normalize(L + V);
    float NdV = max(dot(N, V), 1e-4), NdH = max(dot(N, H), 0.0), VdH = max(dot(V, H), 0.0);
    float a = max(roughness * roughness, 0.002);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F_Schlick(F0, VdH);
    vec3 spec = D_GGX(NdH, a) * V_SmithGGX(NdV, NdL, a) * F;
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    return (kd * albedo + spec * PI) * radiance * NdL;
}

// Cheap analytic split-sum environment BRDF (Karis, "Mobile PBR").
vec2 EnvBRDFApprox(float NdV, float roughness) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// What the surroundings look like in direction R, blurred by roughness: the live reflection
// cubemap when there is one, otherwise the procedural sky above / ground below.
vec3 EnvironmentColor(vec3 R, float roughness) {
    if (uHasEnvMap != 0)
        return textureLod(uEnvMap, R, roughness * uEnvMaxLod).rgb * uReflectionIntensity;
    vec3 top = ToLinear(uSkyTop), hor = ToLinear(uSkyHorizon), gnd = ToLinear(uGroundColor);
    float t = R.y;
    float blur = 0.15 + roughness * 0.85;
    vec3 sky = mix(hor, top, smoothstep(0.0, 0.6 + blur * 0.4, t));
    vec3 below = mix(hor, gnd, smoothstep(0.0, 0.15 + blur * 0.3, -t));
    vec3 c = t >= 0.0 ? sky : below;
    return mix(c, mix(hor, mix(top, gnd, 0.5), 0.5), blur * 0.5) * uAmbient * uReflectionIntensity;
}

vec3 ComputePBR(vec3 albedo, float metallic, float roughness, float ao, vec3 N, vec3 P) {
    vec3 V = normalize(uCameraPos - P);
    float NdV = max(dot(N, V), 1e-4);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Ambient: diffuse from the sky hemisphere, specular from the environment.
    vec3 F = F_SchlickRoughness(F0, NdV, roughness);
    vec2 ab = EnvBRDFApprox(NdV, roughness);
    vec3 diffuseAmb = albedo * (1.0 - F) * (1.0 - metallic) * AmbientLight(N);
    vec3 specAmb = EnvironmentColor(reflect(-V, N), roughness) * (F0 * ab.x + ab.y);
    vec3 color = (diffuseAmb + specAmb) * ao;

    if (uHasSun != 0) {
        vec3 L = normalize(-uSunDir);
        color += BRDF(albedo, metallic, roughness, N, V, L, uSunColor * ShadowFactor(P, N));
    }
    for (int i = 0; i < uPointCount; i++) {
        vec3 Lv = uPointPos[i] - P;
        float d = length(Lv);
        float x = d / uPointRange[i];
        float atten = pow(clamp(1.0 - x * x * x * x, 0.0, 1.0), 2.0) / (1.0 + d * d * 0.25);
        color += BRDF(albedo, metallic, roughness, N, V, Lv / max(d, 1e-4), uPointColor[i] * atten);
    }
    for (int i = 0; i < uSpotCount; i++) {
        vec3 Lv = uSpotPos[i] - P;
        float d = length(Lv);
        vec3 L = Lv / max(d, 1e-4);
        float x = d / uSpotRange[i];
        float atten = pow(clamp(1.0 - x * x * x * x, 0.0, 1.0), 2.0) / (1.0 + d * d * 0.25);
        float cone = smoothstep(uSpotCos[i], uSpotCos[i] + 0.08, dot(-L, normalize(uSpotDir[i])));
        color += BRDF(albedo, metallic, roughness, N, V, L, uSpotColor[i] * atten * cone);
    }
    return color;
}

// Older entry point (terrain, foliage): a dielectric whose roughness comes from a Blinn-Phong
// exponent.
float RoughnessFromShininess(float n) { return clamp(pow(2.0 / (n + 2.0), 0.25), 0.03, 1.0); }
vec3 ComputeLighting(vec3 albedo, vec3 N, vec3 P, float specular, float shininess) {
    return ComputePBR(albedo, 0.0, RoughnessFromShininess(shininess), 1.0, N, P);
}

vec3 ApplyFog(vec3 c, vec3 P) {
    if (uFog == 0) return c;
    float d = length(P - uCameraPos);
    float f = 1.0 - exp(-pow(d * uFogDensity, 1.6));
    return mix(c, ToLinear(uFogColor), clamp(f, 0.0, 1.0));
}

// Shaders write linear HDR; exposure, tone mapping and gamma happen in post_composite.glsl.
// (Kept as a named step so every output goes through one place.)
vec3 Tonemap(vec3 c) { return max(c, vec3(0.0)); }
