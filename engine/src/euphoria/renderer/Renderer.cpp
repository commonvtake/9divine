#include "euphoria/renderer/Renderer.h"
#include "euphoria/renderer/GL.h"
#include "euphoria/renderer/Shader.h"
#include "euphoria/renderer/Framebuffer.h"
#include "euphoria/renderer/DebugDraw.h"
#include "euphoria/renderer/Mesh.h"
#include "euphoria/renderer/PostProcess.h"
#include "euphoria/renderer/RayTracer.h"
#include "euphoria/renderer/Renderer2D.h"
#include "euphoria/scene/Scene.h"
#include "euphoria/scene/Entity.h"
#include "euphoria/assets/AssetManager.h"
#include "euphoria/terrain/Terrain.h"
#include "euphoria/particles/Particles.h"
#include "euphoria/physics/Physics.h"
#include "euphoria/ai/Navigation.h"
#include "euphoria/core/Application.h"
#include "euphoria/core/Profiler.h"
#include "euphoria/core/Log.h"
#include <map>
#include <unordered_map>
#include <cmath>
#include <chrono>

namespace eu {

RenderSettings Renderer::s_Settings;
bool g_GLContextAlive = true;

namespace {

constexpr int kMaxCascades = 4;
constexpr int kShadowUnit = 5, kEnvUnit = 6;

struct LightData {
    bool hasSun = false;
    vec3 sunDir{ -0.4f, -1.0f, -0.3f };
    vec3 sunColor{ 0.0f };
    bool sunShadows = false;
    int pointCount = 0;
    vec3 pointPos[8], pointColor[8];
    float pointRange[8];
    int spotCount = 0;
    vec3 spotPos[4], spotDir[4], spotColor[4];
    float spotRange[4], spotCos[4];
    // Cascaded shadows
    bool shadows = false;
    int cascadeCount = 0;
    mat4 cascadeMats[kMaxCascades];
    float cascadeEnds[kMaxCascades] = {};
    float cascadeTexel[kMaxCascades] = {};
};

// Depth texture array with one layer per cascade, compared in hardware (sampler2DArrayShadow).
struct ShadowCascades {
    uint32_t fbo = 0, tex = 0;
    int size = 0;
    void Ensure(int s) {
        if (tex && s == size) return;
        Release();
        size = s;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, s, s, kMaxCascades, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[] = { 1, 1, 1, 1 };
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, tex, 0, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void BindLayer(int layer) const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, tex, 0, layer);
        glViewport(0, 0, size, size);
    }
    void Release() {
        if (tex && g_GLContextAlive) { glDeleteTextures(1, &tex); glDeleteFramebuffers(1, &fbo); }
        tex = fbo = 0;
        size = 0;
    }
};

// A cubemap of the surroundings, re-captured around the camera one face per frame (so a
// full refresh costs a sixth of a scene render per frame). Rendered into `back` while
// shaders sample `front`, then swapped - never reading and writing the same texture.
struct ReflectionProbe {
    uint32_t cube[2] = { 0, 0 };
    uint32_t fbo = 0, depthRb = 0;
    int size = 0, front = 0, nextFace = 0, mipCount = 1;
    vec3 capturePos{ 0.0f }, lastCameraPos{ 0.0f };
    uint32_t frame = 0;
    bool valid = false;           // front has a complete capture
    void Ensure(int s) {
        if (cube[0] && s == size) return;
        Release();
        size = s;
        mipCount = 1 + (int)std::floor(std::log2((float)s));
        glGenTextures(2, cube);
        for (uint32_t c : cube) {
            glBindTexture(GL_TEXTURE_CUBE_MAP, c);
            for (int f = 0; f < 6; f++)
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA16F, s, s, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        }
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        glGenRenderbuffers(1, &depthRb);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, s, s);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        valid = false;
        nextFace = 0;
    }
    void Release() {
        if (cube[0] && g_GLContextAlive) {
            glDeleteTextures(2, cube);
            glDeleteRenderbuffers(1, &depthRb);
            glDeleteFramebuffers(1, &fbo);
        }
        cube[0] = cube[1] = fbo = depthRb = 0;
        size = 0;
        valid = false;
    }
    uint32_t Front() const { return cube[front]; }
    uint32_t Back() const { return cube[1 - front]; }
    // Camera for cube face f (standard GL face orientation).
    static RenderCamera FaceCamera(const vec3& pos, int f) {
        static const vec3 dirs[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
        static const vec3 ups[6] = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 }, { 0, -1, 0 } };
        RenderCamera c;
        c.position = pos;
        c.view = glm::lookAt(pos, pos + dirs[f], ups[f]);
        c.nearClip = 0.1f;
        c.farClip = 600.0f;
        c.projection = glm::perspective(glm::radians(90.0f), 1.0f, c.nearClip, c.farClip);
        return c;
    }
};

// What each shadow cascade was last rendered with (they're refreshed at different rates).
struct CascadeState {
    mat4 mat{ 1.0f };
    vec3 center{ 0.0f };
    float radius = 0.0f, texel = 0.0f;
    bool valid = false;
};

struct RendererData {
    std::map<std::string, Ref<Shader>> shaders;
    CascadeState cascadeState[kMaxCascades];
    uint32_t shadowFrame = 0;
    vec3 shadowSunDir{ 0.0f };
    int shadowConfig = 0;
    bool cascadeUpdate[kMaxCascades] = {};
    // GPU timing: a ring of GL_TIME_ELAPSED queries read a few frames later, so the CPU
    // never waits for the GPU just to measure it.
    static constexpr int kQueries = 4;
    uint32_t timeQuery[kQueries] = {};
    bool queryPending[kQueries] = {};
    int queryIndex = 0;
    int activeQuery = -1;
    ShadowCascades cascades;
    ReflectionProbe probe;
    Scope<Framebuffer> hdr;           // RGBA16F scene color, R32I ids, depth
    uint32_t emptyVAO = 0;
    uint32_t lineVAO = 0, lineVBO = 0;
    size_t lineCapacity = 0;
    Ref<Mesh> waterGrid;
    vec3 skySH[9];
};
RendererData* s_Data = nullptr;

vec3 SRGBToLinear(const vec3& c) { return glm::pow(glm::max(c, vec3(0.0f)), vec3(2.2f)); }
vec3 Forward(const RenderCamera& c) { return -vec3(glm::inverse(c.view)[2]); }

struct Frustum {
    vec4 planes[6];
    explicit Frustum(const mat4& m) {
        mat4 t = glm::transpose(m);
        planes[0] = t[3] + t[0]; planes[1] = t[3] - t[0];
        planes[2] = t[3] + t[1]; planes[3] = t[3] - t[1];
        planes[4] = t[3] + t[2]; planes[5] = t[3] - t[2];
        for (auto& p : planes) p /= glm::length(vec3(p));
    }
    bool Visible(const AABB& b) const {
        vec3 c = b.Center(), e = b.Extents();
        for (auto& p : planes) {
            float r = e.x * std::abs(p.x) + e.y * std::abs(p.y) + e.z * std::abs(p.z);
            if (glm::dot(vec3(p), c) + p.w < -r) return false;
        }
        return true;
    }
};

AABB TransformAABB(const AABB& b, const mat4& m) {
    vec3 c = vec3(m * vec4(b.Center(), 1.0f));
    vec3 e = b.Extents();
    vec3 ne(0.0f);
    for (int i = 0; i < 3; i++) ne += glm::abs(vec3(m[i])) * e[i];
    AABB out;
    out.min = c - ne;
    out.max = c + ne;
    return out;
}

// ---------------------------------------------------------------- sky lighting
// The procedural sky's radiance in direction d (must match sky.glsl, minus the sun disc,
// which is lit directly).
vec3 SkyRadiance(const SceneEnvironment& env, const vec3& d) {
    vec3 hor = SRGBToLinear(env.skyHorizon), top = SRGBToLinear(env.skyTop), gnd = SRGBToLinear(env.groundColor);
    if (d.y >= 0.0f) return glm::mix(hor, top, std::pow(glm::clamp(d.y, 0.0f, 1.0f), 0.6f));
    return glm::mix(hor, gnd, glm::clamp(-d.y * 3.0f, 0.0f, 1.0f));
}

// Projects the sky onto 9 SH coefficients and pre-convolves them with the clamped cosine
// lobe (Ramamoorthi & Hanrahan), so the shader's evaluation is irradiance / PI: a uniform
// sky of radiance L gives exactly L, matching the previous hemisphere ambient's scale.
void ComputeSkySH(const SceneEnvironment& env, vec3 out[9]) {
    for (int i = 0; i < 9; i++) out[i] = vec3(0.0f);
    const int n = 512;
    const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
    for (int i = 0; i < n; i++) {                       // Fibonacci sphere: even coverage
        float y = 1.0f - (i + 0.5f) * 2.0f / n, r = std::sqrt(1.0f - y * y), phi = golden * i;
        vec3 d(std::cos(phi) * r, y, std::sin(phi) * r);
        vec3 L = SkyRadiance(env, d);
        float b[9] = { 0.282095f, 0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x,
                       1.092548f * d.x * d.y, 1.092548f * d.y * d.z, 0.315392f * (3.0f * d.z * d.z - 1.0f),
                       1.092548f * d.x * d.z, 0.546274f * (d.x * d.x - d.y * d.y) };
        for (int k = 0; k < 9; k++) out[k] += L * b[k];
    }
    const float w = 4.0f * 3.14159265f / n;
    const float band[9] = { 1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f };
    for (int k = 0; k < 9; k++) out[k] *= w * band[k];
}

// Everything that affects how a mesh renderer looks, for automatic batching. Two renderers
// with equal keys (and SameLook) can share one instanced draw call.
template <typename T> void HashMix(uint64_t& h, const T& v) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&v);
    for (size_t i = 0; i < sizeof(T); i++) { h ^= p[i]; h *= 1099511628211ull; }
}
uint64_t BatchKey(const MeshRendererComponent& m) {
    uint64_t h = 1469598103934665603ull;
    HashMix(h, m.model.get());
    HashMix(h, std::hash<std::string>()(m.texture));
    HashMix(h, m.useModelMaterials); HashMix(h, m.color); HashMix(h, m.tiling);
    HashMix(h, m.metallic); HashMix(h, m.roughness); HashMix(h, m.emissive); HashMix(h, m.normalStrength);
    HashMix(h, std::hash<std::string>()(m.normalMap)); HashMix(h, std::hash<std::string>()(m.metallicRoughnessMap));
    HashMix(h, std::hash<std::string>()(m.emissiveMap)); HashMix(h, m.worldUV);
    HashMix(h, m.toon);
    if (m.toon) {
        HashMix(h, m.toonBands); HashMix(h, m.toonSoftness); HashMix(h, m.toonShadowColor);
        HashMix(h, m.toonShadowFill); HashMix(h, m.toonRim);
    }
    return h;
}
bool SameLook(const MeshRendererComponent& a, const MeshRendererComponent& b) {
    bool same = a.model == b.model && a.texture == b.texture && a.useModelMaterials == b.useModelMaterials &&
                a.color == b.color && a.tiling == b.tiling && a.metallic == b.metallic && a.roughness == b.roughness &&
                a.normalMap == b.normalMap && a.normalStrength == b.normalStrength && a.metallicRoughnessMap == b.metallicRoughnessMap &&
                a.emissiveMap == b.emissiveMap && a.worldUV == b.worldUV &&
                a.emissive == b.emissive && a.toon == b.toon;
    if (same && a.toon)
        same = a.toonBands == b.toonBands && a.toonSoftness == b.toonSoftness && a.toonShadowColor == b.toonShadowColor &&
               a.toonShadowFill == b.toonShadowFill && a.toonRim == b.toonRim;
    return same;
}


// ---------------------------------------------------------------- per-frame data
struct DrawItem {
    EntityId id;
    mat4 world;
    MeshRendererComponent* mr;
    AnimatorComponent* anim;
    bool skinned;                 // animated pose available this frame
};
struct TerrainItem { EntityId id; mat4 world; TerrainComponent* t; };

struct Frame {
    Scene* scene;
    const SceneEnvironment* env;
    LightData L;
    std::vector<DrawItem> items;
    std::vector<TerrainItem> terrains;
    bool envMap = false;          // a reflection cubemap is bound on kEnvUnit
    float time = 0.0f;
};

// What a DrawWorld call includes. The main view gets everything; reflection faces skip the
// costly or view-dependent parts.
struct PassFlags {
    bool ids = true;              // target has an entity-ID attachment at location 1
    bool outlines = true, grass = true, water = true, particles = true;
    float minAngularSize = 0.0f;  // skip objects smaller than this (radius / distance)
};

void ApplyLights(Shader& s, const Frame& F, const RenderCamera& cam) {
    const LightData& L = F.L;
    const SceneEnvironment& env = *F.env;
    s.SetVec3("uCameraPos", cam.position);
    s.SetVec3("uCameraForward", Forward(cam));
    s.SetInt("uHasSun", L.hasSun ? 1 : 0);
    s.SetVec3("uSunDir", glm::normalize(L.sunDir));
    s.SetVec3("uSunColor", L.sunColor);
    s.SetVec3("uSkyTop", env.skyTop);
    s.SetVec3("uSkyHorizon", env.skyHorizon);
    s.SetVec3("uGroundColor", env.groundColor);
    s.SetFloat("uAmbient", env.ambientIntensity);
    s.SetVec3Array("uSkySH", s_Data->skySH, 9);
    s.SetInt("uHasSkySH", 1);
    s.SetInt("uPointCount", L.pointCount);
    if (L.pointCount > 0) {
        s.SetVec3Array("uPointPos", L.pointPos, L.pointCount);
        s.SetVec3Array("uPointColor", L.pointColor, L.pointCount);
        s.SetFloatArray("uPointRange", L.pointRange, L.pointCount);
    }
    s.SetInt("uSpotCount", L.spotCount);
    if (L.spotCount > 0) {
        s.SetVec3Array("uSpotPos", L.spotPos, L.spotCount);
        s.SetVec3Array("uSpotDir", L.spotDir, L.spotCount);
        s.SetVec3Array("uSpotColor", L.spotColor, L.spotCount);
        s.SetFloatArray("uSpotRange", L.spotRange, L.spotCount);
        s.SetFloatArray("uSpotCos", L.spotCos, L.spotCount);
    }
    s.SetInt("uHasShadows", L.shadows ? 1 : 0);
    s.SetInt("uShadowMap", kShadowUnit);
    if (L.shadows) {
        s.SetInt("uCascadeCount", L.cascadeCount);
        s.SetMat4Array("uCascadeMats", L.cascadeMats, L.cascadeCount);
        s.SetFloatArray("uCascadeEnds", L.cascadeEnds, L.cascadeCount);
        s.SetFloatArray("uCascadeTexel", L.cascadeTexel, L.cascadeCount);
        s.SetFloat("uShadowSoftness", Renderer::Settings().shadowSoftness);
    }
    s.SetInt("uEnvMap", kEnvUnit);
    s.SetInt("uHasEnvMap", F.envMap ? 1 : 0);
    s.SetFloat("uEnvMaxLod", (float)std::max(0, s_Data->probe.mipCount - 3));
    s.SetFloat("uReflectionIntensity", env.reflectionIntensity);
    s.SetInt("uFog", env.fog ? 1 : 0);
    s.SetVec3("uFogColor", env.fogColor);
    s.SetFloat("uFogDensity", env.fogDensity);
    s.SetFloat("uExposure", env.exposure);
}

void EnsureModel(MeshRendererComponent& mr) {
    if (!mr.model || mr.loadedMesh != mr.mesh || mr.loadedVersion != AssetManager::Version()) {
        mr.model = AssetManager::GetModel(mr.mesh);
        mr.loadedMesh = mr.mesh;
        mr.loadedVersion = AssetManager::Version();
    }
}

// ---------------------------------------------------------------- cascaded shadows
// Splits [near, shadowDistance] between cascades (mix of uniform and logarithmic splits),
// fits each slice of the view frustum with a bounding sphere - rotation-invariant, so the
// shadow doesn't swim when the camera turns - and snaps it to whole texels so it doesn't
// shimmer when the camera moves.
void SetupCascades(LightData& L, const RenderCamera& cam) {
    const auto& rs = Renderer::Settings();
    int count = glm::clamp(rs.shadowCascades, 1, kMaxCascades);
    float n = std::max(cam.nearClip, 0.05f), f = std::min(rs.shadowDistance, cam.farClip);
    mat4 invViewProj = glm::inverse(cam.projection * cam.view);
    auto ndcZ = [&](float d) {
        vec4 c = cam.projection * vec4(0, 0, -d, 1);
        return c.z / c.w;
    };
    vec3 dir = glm::normalize(L.sunDir);
    vec3 up = std::abs(dir.y) > 0.99f ? vec3(0, 0, 1) : vec3(0, 1, 0);
    // Any change to the sun or the settings refreshes every cascade at once.
    int config = count * 1000003 + rs.shadowMapSize * 31 + (int)(rs.shadowDistance * 10.0f);
    bool forceAll = config != s_Data->shadowConfig || glm::dot(dir, s_Data->shadowSunDir) < 0.99999f;
    s_Data->shadowConfig = config;
    s_Data->shadowSunDir = dir;
    uint32_t frame = s_Data->shadowFrame++;
    float prev = n;
    for (int i = 0; i < count; i++) {
        float t = (i + 1) / (float)count;
        float split = glm::mix(n + (f - n) * t, n * std::pow(f / n, t), 0.75f);
        vec3 corners[8];
        int k = 0;
        for (float z : { ndcZ(prev), ndcZ(split) })
            for (int c = 0; c < 4; c++) {
                vec4 p = invViewProj * vec4((c & 1) ? 1.0f : -1.0f, (c & 2) ? 1.0f : -1.0f, z, 1.0f);
                corners[k++] = vec3(p) / p.w;
            }
        vec3 center(0.0f);
        for (auto& c : corners) center += c;
        center /= 8.0f;
        float radius = 0.0f;
        for (auto& c : corners) radius = std::max(radius, glm::length(c - center));
        // Cascades refreshed less often get 10% slack so the camera can move a little meanwhile.
        if (i > 0) radius *= 1.1f;
        radius = std::ceil(radius * 16.0f) / 16.0f;
        prev = split;
        L.cascadeEnds[i] = split;

        // Refresh rate: cascade 0 every frame, 1 every other frame, 2 and 3 every 4th (offset
        // so at most two cascades render in any frame) - plus whenever the view has drifted
        // far enough that the stale one wouldn't cover it.
        CascadeState& st = s_Data->cascadeState[i];
        bool due = i == 0 || (i == 1 && frame % 2 == 0) || (i == 2 && frame % 4 == 1) || (i == 3 && frame % 4 == 3);
        bool drifted = !st.valid || glm::distance(st.center, center) > st.radius * 0.08f || std::abs(st.radius - radius) > radius * 0.05f;
        s_Data->cascadeUpdate[i] = forceAll || due || drifted;
        if (s_Data->cascadeUpdate[i]) {
            mat4 lightView = glm::lookAt(center - dir * (radius + 150.0f), center, up);
            float texel = 2.0f * radius / rs.shadowMapSize;
            vec4 lc = lightView * vec4(center, 1.0f);
            vec2 snapped = glm::floor(vec2(lc) / texel) * texel;
            lightView = glm::translate(mat4(1.0f), vec3(snapped - vec2(lc), 0.0f)) * lightView;
            mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 1.0f, 2.0f * radius + 300.0f);
            st.mat = lightProj * lightView;
            st.center = center;
            st.radius = radius;
            st.texel = texel;
            st.valid = true;
        }
        L.cascadeMats[i] = st.mat;
        L.cascadeTexel[i] = st.texel;
    }
    L.cascadeCount = count;
}

void ShadowPass(Frame& F) {
    EU_PROFILE_SCOPE("Shadows");
    auto& rs = Renderer::Settings();
    auto& C = s_Data->cascades;
    C.Ensure(rs.shadowMapSize);
    auto sh = Renderer::GetShader("shadow.glsl");
    auto shSkin = Renderer::GetShader("shadow.glsl", { "SKINNED" });
    auto shInst = Renderer::GetShader("shadow.glsl", { "INSTANCED" });

    // Static casters sharing a model are drawn as one instanced call per sub-mesh.
    std::unordered_map<const Model*, std::vector<const DrawItem*>> groups;
    for (auto& d : F.items)
        if (d.mr->castShadows && !d.skinned && !d.mr->model->IsSkinned()) groups[d.mr->model.get()].push_back(&d);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    std::vector<mat4> worlds;
    std::vector<int> ids;
    for (int c = 0; c < F.L.cascadeCount; c++) {
        if (!s_Data->cascadeUpdate[c]) continue;   // reusing last frame's depth for this one
        C.BindLayer(c);
        glClear(GL_DEPTH_BUFFER_BIT);
        const mat4& ls = F.L.cascadeMats[c];
        Frustum lightFrustum(ls);
        // Casters smaller than ~2 texels of this cascade can't produce a visible shadow
        float minRadius = c > 0 ? F.L.cascadeTexel[c] * 2.0f : 0.0f;
        for (auto& [model, group] : groups) {
            worlds.clear();
            ids.clear();
            for (auto* d : group) {
                AABB box = TransformAABB(model->bounds, d->world);
                if (glm::length(box.Extents()) < minRadius) continue;
                if (lightFrustum.Visible(box)) { worlds.push_back(d->world); ids.push_back((int)d->id); }
            }
            if (worlds.empty()) continue;
            if (worlds.size() >= 2 && rs.batching) {
                shInst->Bind();
                shInst->SetMat4("uLightSpace", ls);
                shInst->SetMat4("uModel", mat4(1.0f));
                for (auto& sm : model->meshes) sm.mesh->DrawBatch(worlds.data(), ids.data(), (int)worlds.size());
            } else {
                sh->Bind();
                sh->SetMat4("uLightSpace", ls);
                for (auto& w : worlds) {
                    sh->SetMat4("uModel", w);
                    for (auto& sm : model->meshes) sm.mesh->Draw();
                }
            }
        }
        for (auto& d : F.items) {
            if (!d.mr->castShadows || (!d.skinned && !d.mr->model->IsSkinned())) continue;   // static ones done above
            Shader& s = d.skinned ? *shSkin : *sh;
            s.Bind();
            s.SetMat4("uLightSpace", ls);
            s.SetMat4("uModel", d.world);
            if (d.skinned) {
                auto& m = d.anim->runtime->skinMatrices;
                s.SetMat4Array("uBones", m.data(), (int)std::min<size_t>(m.size(), 100));
            }
            for (auto& sm : d.mr->model->meshes) sm.mesh->Draw();
        }
        for (auto& t : F.terrains) {
            sh->Bind();
            sh->SetMat4("uLightSpace", ls);
            sh->SetMat4("uModel", t.world);
            t.t->runtime->mesh->Draw();
            shInst->Bind();
            shInst->SetMat4("uLightSpace", ls);
            shInst->SetMat4("uModel", t.world);
            for (auto& tb : t.t->runtime->trees) tb.mesh->DrawInstanced(tb.count);
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
}

// ---------------------------------------------------------------- world drawing
// Draws sky, meshes, terrain, water and particles for `cam` into the bound framebuffer.
// Used for the main view (into the HDR buffer) and for reflection cubemap faces.
void DrawWorld(Frame& F, const RenderCamera& cam, const PassFlags& flags, int viewportW, int viewportH) {
    const auto& rs = Renderer::Settings();
    Registry& reg = F.scene->Reg();
    const mat4 viewProj = cam.projection * cam.view;
    Frustum frustum(viewProj);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glActiveTexture(GL_TEXTURE0 + kShadowUnit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, s_Data->cascades.tex);
    glActiveTexture(GL_TEXTURE0 + kEnvUnit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, F.envMap ? s_Data->probe.Front() : 0);
    glActiveTexture(GL_TEXTURE0);
    // Only the main view has an entity-ID attachment (location 1); faces write color only.
    auto idMask = [&](bool on) { if (flags.ids) glColorMaski(1, on, on, on, on); };

    // Sky (no depth write, no id)
    {
        auto sky = Renderer::GetShader("sky.glsl");
        sky->Bind();
        ApplyLights(*sky, F, cam);
        sky->SetMat4("uInvViewProj", glm::inverse(viewProj));
        glDepthMask(GL_FALSE);
        idMask(false);
        glBindVertexArray(s_Data->emptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        idMask(true);
        glDepthMask(GL_TRUE);
    }

    // Opaque meshes
    std::vector<const DrawItem*> outlined;
    {
        EU_PROFILE_SCOPE("Opaque");
        auto white = Texture2D::White();
        auto flatNormal = Texture2D::FlatNormal();
        // Variants: [toon][plain|skinned|batched]
        Ref<Shader> variants[2][3] = {
            { Renderer::GetShader("lit.glsl"), Renderer::GetShader("lit.glsl", { "SKINNED" }), Renderer::GetShader("lit.glsl", { "BATCHED" }) },
            { Renderer::GetShader("lit.glsl", { "TOON" }), Renderer::GetShader("lit.glsl", { "SKINNED", "TOON" }),
              Renderer::GetShader("lit.glsl", { "BATCHED", "TOON" }) },
        };
        enum { kPlain = 0, kSkinned = 1, kBatched = 2 };
        Shader* bound = nullptr;
        auto bindVariant = [&](bool toon, int kind) -> Shader* {
            Shader* s = variants[toon ? 1 : 0][kind].get();
            if (s != bound) {
                s->Bind();
                ApplyLights(*s, F, cam);
                s->SetMat4("uViewProj", viewProj);
                s->SetInt("uAlbedoMap", 0);
                s->SetInt("uNormalMap", 1);
                s->SetInt("uMetalRoughMap", 2);
                s->SetInt("uOcclusionMap", 3);
                s->SetInt("uEmissiveMap", 4);
                bound = s;
            }
            return s;
        };
        auto setObjectUniforms = [&](Shader* s, const MeshRendererComponent& mr) {
            if (!mr.toon) return;
            s->SetFloat("uToonBands", (float)glm::clamp(mr.toonBands, 2, 8));
            s->SetFloat("uToonSoftness", glm::clamp(mr.toonSoftness, 0.0f, 0.5f));
            s->SetVec3("uToonShadowColor", mr.toonShadowColor);
            s->SetFloat("uToonShadowFill", glm::clamp(mr.toonShadowFill, 0.0f, 1.0f));
            s->SetFloat("uToonRim", mr.toonRim);
        };
        // Texture overrides set on the renderer (they win over the model's own maps).
        struct Overrides { Ref<Texture2D> albedo, normal, metalRough, emissive; };
        auto overridesOf = [](const MeshRendererComponent& mr) {
            Overrides o;
            if (!mr.emissiveMap.empty()) o.emissive = AssetManager::GetTexture(mr.emissiveMap);
            if (!mr.texture.empty()) o.albedo = AssetManager::GetTexture(mr.texture);
            if (!mr.normalMap.empty()) o.normal = AssetManager::GetTexture(mr.normalMap);
            if (!mr.metallicRoughnessMap.empty()) o.metalRough = AssetManager::GetTexture(mr.metallicRoughnessMap);
            return o;
        };
        // Sets one sub-mesh's material; returns whether it is double sided. With model
        // materials on, the model's PBR factors are used; otherwise the renderer's.
        auto setMaterial = [&](Shader* s, const MeshRendererComponent& mr, const SubMesh& sm, const Overrides& o) {
            const MaterialData* mat = (mr.useModelMaterials && sm.material >= 0 && sm.material < (int)mr.model->materials.size())
                                          ? &mr.model->materials[sm.material] : nullptr;
            s->SetVec4("uColor", mr.color * (mat ? mat->baseColor : vec4(1.0f)));
            s->SetFloat("uTiling", mr.tiling);
            s->SetFloat("uMetallic", mat ? mat->metallic : mr.metallic);
            s->SetFloat("uRoughness", mat ? mat->roughness : mr.roughness);
            s->SetVec3("uEmissive", mr.emissive + (mat ? mat->emissive : vec3(0.0f)));
            Ref<Texture2D> normal = o.normal ? o.normal : (mat ? mat->normalMap : nullptr);
            s->SetFloat("uNormalScale", normal ? (o.normal ? mr.normalStrength : mat->normalScale * mr.normalStrength) : 0.0f);
            (o.albedo ? o.albedo : (mat && mat->albedo ? mat->albedo : white))->Bind(0);
            (normal ? normal : flatNormal)->Bind(1);
            (o.metalRough ? o.metalRough : (mat && mat->metallicRoughnessMap ? mat->metallicRoughnessMap : white))->Bind(2);
            (mat && mat->occlusionMap ? mat->occlusionMap : white)->Bind(3);
            (o.emissive ? o.emissive : (mat && mat->emissiveMap ? mat->emissiveMap : white))->Bind(4);
            s->SetInt("uWorldUV", mr.worldUV ? 1 : 0);
            return mat && mat->doubleSided;
        };

        // Group visible, non-skinned items that look identical (same model, same material
        // settings) so each group becomes a single instanced draw per sub-mesh.
        std::vector<const DrawItem*> singles;
        std::unordered_map<uint64_t, std::vector<std::vector<const DrawItem*>>> groups;
        for (auto& d : F.items) {
            if (!d.skinned && !d.mr->model->IsSkinned()) {
                AABB box = TransformAABB(d.mr->model->bounds, d.world);
                if (!frustum.Visible(box)) continue;
                if (flags.minAngularSize > 0.0f) {
                    float radius = glm::length(box.Extents());
                    float dist = std::max(glm::distance(box.Center(), cam.position), 1e-3f);
                    if (radius / dist < flags.minAngularSize) continue;
                }
            }
            if (d.mr->outline && flags.outlines) outlined.push_back(&d);
            if (d.skinned || d.mr->model->IsSkinned() || !rs.batching) { singles.push_back(&d); continue; }
            auto& buckets = groups[BatchKey(*d.mr)];
            bool placed = false;
            for (auto& b : buckets)
                if (SameLook(*b.front()->mr, *d.mr)) { b.push_back(&d); placed = true; break; }   // hash collision guard
            if (!placed) buckets.push_back({ &d });
        }

        std::vector<mat4> batchWorlds;
        std::vector<int> batchIds;
        for (auto& [key, buckets] : groups) {
            for (auto& b : buckets) {
                if (b.size() < 2) { singles.push_back(b.front()); continue; }
                const MeshRendererComponent& mr = *b.front()->mr;
                batchWorlds.clear();
                batchIds.clear();
                for (auto* d : b) { batchWorlds.push_back(d->world); batchIds.push_back((int)d->id); }
                Shader* s = bindVariant(mr.toon, kBatched);
                setObjectUniforms(s, mr);
                s->SetMat4("uModel", mat4(1.0f));
                Overrides ov = overridesOf(mr);
                for (auto& sm : mr.model->meshes) {
                    bool ds = setMaterial(s, mr, sm, ov);
                    if (ds) glDisable(GL_CULL_FACE);
                    sm.mesh->DrawBatch(batchWorlds.data(), batchIds.data(), (int)b.size());
                    if (ds) glEnable(GL_CULL_FACE);
                }
                if (flags.ids) Profiler::Stats().batchedObjects += (uint32_t)b.size();
            }
        }

        for (const DrawItem* dp : singles) {
            const DrawItem& d = *dp;
            Shader* s = bindVariant(d.mr->toon, d.skinned ? kSkinned : kPlain);
            setObjectUniforms(s, *d.mr);
            s->SetMat4("uModel", d.world);
            s->SetInt("uEntityID", (int)d.id);
            if (d.skinned) {
                auto& m = d.anim->runtime->skinMatrices;
                s->SetMat4Array("uBones", m.data(), (int)std::min<size_t>(m.size(), 100));
            }
            Overrides ov = overridesOf(*d.mr);
            for (auto& sm : d.mr->model->meshes) {
                bool ds = setMaterial(s, *d.mr, sm, ov);
                if (ds) glDisable(GL_CULL_FACE);
                sm.mesh->Draw();
                if (ds) glEnable(GL_CULL_FACE);
            }
        }
    }

    // Ink outlines (inverted hull): redraw outlined meshes inflated along their outline
    // normals with front faces culled, so only the shell's back faces show around the edges.
    if (!outlined.empty()) {
        EU_PROFILE_SCOPE("Outlines");
        auto os = Renderer::GetShader("outline.glsl");
        auto osSkin = Renderer::GetShader("outline.glsl", { "SKINNED" });
        vec2 viewport((float)viewportW, (float)viewportH);
        glCullFace(GL_FRONT);
        Shader* bound = nullptr;
        for (const DrawItem* d : outlined) {
            Shader* s = d->skinned ? osSkin.get() : os.get();
            if (s != bound) {
                s->Bind();
                ApplyLights(*s, F, cam);   // camera position + fog for distance fading
                s->SetMat4("uViewProj", viewProj);
                s->SetVec2("uViewportSize", viewport);
                bound = s;
            }
            s->SetMat4("uModel", d->world);
            s->SetInt("uEntityID", (int)d->id);
            s->SetFloat("uOutlineWidth", glm::max(d->mr->outlineWidth, 0.0f));
            s->SetVec4("uOutlineColor", d->mr->outlineColor);
            if (d->skinned) {
                auto& m = d->anim->runtime->skinMatrices;
                s->SetMat4Array("uBones", m.data(), (int)std::min<size_t>(m.size(), 100));
            }
            for (auto& sm : d->mr->model->meshes) sm.mesh->Draw();
        }
        glCullFace(GL_BACK);
    }

    // Terrain + foliage
    if (!F.terrains.empty()) {
        EU_PROFILE_SCOPE("Terrain");
        auto ts = Renderer::GetShader("terrain.glsl");
        auto treeShader = Renderer::GetShader("lit.glsl", { "INSTANCED" });
        auto grassShader = Renderer::GetShader("grass.glsl");
        auto white = Texture2D::White();
        for (auto& t : F.terrains) {
            TerrainComponent& tc = *t.t;
            ts->Bind();
            ApplyLights(*ts, F, cam);
            ts->SetMat4("uViewProj", viewProj);
            ts->SetMat4("uModel", t.world);
            ts->SetVec3("uSand", tc.sandColor);
            ts->SetVec3("uGrass", tc.grassColor);
            ts->SetVec3("uRock", tc.rockColor);
            ts->SetVec3("uSnow", tc.snowColor);
            ts->SetFloat("uSandHeight", tc.sandHeight);
            ts->SetFloat("uSnowHeight", tc.snowHeight);
            ts->SetInt("uEntityID", (int)t.id);
            tc.runtime->mesh->Draw();

            if (!tc.runtime->trees.empty()) {
                treeShader->Bind();
                ApplyLights(*treeShader, F, cam);
                treeShader->SetMat4("uViewProj", viewProj);
                treeShader->SetMat4("uModel", t.world);
                treeShader->SetInt("uAlbedoMap", 0);
                treeShader->SetInt("uEntityID", (int)t.id);
                treeShader->SetFloat("uTiling", 1.0f);
                treeShader->SetFloat("uMetallic", 0.0f);
                treeShader->SetFloat("uRoughness", 0.8f);
                treeShader->SetFloat("uNormalScale", 0.0f);
                treeShader->SetInt("uNormalMap", 1);
                treeShader->SetInt("uMetalRoughMap", 2);
                treeShader->SetInt("uOcclusionMap", 3);
                treeShader->SetInt("uEmissiveMap", 4);
                treeShader->SetInt("uWorldUV", 0);
                Texture2D::FlatNormal()->Bind(1);
                white->Bind(2);
                white->Bind(3);
                white->Bind(4);
                treeShader->SetVec3("uEmissive", vec3(0.0f));
                for (auto& tb : tc.runtime->trees) {
                    treeShader->SetVec4("uColor", tb.color);
                    (tb.texture ? tb.texture : white)->Bind(0);
                    tb.mesh->DrawInstanced(tb.count);
                }
            }
            if (flags.grass && rs.grass && tc.grass && tc.runtime->grassCount > 0) {
                glDisable(GL_CULL_FACE);
                idMask(false);
                grassShader->Bind();
                ApplyLights(*grassShader, F, cam);
                grassShader->SetMat4("uViewProj", viewProj);
                grassShader->SetMat4("uModel", t.world);
                grassShader->SetFloat("uTime", F.time);
                grassShader->SetVec3("uCameraPosV", cam.position);
                grassShader->SetFloat("uMaxDistance", rs.grassDistance);
                grassShader->SetVec3("uGrassColor", tc.grassColor);
                tc.runtime->grassMesh->DrawInstanced(tc.runtime->grassCount);
                idMask(true);
                glEnable(GL_CULL_FACE);
            }
        }
    }

    // ---- Transparent: water ----------------------------------------------
    idMask(false);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (flags.water) {
        auto ws = Renderer::GetShader("water.glsl");
        bool bound = false;
        reg.View<TransformComponent, WaterComponent>([&](EntityId id, TransformComponent&, WaterComponent& w) {
            if (!F.scene->CachedActive(id)) return;
            if (!bound) {
                ws->Bind();
                ApplyLights(*ws, F, cam);
                ws->SetMat4("uViewProj", viewProj);
                ws->SetFloat("uTime", F.time);
                bound = true;
            }
            mat4 world = F.scene->CachedWorld(id) * glm::scale(mat4(1.0f), vec3(w.size.x, 1.0f, w.size.y));
            ws->SetMat4("uModel", world);
            ws->SetVec4("uShallow", w.shallowColor);
            ws->SetVec4("uDeep", w.deepColor);
            ws->SetFloat("uWaveHeight", w.waveHeight);
            ws->SetFloat("uWaveScale", w.waveScale);
            ws->SetFloat("uWaveSpeed", w.waveSpeed);
            glDisable(GL_CULL_FACE);
            s_Data->waterGrid->Draw();
            glEnable(GL_CULL_FACE);
        });
    }

    // ---- Particles --------------------------------------------------------
    if (flags.particles) {
        EU_PROFILE_SCOPE("Particles");
        struct PE { ParticleEmitterComponent* e; mat4 world; float dist; };
        std::vector<PE> emitters;
        uint32_t total = 0;
        reg.View<TransformComponent, ParticleEmitterComponent>([&](EntityId id, TransformComponent&, ParticleEmitterComponent& p) {
            if (!p.runtime || p.runtime->particles.empty() || !F.scene->CachedActive(id)) return;
            const mat4& w = F.scene->CachedWorld(id);
            emitters.push_back({ &p, w, glm::distance(vec3(w[3]), cam.position) });
            total += (uint32_t)p.runtime->particles.size();
        });
        Profiler::Stats().particles = total;
        if (!emitters.empty()) {
            std::sort(emitters.begin(), emitters.end(), [](const PE& a, const PE& b) { return a.dist > b.dist; });
            auto ps = Renderer::GetShader("particle.glsl");
            ps->Bind();
            ApplyLights(*ps, F, cam);
            ps->SetMat4("uViewProj", viewProj);
            mat4 inv = glm::inverse(cam.view);
            ps->SetVec3("uCamRight", vec3(inv[0]));
            ps->SetVec3("uCamUp", vec3(inv[1]));
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            for (auto& pe : emitters) {
                int n = pe.e->runtime->PrepareDraw(*pe.e, pe.world, cam.position);
                if (pe.e->additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                pe.e->runtime->quad->DrawInstanced(n);
            }
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }
    glDisable(GL_BLEND);
    idMask(true);
    glBindVertexArray(0);
}

// Re-captures the reflection cubemap around `cam`: one face per frame normally, all six when
// there is no valid capture yet (first frame, or after a teleport). Water is included, so the
// lake reflects in glossy surfaces too.
void UpdateReflectionProbe(Frame& F, const RenderCamera& cam) {
    EU_PROFILE_SCOPE("Reflections");
    auto& P = s_Data->probe;
    P.Ensure(Renderer::Settings().reflectionSize);
    P.frame++;
    if (P.nextFace == 0) P.capturePos = cam.position;
    // After a teleport or scene switch, restart the cycle from the new spot but keep showing
    // the old reflections until it completes (no 6-face burst, no frame without reflections).
    if (P.valid && glm::distance(P.capturePos, cam.position) > 25.0f) { P.nextFace = 0; P.capturePos = cam.position; }
    // Budget: the very first capture takes 2 faces a frame (valid after 3 frames); after that
    // one face a frame while the camera moves, and one every 3rd frame while it stays put.
    bool still = glm::distance(P.lastCameraPos, cam.position) < 0.05f;
    P.lastCameraPos = cam.position;
    int faces = !P.valid ? 2 : (still && P.frame % 3 != 0 ? 0 : 1);
    if (faces == 0) return;
    PassFlags flags;
    flags.ids = false;
    flags.outlines = false;
    flags.grass = false;
    flags.particles = false;
    flags.minAngularSize = 0.02f;   // a 128 px face can't show anything smaller
    glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
    glViewport(0, 0, P.size, P.size);
    vec3 clear = SRGBToLinear(F.env->skyHorizon);
    for (int i = 0; i < faces; i++) {
        int f = P.nextFace;
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, P.Back(), 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glClearColor(clear.r, clear.g, clear.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        DrawWorld(F, ReflectionProbe::FaceCamera(P.capturePos, f), flags, P.size, P.size);
        if (++P.nextFace == 6) {
            P.nextFace = 0;
            glBindTexture(GL_TEXTURE_CUBE_MAP, P.Back());
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
            P.front = 1 - P.front;
            P.valid = true;
            F.envMap = true;
        }
    }
}

} // namespace

// --------------------------------------------------------------------------
void Renderer::Init() {
    s_Data = new RendererData();
    glGenVertexArrays(1, &s_Data->emptyVAO);
    glGenVertexArrays(1, &s_Data->lineVAO);
    glGenBuffers(1, &s_Data->lineVBO);
    glBindVertexArray(s_Data->lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, s_Data->lineVBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DebugDraw::LineVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(DebugDraw::LineVertex), (void*)sizeof(vec3));
    glBindVertexArray(0);
    s_Data->waterGrid = Primitives::Plane(1.0f, 128);
    glGenQueries(RendererData::kQueries, s_Data->timeQuery);
    PostProcess::Init();
    RayTracer::Init();
    Prewarm();
}

// Compiles every shader variant the renderer can use, at startup. Otherwise each one compiles
// the first time something needs it - the first outlined character on screen, the first time
// two identical crates are batched, the first skinned toon model - and that frame stalls.
void Renderer::Prewarm() {
    auto start = std::chrono::steady_clock::now();
    for (bool toon : { false, true })
        for (const char* kind : { "", "SKINNED", "BATCHED" }) {
            std::vector<std::string> defs;
            if (*kind) defs.push_back(kind);
            if (toon) defs.push_back("TOON");
            GetShader("lit.glsl", defs);
        }
    GetShader("lit.glsl", { "INSTANCED" });
    GetShader("sprite.glsl");
    for (const char* v : { "", "SKINNED", "INSTANCED" }) GetShader("shadow.glsl", *v ? std::vector<std::string>{ v } : std::vector<std::string>{});
    GetShader("outline.glsl");
    GetShader("outline.glsl", { "SKINNED" });
    for (const char* f : { "sky.glsl", "terrain.glsl", "grass.glsl", "water.glsl", "particle.glsl", "line.glsl", "post_ssao.glsl",
                           "post_ao_blur.glsl", "post_bloom_down.glsl", "post_bloom_up.glsl", "post_composite.glsl", "post_fxaa.glsl" })
        GetShader(f);
    if (s_Settings.rayTraced) GetShader("pathtrace.glsl");
    float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    EU_INFO("Shaders compiled in {} ms", (int)ms);
}

void Renderer::Shutdown() {
    if (!s_Data) return;
    Renderer2D::Shutdown();
    RayTracer::Shutdown();
    PostProcess::Shutdown();
    s_Data->cascades.Release();
    s_Data->probe.Release();
    s_Data->hdr.reset();
    glDeleteQueries(RendererData::kQueries, s_Data->timeQuery);
    glDeleteVertexArrays(1, &s_Data->emptyVAO);
    glDeleteVertexArrays(1, &s_Data->lineVAO);
    glDeleteBuffers(1, &s_Data->lineVBO);
    delete s_Data;
    s_Data = nullptr;
}

Ref<Shader> Renderer::GetShader(const std::string& file, const std::vector<std::string>& defines) {
    std::string key = file;
    for (auto& d : defines) key += "|" + d;
    auto it = s_Data->shaders.find(key);
    if (it != s_Data->shaders.end()) return it->second;
    auto shader = Shader::FromFile(file, defines);
    s_Data->shaders[key] = shader;
    return shader;
}

void Renderer::ReloadShaders() {
    s_Data->shaders.clear();
    Prewarm();
    EU_INFO("Shaders reloaded");
}

RenderCamera Renderer::MakeCamera(const mat4& world, float fov, float aspect, float n, float f, bool ortho, float orthoSize) {
    RenderCamera c;
    vec3 t, s; quat r;
    DecomposeTRS(world, t, r, s);
    c.position = t;
    c.view = glm::inverse(ComposeTRS(t, r, vec3(1.0f)));
    if (ortho) {
        float h = orthoSize * 0.5f, w = h * aspect;
        c.projection = glm::ortho(-w, w, -h, h, n, f);
    } else {
        c.projection = glm::perspective(glm::radians(fov), aspect, n, f);
    }
    c.nearClip = n;
    c.farClip = f;
    return c;
}

// --------------------------------------------------------------------------
void Renderer::RenderScene(Scene& scene, const RenderCamera& cam, Framebuffer& target, const RenderOptions& opt) {
    EU_PROFILE_SCOPE("Render");
    // Collect the oldest finished GPU timing, then start timing this frame.
    {
        int q = s_Data->queryIndex;
        if (s_Data->queryPending[q]) {
            GLint ready = 0;
            glGetQueryObjectiv(s_Data->timeQuery[q], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (ready) {
                GLuint64 ns = 0;
                glGetQueryObjectui64v(s_Data->timeQuery[q], GL_QUERY_RESULT, &ns);
                Profiler::Stats().gpuMs = (float)(ns / 1.0e6);
                s_Data->queryPending[q] = false;
            }
        }
        if (!s_Data->queryPending[q]) {
            glBeginQuery(GL_TIME_ELAPSED, s_Data->timeQuery[q]);
            s_Data->queryPending[q] = true;
        } else {
            q = -1;   // still busy: skip timing this frame
        }
        s_Data->activeQuery = q;
        s_Data->queryIndex = (s_Data->queryIndex + 1) % RendererData::kQueries;
    }
    if (scene.Environment().mode2D) {
        RenderCamera used = Renderer2D::Render(scene, cam, target, opt);
        DrawDebugOverlay(scene, used, opt);
        if (s_Data->activeQuery >= 0) glEndQuery(GL_TIME_ELAPSED);
        return;
    }
    Registry& reg = scene.Reg();
    Frame F;
    F.scene = &scene;
    F.env = &scene.Environment();
    F.time = (float)Time::Elapsed();
    const int W = target.Width(), H = target.Height();

    // ---- Prepare: transforms, terrain meshes, model references -----------
    scene.CacheTransforms();
    reg.View<TerrainComponent>([&](EntityId, TerrainComponent& t) { TerrainSystem::Update(t); });
    reg.View<MeshRendererComponent>([&](EntityId, MeshRendererComponent& mr) { EnsureModel(mr); });

    // ---- Lights ------------------------------------------------------------
    LightData& L = F.L;
    struct PL { vec3 pos, color; float range, dist; };
    std::vector<PL> points;
    reg.View<TransformComponent, LightComponent>([&](EntityId id, TransformComponent&, LightComponent& l) {
        if (!scene.CachedActive(id)) return;
        const mat4& w = scene.CachedWorld(id);
        vec3 pos = vec3(w[3]);
        vec3 fwd = glm::normalize(vec3(w * vec4(0, 0, -1, 0)));
        vec3 color = SRGBToLinear(l.color) * l.intensity;
        if (l.type == LightType::Directional) {
            if (!L.hasSun) { L.hasSun = true; L.sunDir = fwd; L.sunColor = color; L.sunShadows = l.castShadows; }
        } else if (l.type == LightType::Point) {
            points.push_back({ pos, color, l.range, glm::distance(pos, cam.position) });
        } else if (L.spotCount < 4) {
            int i = L.spotCount++;
            L.spotPos[i] = pos; L.spotDir[i] = fwd; L.spotColor[i] = color; L.spotRange[i] = l.range;
            L.spotCos[i] = std::cos(glm::radians(l.spotAngle * 0.5f));
        }
        if (opt.showLightGizmos) {
            vec4 c(l.color, 1.0f);
            if (l.type == LightType::Point) DebugDraw::Sphere(pos, 0.25f, c, 12);
            else DebugDraw::Ray(pos, fwd * 2.0f, c);
        }
    });
    // The shaders take the 8 nearest point lights. With more in the scene (a street of lamps),
    // lights near the cut-off fade by how close the next light is to taking their slot, so a
    // light swapping in or out is at zero brightness at that moment instead of popping.
    std::sort(points.begin(), points.end(), [](const PL& a, const PL& b) { return a.dist < b.dist; });
    const float cutoff = points.size() > 8 ? points[8].dist : 1e30f;
    for (auto& p : points) {
        if (L.pointCount >= 8) break;
        int i = L.pointCount++;
        float fade = glm::clamp((cutoff - p.dist) / 6.0f, 0.0f, 1.0f);
        L.pointPos[i] = p.pos; L.pointColor[i] = p.color * fade * fade * (3.0f - 2.0f * fade); L.pointRange[i] = p.range;
    }
    ComputeSkySH(*F.env, s_Data->skySH);

    // ---- Collect draw items ----------------------------------------------
    reg.View<TransformComponent, MeshRendererComponent>([&](EntityId id, TransformComponent&, MeshRendererComponent& mr) {
        if (!mr.visible || !mr.model || !scene.CachedActive(id)) return;
        DrawItem d{ id, scene.CachedWorld(id), &mr, reg.TryGet<AnimatorComponent>(id), false };
        d.skinned = mr.model->IsSkinned() && d.anim && d.anim->runtime && !d.anim->runtime->skinMatrices.empty();
        F.items.push_back(d);
    });
    reg.View<TransformComponent, TerrainComponent>([&](EntityId id, TransformComponent&, TerrainComponent& t) {
        if (!t.runtime || !t.runtime->mesh || !scene.CachedActive(id)) return;
        F.terrains.push_back({ id, scene.CachedWorld(id), &t });
    });

    // ---- HDR scene buffer ----------------------------------------------------
    if (!s_Data->hdr) {
        FramebufferSpec spec;
        spec.width = W;
        spec.height = H;
        spec.attachments = { FBFormat::RGBA16F, FBFormat::R32I, FBFormat::Depth24Stencil8 };
        s_Data->hdr = MakeScope<Framebuffer>(spec);
    }
    Framebuffer& hdr = *s_Data->hdr;
    hdr.Resize(W, H);

    if (s_Settings.rayTraced) {
        // ---- Path traced ----------------------------------------------------
        RayTracer::Lights rl;
        rl.hasSun = L.hasSun;
        rl.sunDir = L.sunDir;
        rl.sunColor = L.sunColor;
        rl.pointCount = L.pointCount;
        for (int i = 0; i < L.pointCount; i++) { rl.pointPos[i] = L.pointPos[i]; rl.pointColor[i] = L.pointColor[i]; rl.pointRange[i] = L.pointRange[i]; }
        rl.spotCount = L.spotCount;
        for (int i = 0; i < L.spotCount; i++) {
            rl.spotPos[i] = L.spotPos[i]; rl.spotDir[i] = L.spotDir[i]; rl.spotColor[i] = L.spotColor[i];
            rl.spotRange[i] = L.spotRange[i]; rl.spotCos[i] = L.spotCos[i];
        }
        RayTracer::Render(scene, cam, rl, hdr);
    } else {
    RayTracer::Reset();   // switching back to ray tracing starts a fresh accumulation

    // ---- Shadows -----------------------------------------------------------
    L.shadows = L.hasSun && L.sunShadows && s_Settings.shadows;
    if (L.shadows) {
        SetupCascades(L, cam);
        ShadowPass(F);
    } else {
        s_Data->cascades.Ensure(s_Settings.shadowMapSize);   // keep a valid texture bound
    }

    // ---- Reflections -------------------------------------------------------
    F.envMap = s_Settings.reflections && s_Data->probe.valid;
    if (s_Settings.reflections) UpdateReflectionProbe(F, cam);

    // ---- Main pass into the HDR buffer ------------------------------------
    hdr.Bind();
    vec3 clear = SRGBToLinear(F.env->skyHorizon);
    glClearColor(clear.r, clear.g, clear.b, 1.0f);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    hdr.ClearAttachmentInt(1, -1);
    DrawWorld(F, cam, PassFlags{}, W, H);
    }   // rasterized

    // ---- Post: HDR -> target color; copy ids and depth across ------------
    {
        // Post writes only color attachment 0 of the target.
        glBindFramebuffer(GL_FRAMEBUFFER, target.Id());
        GLenum only0 = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &only0);
        PostProcess::Inputs in;
        in.hdrColor = hdr.ColorAttachment(0);
        in.depth = hdr.DepthAttachment();
        in.width = W;
        in.height = H;
        in.projection = cam.projection;
        in.nearClip = cam.nearClip;
        in.farClip = cam.farClip;
        in.destFbo = target.Id();
        RenderSettings post = s_Settings;
        if (s_Settings.rayTraced) post.ssao = false;   // path tracing already shades occlusion
        PostProcess::Run(in, *F.env, post);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, hdr.Id());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.Id());
        if (target.ColorCount() > 1) {
            glReadBuffer(GL_COLOR_ATTACHMENT1);
            GLenum only1[2] = { GL_NONE, GL_COLOR_ATTACHMENT1 };
            glDrawBuffers(2, only1);
            glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
        if (target.DepthAttachment())
            glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        std::vector<GLenum> all;
        for (int i = 0; i < target.ColorCount(); i++) all.push_back(GL_COLOR_ATTACHMENT0 + i);
        target.Bind();
        glDrawBuffers((GLsizei)all.size(), all.data());
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    DrawDebugOverlay(scene, cam, opt);
    if (s_Data->activeQuery >= 0) glEndQuery(GL_TIME_ELAPSED);
}

// Debug lines (drawn on the final image, depth-tested)
void Renderer::DrawDebugOverlay(Scene& scene, const RenderCamera& cam, const RenderOptions& opt) {
    if (opt.showColliders) scene.Physics().DebugDraw();
    if (opt.showNavGrid) scene.Nav().DebugDraw();
    auto& lines = DebugDraw::Lines();
    if (!lines.empty()) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        auto ls = GetShader("line.glsl");
        ls->Bind();
        ls->SetMat4("uViewProj", cam.projection * cam.view);
        glBindVertexArray(s_Data->lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, s_Data->lineVBO);
        size_t bytes = lines.size() * sizeof(DebugDraw::LineVertex);
        if (bytes > s_Data->lineCapacity) {
            s_Data->lineCapacity = bytes * 2;
            glBufferData(GL_ARRAY_BUFFER, s_Data->lineCapacity, nullptr, GL_DYNAMIC_DRAW);
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, lines.data());
        glDrawArrays(GL_LINES, 0, (GLsizei)lines.size());
        Profiler::AddDrawCall(0);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    DebugDraw::Clear();
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindVertexArray(0);
}

} // namespace eu
