// Bloom downsample: 13-tap filter (Jimenez, "Next Generation Post Processing in Call of Duty").
// The first pass also applies the brightness threshold (soft knee) and a Karis average so a
// single very bright pixel can't flicker into a huge glow.
#type vertex
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
in vec2 vUV;
uniform sampler2D uSource;
uniform vec2 uTexel;          // 1 / source size
uniform int uFirst;
uniform float uThreshold;
uniform float uExposure;
layout(location = 0) out vec4 oColor;

vec3 Prefilter(vec3 c) {
    c *= uExposure;
    float br = max(c.r, max(c.g, c.b));
    float knee = uThreshold * 0.5;
    float soft = clamp(br - uThreshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    return c * max(soft, br - uThreshold) / max(br, 1e-4);
}
float KarisWeight(vec3 c) { return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722))); }

void main() {
    vec2 t = uTexel;
    vec3 a = texture(uSource, vUV + t * vec2(-2, 2)).rgb, b = texture(uSource, vUV + t * vec2(0, 2)).rgb, c = texture(uSource, vUV + t * vec2(2, 2)).rgb;
    vec3 d = texture(uSource, vUV + t * vec2(-2, 0)).rgb, e = texture(uSource, vUV).rgb, f = texture(uSource, vUV + t * vec2(2, 0)).rgb;
    vec3 g = texture(uSource, vUV + t * vec2(-2, -2)).rgb, h = texture(uSource, vUV + t * vec2(0, -2)).rgb, i = texture(uSource, vUV + t * vec2(2, -2)).rgb;
    vec3 j = texture(uSource, vUV + t * vec2(-1, 1)).rgb, k = texture(uSource, vUV + t * vec2(1, 1)).rgb;
    vec3 l = texture(uSource, vUV + t * vec2(-1, -1)).rgb, m = texture(uSource, vUV + t * vec2(1, -1)).rgb;
    vec3 result;
    if (uFirst != 0) {
        vec3 g0 = Prefilter((a + b + d + e) * 0.25), g1 = Prefilter((b + c + e + f) * 0.25);
        vec3 g2 = Prefilter((d + e + g + h) * 0.25), g3 = Prefilter((e + f + h + i) * 0.25), g4 = Prefilter((j + k + l + m) * 0.25);
        float w0 = KarisWeight(g0), w1 = KarisWeight(g1), w2 = KarisWeight(g2), w3 = KarisWeight(g3), w4 = KarisWeight(g4);
        result = (g0 * w0 * 0.125 + g1 * w1 * 0.125 + g2 * w2 * 0.125 + g3 * w3 * 0.125 + g4 * w4 * 0.5) /
                 (w0 * 0.125 + w1 * 0.125 + w2 * 0.125 + w3 * 0.125 + w4 * 0.5);
    } else {
        result = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    }
    oColor = vec4(max(result, vec3(0.0)), 1.0);
}
