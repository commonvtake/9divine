// 2D sprites and tiles: textured quads, tinted, with the entity id for editor picking.
// Pixel art is drawn as-is (no lighting, no tone mapping): the colors in the image are the
// colors on screen.
#type vertex
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in int aEntity;
uniform mat4 uViewProj;
out vec2 vUV;
out vec4 vColor;
flat out int vEntity;
void main() {
    vUV = aUV;
    vColor = aColor;
    vEntity = aEntity;
    gl_Position = uViewProj * vec4(aPos, 0.0, 1.0);
}

#type fragment
in vec2 vUV;
in vec4 vColor;
flat in int vEntity;
uniform sampler2D uTexture;
layout(location = 0) out vec4 oColor;
layout(location = 1) out int oID;
void main() {
    vec4 c = texture(uTexture, vUV) * vColor;
    if (c.a < 0.01) discard;    // fully transparent pixels don't block picking what's behind
    oColor = c;
    oID = vEntity;
}
