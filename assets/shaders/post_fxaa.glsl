// FXAA (after Timothy Lottes' FXAA 3.11, "quality" preset reduced to the common 12-step edge
// search). Reads luma from alpha, written by post_composite.glsl.
#type vertex
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
in vec2 vUV;
uniform sampler2D uImage;
uniform vec2 uTexel;
layout(location = 0) out vec4 oColor;

float L(vec2 uv) { return texture(uImage, uv).a; }

void main() {
    vec4 rgbaM = texture(uImage, vUV);
    float lumaM = rgbaM.a;
    float lumaN = L(vUV + vec2(0, uTexel.y)), lumaS = L(vUV - vec2(0, uTexel.y));
    float lumaE = L(vUV + vec2(uTexel.x, 0)), lumaW = L(vUV - vec2(uTexel.x, 0));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float range = lumaMax - lumaMin;
    if (range < max(0.0312, lumaMax * 0.125)) { oColor = vec4(rgbaM.rgb, 1.0); return; }

    float lumaNW = L(vUV + vec2(-uTexel.x, uTexel.y)), lumaNE = L(vUV + uTexel);
    float lumaSW = L(vUV - uTexel), lumaSE = L(vUV + vec2(uTexel.x, -uTexel.y));
    float edgeH = abs(lumaNW + lumaNE - 2.0 * lumaN) + 2.0 * abs(lumaW + lumaE - 2.0 * lumaM) + abs(lumaSW + lumaSE - 2.0 * lumaS);
    float edgeV = abs(lumaNW + lumaSW - 2.0 * lumaW) + 2.0 * abs(lumaN + lumaS - 2.0 * lumaM) + abs(lumaNE + lumaSE - 2.0 * lumaE);
    bool horizontal = edgeH >= edgeV;

    float luma1 = horizontal ? lumaS : lumaW, luma2 = horizontal ? lumaN : lumaE;
    float grad1 = abs(luma1 - lumaM), grad2 = abs(luma2 - lumaM);
    bool steep1 = grad1 >= grad2;
    float stepLen = horizontal ? uTexel.y : uTexel.x;
    float lumaLocal;
    if (steep1) { stepLen = -stepLen; lumaLocal = 0.5 * (luma1 + lumaM); }
    else lumaLocal = 0.5 * (luma2 + lumaM);
    float gradScaled = 0.25 * max(grad1, grad2);

    vec2 uv = vUV;
    if (horizontal) uv.y += stepLen * 0.5; else uv.x += stepLen * 0.5;
    vec2 off = horizontal ? vec2(uTexel.x, 0) : vec2(0, uTexel.y);
    vec2 uv1 = uv - off, uv2 = uv + off;
    float e1 = L(uv1) - lumaLocal, e2 = L(uv2) - lumaLocal;
    bool done1 = abs(e1) >= gradScaled, done2 = abs(e2) >= gradScaled;
    const float kSteps[10] = float[](1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 4.0, 8.0, 8.0, 8.0);
    for (int i = 0; i < 10 && !(done1 && done2); i++) {
        if (!done1) { uv1 -= off * kSteps[i]; e1 = L(uv1) - lumaLocal; done1 = abs(e1) >= gradScaled; }
        if (!done2) { uv2 += off * kSteps[i]; e2 = L(uv2) - lumaLocal; done2 = abs(e2) >= gradScaled; }
    }
    float d1 = horizontal ? vUV.x - uv1.x : vUV.y - uv1.y;
    float d2 = horizontal ? uv2.x - vUV.x : uv2.y - vUV.y;
    bool dir1 = d1 < d2;
    float dist = min(d1, d2);
    float edgeLen = d1 + d2;
    float pixelOffset = -dist / edgeLen + 0.5;
    bool centerSmaller = lumaM < lumaLocal;
    bool correct = ((dir1 ? e1 : e2) < 0.0) != centerSmaller;
    float finalOffset = correct ? pixelOffset : 0.0;
    // Sub-pixel aliasing
    float lumaAvg = (2.0 * (lumaN + lumaS + lumaE + lumaW) + lumaNW + lumaNE + lumaSW + lumaSE) / 12.0;
    float sub = clamp(abs(lumaAvg - lumaM) / range, 0.0, 1.0);
    sub = (-2.0 * sub + 3.0) * sub * sub;
    finalOffset = max(finalOffset, sub * sub * 0.75);
    vec2 finalUV = vUV;
    if (horizontal) finalUV.y += finalOffset * stepLen; else finalUV.x += finalOffset * stepLen;
    oColor = vec4(texture(uImage, finalUV).rgb, 1.0);
}
