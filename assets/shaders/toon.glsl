// Toon / cel lighting (included by lit.glsl when compiled with TOON).
// Requires lighting.glsl to be included first (light uniforms, shadows, ambient).
//
// Instead of a smooth N.L gradient, each light's contribution is snapped into a few flat
// bands. The darkest band is tinted with uToonShadowColor (a cool purple by default) rather
// than going grey, and cast shadows are folded into the light term *before* banding, so
// they read as the same flat shapes as the form shading.

uniform float uToonBands;        // number of light steps, >= 2
uniform float uToonSoftness;     // width of the blend at each band edge (0 = hard)
uniform vec3  uToonShadowColor;  // sRGB tint for the darkest band
uniform float uToonRim;          // strength of the rim highlight

// Snaps x in [0,1] to one of `bands` evenly spaced levels with a small smooth transition.
float ToonBand(float x) {
    float n = max(uToonBands, 2.0);
    float scaled = clamp(x, 0.0, 1.0) * n;
    float i = floor(scaled);
    float f = scaled - i;
    float w = max(uToonSoftness * n, 1e-4);
    i += smoothstep(1.0 - w, 1.0, f);   // ease into the next band only near its edge
    return clamp(i / (n - 1.0), 0.0, 1.0);
}

// A single hard-edged specular blob instead of a soft falloff.
float ToonSpec(vec3 N, vec3 L, vec3 V, float shininess) {
    float ndh = max(dot(N, normalize(L + V)), 0.0);
    float s = pow(ndh, shininess);
    float w = max(uToonSoftness, 0.005);
    return smoothstep(0.5 - w, 0.5 + w, s);
}

vec3 ToonLightTerm(vec3 albedo, vec3 shadowTint, float level) {
    // Lerp from the tinted shadow color to full albedo across the bands.
    return albedo * mix(shadowTint, vec3(1.0), level);
}

vec3 ComputeToonLighting(vec3 albedo, vec3 N, vec3 P, float specular, float shininess) {
    vec3 V = normalize(uCameraPos - P);
    vec3 shadowTint = ToLinear(uToonShadowColor);

    // Ambient is flattened too: one constant (mostly sky) value instead of a hemisphere gradient.
    vec3 color = albedo * AmbientLight(vec3(0.0, 0.5, 0.0));

    if (uHasSun != 0) {
        vec3 L = normalize(-uSunDir);
        float ndl = max(dot(N, L), 0.0);
        float light = ndl * ShadowFactor(P, N);
        float level = ToonBand(light);
        color += ToonLightTerm(albedo, shadowTint, level) * uSunColor;
        // Specular and rim only appear on the lit side.
        float litMask = step(0.001, level);
        color += ToonSpec(N, L, V, shininess) * specular * uSunColor * litMask;
        float rim = 1.0 - max(dot(N, V), 0.0);
        float rimMask = smoothstep(0.68 - uToonSoftness, 0.68 + uToonSoftness, rim * pow(ndl, 0.15));
        color += rimMask * uToonRim * uSunColor * albedo;
    }
    for (int i = 0; i < uPointCount; i++) {
        vec3 Lv = uPointPos[i] - P;
        float d = length(Lv);
        vec3 L = Lv / max(d, 1e-4);
        float x = d / uPointRange[i];
        float atten = pow(clamp(1.0 - x * x * x * x, 0.0, 1.0), 2.0) / (1.0 + d * d * 0.25);
        float level = ToonBand(max(dot(N, L), 0.0) * min(atten * 4.0, 1.0));
        // Bands give the shape; attenuation still controls overall brightness.
        color += albedo * level * uPointColor[i] * min(atten, 1.0);
        color += ToonSpec(N, L, V, shininess) * specular * uPointColor[i] * atten * step(0.001, level);
    }
    for (int i = 0; i < uSpotCount; i++) {
        vec3 Lv = uSpotPos[i] - P;
        float d = length(Lv);
        vec3 L = Lv / max(d, 1e-4);
        float x = d / uSpotRange[i];
        float atten = pow(clamp(1.0 - x * x * x * x, 0.0, 1.0), 2.0) / (1.0 + d * d * 0.25);
        float cone = step(uSpotCos[i], dot(-L, normalize(uSpotDir[i])));   // hard cone edge
        float level = ToonBand(max(dot(N, L), 0.0) * cone);
        color += albedo * level * uSpotColor[i] * min(atten, 1.0);
    }
    return color;
}
