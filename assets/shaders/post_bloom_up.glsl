// Bloom upsample: 3x3 tent filter, added onto the next larger mip (additive blending).
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
uniform vec2 uTexel;
layout(location = 0) out vec4 oColor;
void main() {
    vec2 t = uTexel;
    vec3 c = texture(uSource, vUV).rgb * 4.0;
    c += (texture(uSource, vUV + vec2(-t.x, 0)).rgb + texture(uSource, vUV + vec2(t.x, 0)).rgb +
          texture(uSource, vUV + vec2(0, -t.y)).rgb + texture(uSource, vUV + vec2(0, t.y)).rgb) * 2.0;
    c += texture(uSource, vUV + vec2(-t.x, -t.y)).rgb + texture(uSource, vUV + vec2(t.x, -t.y)).rgb +
         texture(uSource, vUV + vec2(-t.x, t.y)).rgb + texture(uSource, vUV + vec2(t.x, t.y)).rgb;
    oColor = vec4(c / 16.0, 1.0);
}
