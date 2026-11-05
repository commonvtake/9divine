// Depth-aware blur that removes SSAO noise without bleeding across silhouettes.
#type vertex
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
in vec2 vUV;
uniform sampler2D uAO;
uniform sampler2D uDepth;
uniform vec2 uTexel;          // 1 / AO texture size
uniform float uNear, uFar;
layout(location = 0) out vec4 oAO;
float LinearDepth(float d) { float z = d * 2.0 - 1.0; return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear)); }
void main() {
    float center = LinearDepth(texture(uDepth, vUV).r);
    float sum = 0.0, wsum = 0.0;
    for (int x = -2; x <= 2; x++)
        for (int y = -2; y <= 2; y++) {
            vec2 uv = vUV + vec2(x, y) * uTexel;
            float dz = abs(LinearDepth(texture(uDepth, uv).r) - center) / max(center, 1e-3);
            float w = exp(-dz * 40.0) * (1.0 - 0.12 * float(abs(x) + abs(y)));
            sum += texture(uAO, uv).r * w;
            wsum += w;
        }
    oAO = vec4(sum / max(wsum, 1e-4));
}
