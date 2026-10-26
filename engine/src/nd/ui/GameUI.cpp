#include "nd/ui/GameUI.h"
#include "nd/renderer/GL.h"
#include "nd/renderer/Renderer.h"
#include "nd/renderer/Shader.h"
#include "nd/renderer/Texture.h"
#include "nd/core/FileSystem.h"
#include "nd/core/Profiler.h"
#include "nd/core/Log.h"
#include <stb_truetype.h>
#include <cmath>

namespace nd::UI {

namespace {
struct UIVertex { vec2 pos; vec2 uv; vec4 color; };

struct DrawCmd { uint32_t texture; size_t first; size_t count; };

struct UIData {
    Ref<Texture2D> atlas;
    stbtt_packedchar chars[96];
    float bakedSize = 48.0f;
    float ascent = 0.0f;
    vec2 whiteUV{ 0.0f };
    int atlasW = 1024, atlasH = 512;
    uint32_t vao = 0, vbo = 0;
    size_t capacity = 0;
    std::vector<UIVertex> verts;
    std::vector<DrawCmd> cmds;
    float scale = 1.0f;          // pixels per reference unit
    int width = 1, height = 1;
    vec2 mouse{ -1.0f };
    bool mouseDown = false, mousePressed = false;
    bool overUI = false;
    mat4 viewProj{ 1.0f };
    Style style;
    bool ready = false;
};
UIData* s = nullptr;

void PushQuad(const vec2& p0, const vec2& p1, const vec2& uv0, const vec2& uv1, const vec4& c, uint32_t tex) {
    if (s->cmds.empty() || s->cmds.back().texture != tex) s->cmds.push_back({ tex, s->verts.size(), 0 });
    vec2 a = p0 * s->scale, b = p1 * s->scale;
    s->verts.push_back({ { a.x, a.y }, { uv0.x, uv0.y }, c });
    s->verts.push_back({ { b.x, a.y }, { uv1.x, uv0.y }, c });
    s->verts.push_back({ { b.x, b.y }, { uv1.x, uv1.y }, c });
    s->verts.push_back({ { a.x, a.y }, { uv0.x, uv0.y }, c });
    s->verts.push_back({ { b.x, b.y }, { uv1.x, uv1.y }, c });
    s->verts.push_back({ { a.x, b.y }, { uv0.x, uv1.y }, c });
    s->cmds.back().count += 6;
}
} // namespace

void Init() {
    s = new UIData();
    // Font atlas (white RGB, coverage in alpha) + a white texel for solid quads.
    auto font = FileSystem::ReadBinary(Project::EngineAssetsDir() / "fonts" / "Roboto-Medium.ttf");
    std::vector<uint8_t> alpha((size_t)s->atlasW * s->atlasH, 0);
    if (font) {
        stbtt_pack_context pc;
        stbtt_PackBegin(&pc, alpha.data(), s->atlasW, s->atlasH, 0, 1, nullptr);
        stbtt_PackSetOversampling(&pc, 2, 2);
        stbtt_PackFontRange(&pc, font->data(), 0, s->bakedSize, 32, 95, s->chars);
        stbtt_PackEnd(&pc);
        stbtt_fontinfo info;
        stbtt_InitFont(&info, font->data(), 0);
        int asc, desc, gap;
        stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
        s->ascent = asc * stbtt_ScaleForPixelHeight(&info, s->bakedSize);
    } else {
        ND_WARN("UI font missing - text will not render");
    }
    for (int y = s->atlasH - 4; y < s->atlasH; y++)
        for (int x = s->atlasW - 4; x < s->atlasW; x++) alpha[(size_t)y * s->atlasW + x] = 255;
    s->whiteUV = { (s->atlasW - 2) / (float)s->atlasW, (s->atlasH - 2) / (float)s->atlasH };
    std::vector<uint8_t> rgba((size_t)s->atlasW * s->atlasH * 4);
    for (size_t i = 0; i < alpha.size(); i++) { rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = alpha[i]; }
    s->atlas = Texture2D::FromPixels(s->atlasW, s->atlasH, 4, rgba.data(), { true, false, false });

    glGenVertexArrays(1, &s->vao);
    glGenBuffers(1, &s->vbo);
    glBindVertexArray(s->vao);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, color));
    glBindVertexArray(0);
    s->ready = true;
}

void Shutdown() {
    if (!s) return;
    glDeleteBuffers(1, &s->vbo);
    glDeleteVertexArrays(1, &s->vao);
    delete s;
    s = nullptr;
}

void BeginFrame(int w, int h, const vec2& mouse, bool down, bool pressed) {
    if (!s) Init();
    s->width = std::max(1, w);
    s->height = std::max(1, h);
    s->scale = s->height / 1080.0f;
    s->mouse = mouse / s->scale;
    s->mouseDown = down;
    s->mousePressed = pressed;
    s->overUI = false;
    s->verts.clear();
    s->cmds.clear();
}

void EndFrame() {
    if (!s || s->verts.empty()) return;
    ND_PROFILE_SCOPE("GameUI");
    auto shader = Renderer::GetShader("ui.glsl");
    shader->Bind();
    shader->SetMat4("uProj", glm::ortho(0.0f, (float)s->width, (float)s->height, 0.0f));
    shader->SetInt("uTexture", 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glBindVertexArray(s->vao);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    size_t bytes = s->verts.size() * sizeof(UIVertex);
    if (bytes > s->capacity) {
        s->capacity = bytes * 2;
        glBufferData(GL_ARRAY_BUFFER, s->capacity, nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, s->verts.data());
    for (auto& c : s->cmds) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, c.texture);
        glDrawArrays(GL_TRIANGLES, (GLint)c.first, (GLsizei)c.count);
        Profiler::AddDrawCall(c.count / 3);
    }
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

void SetCamera(const mat4& vp) { if (s) s->viewProj = vp; }
Style& GetStyle() { if (!s) Init(); return s->style; }
vec2 CanvasSize() { return s ? vec2(s->width / s->scale, 1080.0f) : vec2(1920, 1080); }
bool IsMouseOverUI() { return s && s->overUI; }

Rect Anchored(UIAnchor anchor, const vec2& off, const vec2& size) {
    vec2 c = CanvasSize();
    vec2 p;
    int col = (int)anchor % 3, row = (int)anchor / 3;
    p.x = col == 0 ? off.x : col == 1 ? (c.x - size.x) * 0.5f + off.x : c.x - size.x - off.x;
    p.y = row == 0 ? off.y : row == 1 ? (c.y - size.y) * 0.5f + off.y : c.y - size.y - off.y;
    return { p, size };
}

void Panel(const Rect& r, const vec4& color) {
    if (!s) return;
    PushQuad(r.pos, r.pos + r.size, s->whiteUV, s->whiteUV, color, s->atlas->Id());
}

void PanelBordered(const Rect& r, const vec4& color, const vec4& border, float t) {
    Panel(r, color);
    Panel({ r.pos, { r.size.x, t } }, border);
    Panel({ { r.pos.x, r.pos.y + r.size.y - t }, { r.size.x, t } }, border);
    Panel({ { r.pos.x, r.pos.y + t }, { t, r.size.y - 2 * t } }, border);
    Panel({ { r.pos.x + r.size.x - t, r.pos.y + t }, { t, r.size.y - 2 * t } }, border);
}

float MeasureText(const std::string& text, float size) {
    if (!s) return 0.0f;
    float k = size / s->bakedSize, w = 0.0f;
    for (char ch : text) {
        if (ch < 32 || ch > 126) continue;
        w += s->chars[ch - 32].xadvance * k;
    }
    return w;
}

void Text(const vec2& pos, const std::string& text, float size, const vec4& color, Align align) {
    if (!s) return;
    float k = size / s->bakedSize;
    float x = pos.x;
    if (align == Align::Center) x -= MeasureText(text, size) * 0.5f;
    else if (align == Align::Right) x -= MeasureText(text, size);
    float baseline = pos.y + s->ascent * k;
    float startX = x;
    for (char ch : text) {
        if (ch == '\n') { x = startX; baseline += size * 1.2f; continue; }
        if (ch < 32 || ch > 126) continue;
        const stbtt_packedchar& c = s->chars[ch - 32];
        vec2 p0(x + c.xoff * k, baseline + c.yoff * k), p1(x + c.xoff2 * k, baseline + c.yoff2 * k);
        vec2 uv0(c.x0 / (float)s->atlasW, c.y0 / (float)s->atlasH), uv1(c.x1 / (float)s->atlasW, c.y1 / (float)s->atlasH);
        // Texture rows are stored top-down; GL samples v from the bottom.
        PushQuad(p0, p1, uv0, uv1, color, s->atlas->Id());
        x += c.xadvance * k;
    }
}

void TextInRect(const Rect& r, const std::string& text, float size, const vec4& color, Align align) {
    float y = r.pos.y + (r.size.y - size) * 0.5f;
    float x = align == Align::Left ? r.pos.x + 10.0f : align == Align::Center ? r.pos.x + r.size.x * 0.5f : r.pos.x + r.size.x - 10.0f;
    Text({ x, y }, text, size, color, align);
}

bool Button(const std::string& label, const Rect& r, float textSize) {
    if (!s) return false;
    bool hover = r.Contains(s->mouse);
    if (hover) s->overUI = true;
    const Style& st = s->style;
    vec4 col = hover ? (s->mouseDown ? st.buttonPressed : st.buttonHover) : st.buttonColor;
    PanelBordered(r, col, st.borderColor, st.border);
    TextInRect(r, label, textSize, st.textColor, Align::Center);
    return hover && s->mousePressed;
}

void ProgressBar(const Rect& r, float value, const vec4& fill, const vec4& back) {
    if (!s) return;
    const Style& st = s->style;
    Panel(r, back.x < 0 ? st.barBack : back);
    float v = Clamp01(value);
    Panel({ r.pos + vec2(2.0f), { (r.size.x - 4.0f) * v, r.size.y - 4.0f } }, fill.x < 0 ? st.barFill : fill);
}

void Image(const Rect& r, const Ref<Texture2D>& tex, const vec4& tint) {
    if (!s || !tex) return;
    PushQuad(r.pos, r.pos + r.size, { 0, 1 }, { 1, 0 }, tint, tex->Id());
}

bool WorldToCanvas(const vec3& world, vec2& out) {
    if (!s) return false;
    vec4 clip = s->viewProj * vec4(world, 1.0f);
    if (clip.w <= 0.01f) return false;
    vec3 ndc = vec3(clip) / clip.w;
    if (ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f) return false;
    vec2 c = CanvasSize();
    out = { (ndc.x * 0.5f + 0.5f) * c.x, (1.0f - (ndc.y * 0.5f + 0.5f)) * c.y };
    return true;
}

void WorldProgressBar(const vec3& world, float value, const vec2& size, const vec4& fill) {
    vec2 p;
    if (!WorldToCanvas(world, p)) return;
    ProgressBar({ p - size * 0.5f, size }, value, fill);
}

void WorldText(const vec3& world, const std::string& text, float size, const vec4& color) {
    vec2 p;
    if (!WorldToCanvas(world, p)) return;
    Text(p - vec2(0, size * 0.5f), text, size, color, Align::Center);
}

} // namespace nd::UI
