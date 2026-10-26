#type vertex
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uViewProj;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
#type fragment
in vec4 vColor;
layout(location = 0) out vec4 oColor;
void main() { oColor = vColor; }
