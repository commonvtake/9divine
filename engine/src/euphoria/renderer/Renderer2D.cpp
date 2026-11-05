#include "euphoria/renderer/Renderer2D.h"
#include "euphoria/renderer/GL.h"
#include "euphoria/renderer/Shader.h"
#include "euphoria/renderer/Framebuffer.h"
#include "euphoria/renderer/Texture.h"
#include "euphoria/scene/Scene.h"
#include "euphoria/assets/AssetManager.h"
#include "euphoria/twod/Tilemap.h"
#include "euphoria/core/Application.h"
#include "euphoria/core/Profiler.h"

namespace eu {

namespace {

struct SpriteVertex {
    vec2 pos;
    vec2 uv;
    vec4 color;
    int32_t entity;
};

// Corners: 0 bottom-left, 1 bottom-right, 2 top-right, 3 top-left.
struct Quad {
    vec2 p[4];
    vec2 uv[4];
    vec4 color;
    Texture2D* tex;
    int order;
    float key;          // within an order: y-sorted things by -bottom, others first
    uint32_t seq;
    int32_t entity;
};

struct Data {
    uint32_t vao = 0, vbo = 0, ibo = 0;
    size_t vboQuads = 0, iboQuads = 0;
    Scope<Framebuffer> low;
    std::vector<Quad> quads;
    std::vector<SpriteVertex> verts;
    Renderer2D::Stats stats;
    Renderer2D::View lastGame;
};
Data* s = nullptr;

constexpr float kUnsortedKey = -1e30f;

void EnsureGL() {
    if (s) return;
    s = new Data();
    glGenVertexArrays(1, &s->vao);
    glGenBuffers(1, &s->vbo);
    glGenBuffers(1, &s->ibo);
    glBindVertexArray(s->vao);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 1, GL_INT, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, entity));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->ibo);
    glBindVertexArray(0);
}

void EnsureCapacity(size_t quads) {
    glBindVertexArray(s->vao);
    if (quads > s->vboQuads) {
        s->vboQuads = std::max<size_t>(quads + quads / 2, 1024);
        glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
        glBufferData(GL_ARRAY_BUFFER, s->vboQuads * 4 * sizeof(SpriteVertex), nullptr, GL_DYNAMIC_DRAW);
    }
    if (quads > s->iboQuads) {
        s->iboQuads = std::max<size_t>(quads + quads / 2, 1024);
        std::vector<uint32_t> idx(s->iboQuads * 6);
        for (size_t q = 0; q < s->iboQuads; q++) {
            uint32_t b = (uint32_t)q * 4;
            uint32_t* o = &idx[q * 6];
            o[0] = b; o[1] = b + 1; o[2] = b + 2; o[3] = b + 2; o[4] = b + 3; o[5] = b;
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_STATIC_DRAW);
    }
}

// UVs for a pixel rectangle (top-left origin) of a texture loaded bottom row first.
void RegionUVs(const Texture2D& t, const ivec4& r, vec2 uv[4]) {
    const float eps = 0.01f;   // keep nearest sampling inside the cell at its very edge
    float W = (float)std::max(1, t.Width()), H = (float)std::max(1, t.Height());
    float u0 = (r.x + eps) / W, u1 = (r.x + r.z - eps) / W;
    float vt = 1.0f - (r.y + eps) / H, vb = 1.0f - (r.y + r.w - eps) / H;
    uv[0] = { u0, vb }; uv[1] = { u1, vb }; uv[2] = { u1, vt }; uv[3] = { u0, vt };
}

// Tiled's flip flags: diagonal (swap axes) first, then horizontal, then vertical.
void ApplyFlags(vec2 uv[4], uint32_t flags) {
    if (flags & kTileFlipD) std::swap(uv[2], uv[0]);
    if (flags & kTileFlipH) { std::swap(uv[3], uv[2]); std::swap(uv[0], uv[1]); }
    if (flags & kTileFlipV) { std::swap(uv[3], uv[0]); std::swap(uv[2], uv[1]); }
}

struct ViewRect { vec2 min, max; };

} // namespace

void Renderer2D::Init() { EnsureGL(); }

void Renderer2D::Shutdown() {
    if (!s) return;
    glDeleteVertexArrays(1, &s->vao);
    glDeleteBuffers(1, &s->vbo);
    glDeleteBuffers(1, &s->ibo);
    s->low.reset();
    delete s;
    s = nullptr;
}

Renderer2D::Stats Renderer2D::LastStats() { return s ? s->stats : Stats{}; }
Renderer2D::View Renderer2D::LastGameView() { return s ? s->lastGame : View{}; }

Renderer2D::View Renderer2D::ComputeView(const SceneEnvironment& env, const RenderCamera& cam, int W, int H, bool editor) {
    View v;
    v.camera = cam;
    v.lowWidth = W;
    v.lowHeight = H;
    W = std::max(W, 1);
    H = std::max(H, 1);
    vec3 pos = vec3(glm::inverse(cam.view)[3]);
    float n = cam.nearClip, f = cam.farClip;
    if (!(f > n)) { n = 0.01f; f = 1000.0f; }
    float ppu = std::max(1.0f, env.pixelsPerUnit);
    ivec2 ref = glm::max(env.referenceResolution, ivec2(16));
    bool perspective = cam.projection[3][3] == 0.0f;
    if (editor) return v;
    if (!env.pixelPerfect) {
        if (perspective) {   // 2D always draws flat: show the reference height, whatever the camera says
            float h = ref.y / ppu * 0.5f, w = h * W / H;
            v.camera.projection = glm::ortho(-w, w, -h, h, n, f);
            v.camera.view = glm::translate(mat4(1.0f), -pos);
        }
        return v;
    }
    int scale = std::max(1, (int)std::floor(std::min((float)W / ref.x, (float)H / ref.y)));
    int lw = (W + scale - 1) / scale, lh = (H + scale - 1) / scale;
    // Snap the view's corner to the art's pixel grid
    float left = std::round(pos.x * ppu - lw * 0.5f) / ppu, bottom = std::round(pos.y * ppu - lh * 0.5f) / ppu;
    v.camera.view = glm::translate(mat4(1.0f), -pos);
    v.camera.projection = glm::ortho(left - pos.x, left - pos.x + lw / ppu, bottom - pos.y, bottom - pos.y + lh / ppu, n, f);
    v.camera.position = pos;
    v.camera.nearClip = n;
    v.camera.farClip = f;
    v.lowWidth = lw;
    v.lowHeight = lh;
    v.scale = scale;
    v.lowRes = true;
    return v;
}

RenderCamera Renderer2D::Render(Scene& scene, const RenderCamera& cameraIn, Framebuffer& target, const RenderOptions& opt) {
    EU_PROFILE_SCOPE("Render2D");
    EnsureGL();
    const SceneEnvironment& env = scene.Environment();
    const int W = target.Width(), H = target.Height();
    View view = ComputeView(env, cameraIn, W, H, opt.editorMode);
    if (!opt.editorMode) s->lastGame = view;
    const RenderCamera& cam = view.camera;
    const mat4 viewProj = cam.projection * cam.view;
    const float ppu = std::max(1.0f, env.pixelsPerUnit);
    const float time = (float)Time::Elapsed();
    const bool snap = view.lowRes;

    // Visible area (world XY)
    ViewRect vr{ vec2(1e30f), vec2(-1e30f) };
    {
        mat4 inv = glm::inverse(viewProj);
        for (int i = 0; i < 4; i++) {
            vec4 p = inv * vec4(i & 1 ? 1.0f : -1.0f, i & 2 ? 1.0f : -1.0f, 0.0f, 1.0f);
            vec2 q = vec2(p) / p.w;
            vr.min = glm::min(vr.min, q);
            vr.max = glm::max(vr.max, q);
        }
    }
    auto onScreen = [&](const vec2 p[4]) {
        vec2 mn = glm::min(glm::min(p[0], p[1]), glm::min(p[2], p[3])), mx = glm::max(glm::max(p[0], p[1]), glm::max(p[2], p[3]));
        return mx.x >= vr.min.x && mn.x <= vr.max.x && mx.y >= vr.min.y && mn.y <= vr.max.y;
    };

    scene.CacheTransforms();
    Registry& reg = scene.Reg();
    auto& quads = s->quads;
    quads.clear();
    uint32_t seq = 0;

    // ---- Tilemaps ---------------------------------------------------------
    reg.View<TransformComponent, TilemapComponent>([&](EntityId id, TransformComponent&, TilemapComponent& tc) {
        if (!scene.CachedActive(id)) return;
        TilemapData* map = EnsureTilemapLoaded(tc);
        if (!map) return;
        TilemapRuntime& rt = *tc.runtime;
        const mat4& world = scene.CachedWorld(id);
        mat4 invWorld = glm::inverse(world);
        vec2 snapDelta(0.0f);
        if (snap) {
            vec2 o = vec2(world[3]);
            snapDelta = glm::round(o * ppu) / ppu - o;
        }
        auto toWorld = [&](const vec2& px) { return vec2(world * vec4(map->MapToLocal(px, ppu), 0.0f, 1.0f)) + snapDelta; };
        // View rect in map pixels
        vec2 pmin(1e30f), pmax(-1e30f);
        for (int i = 0; i < 4; i++) {
            vec2 w(i & 1 ? vr.max.x : vr.min.x, i & 2 ? vr.max.y : vr.min.y);
            vec2 px = map->LocalToMap(vec2(invWorld * vec4(w, 0.0f, 1.0f)), ppu);
            pmin = glm::min(pmin, px);
            pmax = glm::max(pmax, px);
        }
        vec2 camPx = map->LocalToMap(vec2(invWorld * vec4(cam.position, 1.0f)), ppu);
        int maxW = map->tileWidth, maxH = map->tileHeight;
        for (auto& ts : map->tilesets) {
            maxW = std::max(maxW, ts.tileWidth + std::abs(ts.tileOffset.x));
            maxH = std::max(maxH, ts.tileHeight + std::abs(ts.tileOffset.y));
            for (auto& [tid, t] : ts.tiles) { maxW = std::max(maxW, t.imageWidth); maxH = std::max(maxH, t.imageHeight); }
        }
        auto addTile = [&](uint32_t gid, const vec2& bottomLeftPx, float w, float h, float rotationDeg, const vec4& color, int order, bool ySort) {
            int local = 0;
            const Tileset* ts = map->FindTileset(gid, local);
            if (!ts) return;
            local = ts->Animated(local, time);
            std::string image;
            ivec4 src;
            if (!ts->Source(local, image, src)) return;
            Texture2D* tex = rt.Texture(image).get();
            if (!tex || tex->Width() == 0) return;
            if (w <= 0) w = (float)src.z;
            if (h <= 0) h = (float)src.w;
            if ((gid & kTileFlipD) && w != h) std::swap(w, h);
            vec2 c[4] = { { 0, 0 }, { w, 0 }, { w, -h }, { 0, -h } };   // map pixels, y down
            if (rotationDeg != 0.0f) {
                float r = glm::radians(rotationDeg), cs = std::cos(r), sn = std::sin(r);
                for (auto& p : c) p = { p.x * cs - p.y * sn, p.x * sn + p.y * cs };
            }
            Quad q;
            for (int i = 0; i < 4; i++) q.p[i] = toWorld(bottomLeftPx + c[i]);
            if (!onScreen(q.p)) return;
            RegionUVs(*tex, src, q.uv);
            ApplyFlags(q.uv, gid);
            q.color = color;
            q.tex = tex;
            q.order = order;
            q.key = ySort ? -std::min(std::min(q.p[0].y, q.p[1].y), std::min(q.p[2].y, q.p[3].y)) : kUnsortedKey;
            q.seq = seq++;
            q.entity = (int32_t)id;
            quads.push_back(q);
        };
        for (auto& l : map->layers) {
            if (!l.visible || l.opacity <= 0.0f) continue;
            vec4 color = tc.color * l.tint * vec4(1.0f, 1.0f, 1.0f, l.opacity);
            int order = tc.order + l.order;
            vec2 shift = l.offset + camPx * (vec2(1.0f) - l.parallax);
            if (l.type == TiledLayerType::Tiles) {
                int cx0 = (int)std::floor((pmin.x - shift.x - maxW) / map->tileWidth), cx1 = (int)std::ceil((pmax.x - shift.x) / map->tileWidth);
                int cy0 = (int)std::floor((pmin.y - shift.y) / map->tileHeight) - 1, cy1 = (int)std::ceil((pmax.y - shift.y + maxH) / map->tileHeight);
                cx0 = std::max(cx0, l.originX); cy0 = std::max(cy0, l.originY);
                cx1 = std::min(cx1, l.originX + l.width - 1); cy1 = std::min(cy1, l.originY + l.height - 1);
                for (int cy = cy0; cy <= cy1; cy++)
                    for (int cx = cx0; cx <= cx1; cx++) {
                        uint32_t g = l.At(cx, cy);
                        if (!(g & kTileGidMask)) continue;
                        int local = 0;
                        const Tileset* ts = map->FindTileset(g, local);
                        if (!ts) continue;
                        vec2 bl(cx * (float)map->tileWidth + ts->tileOffset.x, (cy + 1) * (float)map->tileHeight + ts->tileOffset.y);
                        addTile(g, bl + shift, 0.0f, 0.0f, 0.0f, color, order, l.ySort);
                    }
            } else if (l.type == TiledLayerType::Objects) {
                for (auto& o : l.objects) {
                    if (!o.visible || !(o.gid & kTileGidMask)) continue;
                    addTile(o.gid, vec2(o.x, o.y) + shift, o.width, o.height, o.rotation, color, order, l.ySort);
                }
            } else if (l.type == TiledLayerType::Image && !l.image.empty()) {
                Texture2D* tex = rt.Texture(l.image).get();
                if (!tex || tex->Width() == 0) continue;
                float w = (float)tex->Width(), h = (float)tex->Height();
                // Repeating images cover the view: copies from the first one left/above it.
                int kx0 = 0, kx1 = 0, ky0 = 0, ky1 = 0;
                if (l.repeatX) { kx0 = (int)std::floor((pmin.x - shift.x) / w); kx1 = (int)std::floor((pmax.x - shift.x) / w); }
                if (l.repeatY) { ky0 = (int)std::floor((pmin.y - shift.y) / h); ky1 = (int)std::floor((pmax.y - shift.y) / h); }
                if ((long long)(kx1 - kx0 + 1) * (ky1 - ky0 + 1) > 4096) continue;
                for (int ky = ky0; ky <= ky1; ky++)
                    for (int kx = kx0; kx <= kx1; kx++) {
                        Quad q;
                        vec2 o = shift + vec2(kx * w, ky * h);
                        vec2 c[4] = { { 0, h }, { w, h }, { w, 0 }, { 0, 0 } };
                        for (int i = 0; i < 4; i++) q.p[i] = toWorld(o + c[i]);
                        if (!onScreen(q.p)) continue;
                        RegionUVs(*tex, ivec4(0, 0, tex->Width(), tex->Height()), q.uv);
                        q.color = color;
                        q.tex = tex;
                        q.order = order;
                        q.key = kUnsortedKey;
                        q.seq = seq++;
                        q.entity = (int32_t)id;
                        quads.push_back(q);
                    }
            }
        }
    });

    // ---- Sprites ----------------------------------------------------------
    reg.View<TransformComponent, SpriteRendererComponent>([&](EntityId id, TransformComponent&, SpriteRendererComponent& sr) {
        if (!sr.visible || sr.texture.empty() || !scene.CachedActive(id)) return;
        Ref<Texture2D> texRef = AssetManager::GetTexture(sr.texture, true);
        Texture2D* tex = texRef.get();
        if (!tex || tex->Width() == 0) return;
        ivec4 region = sr.region.z > 0.0f && sr.region.w > 0.0f ? ivec4(sr.region) : ivec4(0, 0, tex->Width(), tex->Height());
        float sppu = sr.pixelsPerUnit > 0.0f ? sr.pixelsPerUnit : ppu;
        float w = region.z / sppu, h = region.w / sppu;
        float x0 = -sr.pivot.x * w, y0 = -sr.pivot.y * h;
        const mat4& world = scene.CachedWorld(id);
        vec2 snapDelta(0.0f);
        if (snap) {
            vec2 o = vec2(world * vec4(0, 0, 0, 1));
            snapDelta = glm::round(o * ppu) / ppu - o;
        }
        Quad q;
        vec2 local[4] = { { x0, y0 }, { x0 + w, y0 }, { x0 + w, y0 + h }, { x0, y0 + h } };
        for (int i = 0; i < 4; i++) q.p[i] = vec2(world * vec4(local[i], 0.0f, 1.0f)) + snapDelta;
        if (!onScreen(q.p)) return;
        RegionUVs(*tex, region, q.uv);
        if (sr.flipX) { std::swap(q.uv[0], q.uv[1]); std::swap(q.uv[3], q.uv[2]); }
        if (sr.flipY) { std::swap(q.uv[0], q.uv[3]); std::swap(q.uv[1], q.uv[2]); }
        q.color = sr.color;
        q.tex = tex;
        q.order = sr.order;
        q.key = sr.ySort ? -std::min(std::min(q.p[0].y, q.p[1].y), std::min(q.p[2].y, q.p[3].y)) : kUnsortedKey;
        q.seq = seq++;
        q.entity = (int32_t)id;
        quads.push_back(q);
    });

    std::sort(quads.begin(), quads.end(), [](const Quad& a, const Quad& b) {
        if (a.order != b.order) return a.order < b.order;
        if (a.key != b.key) return a.key < b.key;
        return a.seq < b.seq;
    });

    // ---- Draw ---------------------------------------------------------------
    Framebuffer* fb = &target;
    if (view.lowRes) {
        if (!s->low) {
            FramebufferSpec spec;
            spec.width = view.lowWidth;
            spec.height = view.lowHeight;
            spec.attachments = { FBFormat::RGBA8, FBFormat::R32I };
            s->low = MakeScope<Framebuffer>(spec);
        }
        s->low->Resize(view.lowWidth, view.lowHeight);
        fb = s->low.get();
    }
    // Clear the real target (letterbox, ids, depth for the debug overlay)
    target.Bind();
    {
        std::vector<GLenum> all;
        for (int i = 0; i < target.ColorCount(); i++) all.push_back(GL_COLOR_ATTACHMENT0 + i);
        glDrawBuffers((GLsizei)all.size(), all.data());
    }
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(env.background2D.r, env.background2D.g, env.background2D.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (target.ColorCount() > 1) target.ClearAttachmentInt(1, -1);
    if (fb != &target) {
        fb->Bind();
        GLenum two[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers(2, two);
        glClear(GL_COLOR_BUFFER_BIT);
        fb->ClearAttachmentInt(1, -1);
    }

    s->stats = {};
    if (!quads.empty()) {
        EnsureCapacity(quads.size());
        auto& verts = s->verts;
        verts.resize(quads.size() * 4);
        for (size_t i = 0; i < quads.size(); i++) {
            const Quad& q = quads[i];
            for (int k = 0; k < 4; k++) verts[i * 4 + k] = { q.p[k], q.uv[k], q.color, q.entity };
        }
        glBindVertexArray(s->vao);
        glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
        glBufferData(GL_ARRAY_BUFFER, s->vboQuads * 4 * sizeof(SpriteVertex), nullptr, GL_DYNAMIC_DRAW);   // orphan
        glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(SpriteVertex), verts.data());
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->ibo);

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        auto shader = Renderer::GetShader("sprite.glsl");
        shader->Bind();
        shader->SetMat4("uViewProj", viewProj);
        shader->SetInt("uTexture", 0);
        glActiveTexture(GL_TEXTURE0);
        size_t start = 0;
        while (start < quads.size()) {
            size_t end = start + 1;
            while (end < quads.size() && quads[end].tex == quads[start].tex) end++;
            quads[start].tex->Bind(0);
            glDrawElements(GL_TRIANGLES, (GLsizei)((end - start) * 6), GL_UNSIGNED_INT, (void*)(start * 6 * sizeof(uint32_t)));
            Profiler::AddDrawCall((end - start) * 2);
            s->stats.drawCalls++;
            start = end;
        }
        s->stats.quads = (int)quads.size();
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glBindVertexArray(0);
    }

    if (fb != &target) {
        // Scale the low-res image up by whole pixels, centered (the overhang is cropped).
        int sw = view.lowWidth * view.scale, sh = view.lowHeight * view.scale;
        int ox = (W - sw) / 2, oy = (H - sh) / 2;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fb->Id());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.Id());
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        GLenum only0 = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &only0);
        glBlitFramebuffer(0, 0, view.lowWidth, view.lowHeight, ox, oy, ox + sw, oy + sh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        if (target.ColorCount() > 1) {
            glReadBuffer(GL_COLOR_ATTACHMENT1);
            GLenum only1[2] = { GL_NONE, GL_COLOR_ATTACHMENT1 };
            glDrawBuffers(2, only1);
            glBlitFramebuffer(0, 0, view.lowWidth, view.lowHeight, ox, oy, ox + sw, oy + sh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
        target.Bind();
        std::vector<GLenum> all;
        for (int i = 0; i < target.ColorCount(); i++) all.push_back(GL_COLOR_ATTACHMENT0 + i);
        glDrawBuffers((GLsizei)all.size(), all.data());
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    target.Bind();
    return cam;
}

} // namespace eu
