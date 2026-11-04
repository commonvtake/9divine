// Shared lighting, shadows, fog and tone mapping (included by fragment shaders).
uniform vec3 uCameraPos;
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

uniform sampler2DShadow uShadowMap;
uniform mat4 uLightSpace;
uniform int uHasShadows;

uniform int uFog;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uExposure;

vec3 ToLinear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }

float ShadowFactor(vec3 worldPos, vec3 N) {
    if (uHasShadows == 0) return 1.0;
    vec4 ls = uLightSpace * vec4(worldPos + N * 0.06, 1.0);
    vec3 p = ls.xyz / ls.w * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float bias = 0.0012;
    float s = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            s += texture(uShadowMap, vec3(p.xy + vec2(x, y) * texel * 1.25, p.z - bias));
    s /= 9.0;
    // fade out near the edge of the shadow map
    vec2 edge = min(p.xy, 1.0 - p.xy);
    float fade = clamp(min(edge.x, edge.y) * 12.0, 0.0, 1.0);
    return mix(1.0, s, fade);
}

vec3 AmbientLight(vec3 N) {
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

// What the surroundings look like in direction R, blurred by roughness: the procedural sky
// above the horizon, the ground color below.
vec3 EnvironmentColor(vec3 R, float roughness) {
    vec3 top = ToLinear(uSkyTop), hor = ToLinear(uSkyHorizon), gnd = ToLinear(uGroundColor);
    float t = R.y;
    float blur = 0.15 + roughness * 0.85;
    vec3 sky = mix(hor, top, smoothstep(0.0, 0.6 + blur * 0.4, t));
    vec3 below = mix(hor, gnd, smoothstep(0.0, 0.15 + blur * 0.3, -t));
    vec3 c = t >= 0.0 ? sky : below;
    return mix(c, mix(hor, mix(top, gnd, 0.5), 0.5), blur * 0.5) * uAmbient;
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

vec3 Tonemap(vec3 c) {
    c *= uExposure;
    c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);   // ACES (Narkowicz)
    return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
}
