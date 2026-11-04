// Final image: HDR scene x ambient occlusion + bloom -> exposure -> ACES filmic tone mapping
// -> color grading (white balance, contrast, saturation) -> vignette -> sRGB gamma.
// Alpha carries luma for FXAA.
#type vertex
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uAO;
uniform sampler2D uBloom;
uniform int uHasAO;
uniform int uHasBloom;
uniform float uAOStrength;
uniform float uBloomIntensity;
uniform float uExposure;
uniform float uContrast;
uniform float uSaturation;
uniform vec3 uWhiteBalance;   // per-channel gain from temperature/tint
uniform float uVignette;
layout(location = 0) out vec4 oColor;

// ACES filmic curve (Narkowicz's fit). Chosen over the full RRT/ODT fit because it keeps
// saturated colors saturated - important for cel-shaded characters - while still rolling
// highlights off like film.
vec3 ACESFilm(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
    vec3 c = texture(uScene, vUV).rgb;
    if (uHasAO != 0) c *= mix(1.0, texture(uAO, vUV).r, uAOStrength);
    c *= uExposure;
    if (uHasBloom != 0) c += texture(uBloom, vUV).rgb * uBloomIntensity;   // bloom is already exposed
    c *= uWhiteBalance;
    // Contrast in log space around middle grey keeps highlights from clipping early.
    c = exp2((log2(max(c, vec3(1e-5))) - log2(0.18)) * uContrast + log2(0.18));
    c = ACESFilm(c);
    c = mix(vec3(Luma(c)), c, uSaturation);
    vec2 v = vUV - 0.5;
    c *= 1.0 - uVignette * smoothstep(0.3, 0.9, dot(v, v) * 2.0);
    c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
    oColor = vec4(c, Luma(c));
}
