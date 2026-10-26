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

vec3 ComputeLighting(vec3 albedo, vec3 N, vec3 P, float specular, float shininess) {
    vec3 V = normalize(uCameraPos - P);
    vec3 color = albedo * AmbientLight(N);
    if (uHasSun != 0) {
        vec3 L = normalize(-uSunDir);
        float ndl = max(dot(N, L), 0.0);
        vec3 H = normalize(L + V);
        float spec = ndl > 0.0 ? pow(max(dot(N, H), 0.0), shininess) * specular : 0.0;
        color += (albedo * ndl + vec3(spec)) * uSunColor * ShadowFactor(P, N);
    }
    for (int i = 0; i < uPointCount; i++) {
        vec3 Lv = uPointPos[i] - P;
        float d = length(Lv);
        vec3 L = Lv / max(d, 1e-4);
        float x = d / uPointRange[i];
        float atten = pow(clamp(1.0 - x * x * x * x, 0.0, 1.0), 2.0) / (1.0 + d * d * 0.25);
        float ndl = max(dot(N, L), 0.0);
        vec3 H = normalize(L + V);
        float spec = ndl > 0.0 ? pow(max(dot(N, H), 0.0), shininess) * specular : 0.0;
        color += (albedo * ndl + vec3(spec)) * uPointColor[i] * atten;
    }
    for (int i = 0; i < uSpotCount; i++) {
        vec3 Lv = uSpotPos[i] - P;
        float d = length(Lv);
        vec3 L = Lv / max(d, 1e-4);
        float x = d / uSpotRange[i];
        float atten = pow(clamp(1.0 - x * x * x * x, 0.0, 1.0), 2.0) / (1.0 + d * d * 0.25);
        float cone = smoothstep(uSpotCos[i], uSpotCos[i] + 0.08, dot(-L, normalize(uSpotDir[i])));
        float ndl = max(dot(N, L), 0.0);
        color += albedo * ndl * uSpotColor[i] * atten * cone;
    }
    return color;
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
