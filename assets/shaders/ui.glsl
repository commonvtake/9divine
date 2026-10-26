// 2D UI batch: panels, images and text (font atlas stores coverage in alpha).
#type vertex
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
#type fragment
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTexture;
layout(location = 0) out vec4 oColor;
void main() { oColor = texture(uTexture, vUV) * vColor; }
