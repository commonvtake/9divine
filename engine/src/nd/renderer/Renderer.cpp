#include "nd/renderer/Renderer.h"
#include "nd/renderer/GL.h"
#include "nd/renderer/Shader.h"
#include "nd/renderer/Framebuffer.h"
#include "nd/renderer/DebugDraw.h"
#include "nd/renderer/Mesh.h"
#include "nd/scene/Scene.h"
#include "nd/scene/Entity.h"
#include "nd/assets/AssetManager.h"
#include "nd/terrain/Terrain.h"
#include "nd/particles/Particles.h"
#include "nd/physics/Physics.h"
#include "nd/ai/Navigation.h"
#include "nd/core/Application.h"
#include "nd/core/Profiler.h"
#include "nd/core/Log.h"
#include <map>
#include <cmath>

namespace nd {

RenderSettings Renderer::s_Settings;
bool g_GLContextAlive = true;

namespace {

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
    mat4 lightSpace{ 1.0f };
    bool shadows = false;
};

struct RendererData {
    std::map<std::string, Ref<Shader>> shaders;
    Scope<Framebuffer> shadowFB;
    uint32_t emptyVAO = 0;
    uint32_t lineVAO = 0, lineVBO = 0;
    size_t lineCapacity = 0;
    Ref<Mesh> waterGrid;
};
RendererData* s_Data = nullptr;

vec3 SRGBToLinear(const vec3& c) { return glm::pow(glm::max(c, vec3(0.0f)), vec3(2.2f)); }

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

void ApplyLights(Shader& s, const LightData& L, const SceneEnvironment& env, const RenderCamera& cam) {
    s.SetVec3("uCameraPos", cam.position);
    s.SetInt("uHasSun", L.hasSun ? 1 : 0);
    s.SetVec3("uSunDir", glm::normalize(L.sunDir));
    s.SetVec3("uSunColor", L.sunColor);
    s.SetVec3("uSkyTop", env.skyTop);
    s.SetVec3("uSkyHorizon", env.skyHorizon);
    s.SetVec3("uGroundColor", env.groundColor);
    s.SetFloat("uAmbient", env.ambientIntensity);
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
    s.SetMat4("uLightSpace", L.lightSpace);
    s.SetInt("uShadowMap", 5);
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

    FramebufferSpec spec;
    spec.width = spec.height = s_Settings.shadowMapSize;
    spec.attachments = { FBFormat::DepthTexture };
    s_Data->shadowFB = MakeScope<Framebuffer>(spec);
    s_Data->waterGrid = Primitives::Plane(1.0f, 128);
}

void Renderer::Shutdown() {
    if (!s_Data) return;
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
    ND_INFO("Shaders reloaded");
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
    ND_PROFILE_SCOPE("Render");
    Registry& reg = scene.Reg();
    const SceneEnvironment& env = scene.Environment();
    const mat4 viewProj = cam.projection * cam.view;
    const float time = (float)Time::Elapsed();
    Frustum frustum(viewProj);

    // ---- Prepare: terrain meshes, model references -----------------------
    reg.View<TerrainComponent>([&](EntityId, TerrainComponent& t) { TerrainSystem::Update(t); });
    reg.View<MeshRendererComponent>([&](EntityId, MeshRendererComponent& mr) { EnsureModel(mr); });

    // ---- Lights ------------------------------------------------------------
    LightData L;
    struct PL { vec3 pos, color; float range, dist; };
    std::vector<PL> points;
    reg.View<TransformComponent, LightComponent>([&](EntityId id, TransformComponent&, LightComponent& l) {
        Entity e(id, &scene);
        if (!scene.IsActiveInHierarchy(e)) return;
        mat4 w = scene.WorldMatrix(e);
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
    std::sort(points.begin(), points.end(), [](const PL& a, const PL& b) { return a.dist < b.dist; });
    for (auto& p : points) {
        if (L.pointCount >= 8) break;
        int i = L.pointCount++;
        L.pointPos[i] = p.pos; L.pointColor[i] = p.color; L.pointRange[i] = p.range;
    }

    // ---- Collect draw items ----------------------------------------------
    struct DrawItem {
        EntityId id;
        mat4 world;
        MeshRendererComponent* mr;
        AnimatorComponent* anim;
    };
    std::vector<DrawItem> items;
    reg.View<TransformComponent, MeshRendererComponent>([&](EntityId id, TransformComponent&, MeshRendererComponent& mr) {
        if (!mr.visible || !mr.model) return;
        Entity e(id, &scene);
        if (!scene.IsActiveInHierarchy(e)) return;
        DrawItem d{ id, scene.WorldMatrix(e), &mr, reg.TryGet<AnimatorComponent>(id) };
        items.push_back(d);
    });
    struct TerrainItem { EntityId id; mat4 world; TerrainComponent* t; };
    std::vector<TerrainItem> terrains;
    reg.View<TransformComponent, TerrainComponent>([&](EntityId id, TransformComponent&, TerrainComponent& t) {
        Entity e(id, &scene);
        if (!t.runtime || !t.runtime->mesh || !scene.IsActiveInHierarchy(e)) return;
        terrains.push_back({ id, scene.WorldMatrix(e), &t });
    });

    auto isSkinned = [](const DrawItem& d) {
        return d.mr->model->IsSkinned() && d.anim && d.anim->runtime && !d.anim->runtime->skinMatrices.empty();
    };

    // ---- Shadow pass -------------------------------------------------------
    L.shadows = L.hasSun && L.sunShadows && s_Settings.shadows;
    if (L.shadows) {
        ND_PROFILE_SCOPE("Shadows");
        if (s_Data->shadowFB->Width() != s_Settings.shadowMapSize) s_Data->shadowFB->Resize(s_Settings.shadowMapSize, s_Settings.shadowMapSize);
        float dist = s_Settings.shadowDistance;
        mat4 invView = glm::inverse(cam.view);
        vec3 camFwd = -vec3(invView[2]);
        vec3 center = cam.position + camFwd * dist * 0.4f;
        vec3 dir = glm::normalize(L.sunDir);
        vec3 up = std::abs(dir.y) > 0.99f ? vec3(0, 0, 1) : vec3(0, 1, 0);
        mat4 lightView = glm::lookAt(center - dir * 150.0f, center, up);
        // Snap to texels to avoid shimmering
        float texel = (dist * 2.0f) / s_Settings.shadowMapSize;
        vec4 lc = lightView * vec4(center, 1.0f);
        vec2 snapped = glm::floor(vec2(lc) / texel) * texel;
        lightView = glm::translate(mat4(1.0f), vec3(snapped - vec2(lc), 0.0f)) * lightView;
        mat4 lightProj = glm::ortho(-dist, dist, -dist, dist, 1.0f, 400.0f);
        L.lightSpace = lightProj * lightView;

        s_Data->shadowFB->Bind();
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDisable(GL_CULL_FACE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 2.0f);

        auto sh = GetShader("shadow.glsl");
        auto shSkin = GetShader("shadow.glsl", { "SKINNED" });
        auto shInst = GetShader("shadow.glsl", { "INSTANCED" });
        for (auto& d : items) {
            if (!d.mr->castShadows) continue;
            bool skinned = isSkinned(d);
            Shader& s = skinned ? *shSkin : *sh;
            s.Bind();
            s.SetMat4("uLightSpace", L.lightSpace);
            s.SetMat4("uModel", d.world);
            if (skinned) {
                auto& m = d.anim->runtime->skinMatrices;
                s.SetMat4Array("uBones", m.data(), (int)std::min<size_t>(m.size(), 100));
            }
            for (auto& sm : d.mr->model->meshes) sm.mesh->Draw();
        }
        for (auto& t : terrains) {
            sh->Bind();
            sh->SetMat4("uLightSpace", L.lightSpace);
            sh->SetMat4("uModel", t.world);
            t.t->runtime->mesh->Draw();
            shInst->Bind();
            shInst->SetMat4("uLightSpace", L.lightSpace);
            shInst->SetMat4("uModel", t.world);
            for (auto& tb : t.t->runtime->trees) tb.mesh->DrawInstanced(tb.count);
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // ---- Main pass --------------------------------------------------------
    target.Bind();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    vec3 clear = glm::pow(env.skyHorizon, vec3(1.0f));
    glClearColor(clear.r, clear.g, clear.b, 1.0f);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (target.ColorCount() > 1) target.ClearAttachmentInt(1, -1);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, s_Data->shadowFB->DepthAttachment());

    // Sky (no depth write, no id)
    {
        auto sky = GetShader("sky.glsl");
        sky->Bind();
        ApplyLights(*sky, L, env, cam);
        sky->SetMat4("uInvViewProj", glm::inverse(viewProj));
        glDepthMask(GL_FALSE);
        glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glBindVertexArray(s_Data->emptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
    }

    // Opaque meshes
    {
        ND_PROFILE_SCOPE("Opaque");
        auto lit = GetShader("lit.glsl");
        auto litSkin = GetShader("lit.glsl", { "SKINNED" });
        Shader* bound = nullptr;
        auto white = Texture2D::White();
        for (auto& d : items) {
            bool skinned = isSkinned(d);
            if (!skinned && !d.mr->model->IsSkinned()) {
                if (!frustum.Visible(TransformAABB(d.mr->model->bounds, d.world))) continue;
            }
            Shader* s = skinned ? litSkin.get() : lit.get();
            if (s != bound) {
                s->Bind();
                ApplyLights(*s, L, env, cam);
                s->SetMat4("uViewProj", viewProj);
                s->SetInt("uAlbedoMap", 0);
                bound = s;
            }
            s->SetMat4("uModel", d.world);
            s->SetInt("uEntityID", (int)d.id);
            if (skinned) {
                auto& m = d.anim->runtime->skinMatrices;
                s->SetMat4Array("uBones", m.data(), (int)std::min<size_t>(m.size(), 100));
            }
            Ref<Texture2D> overrideTex = d.mr->texture.empty() ? nullptr : AssetManager::GetTexture(d.mr->texture);
            for (auto& sm : d.mr->model->meshes) {
                const MaterialData* mat = (d.mr->useModelMaterials && sm.material >= 0 && sm.material < (int)d.mr->model->materials.size())
                                              ? &d.mr->model->materials[sm.material] : nullptr;
                vec4 color = d.mr->color * (mat ? mat->baseColor : vec4(1.0f));
                Ref<Texture2D> tex = overrideTex ? overrideTex : (mat && mat->albedo ? mat->albedo : white);
                s->SetVec4("uColor", color);
                s->SetFloat("uTiling", d.mr->tiling);
                s->SetFloat("uSpecular", mat ? mat->specular : d.mr->specular);
                s->SetFloat("uShininess", mat ? mat->shininess : d.mr->shininess);
                s->SetVec3("uEmissive", d.mr->emissive + (mat ? mat->emissive : vec3(0.0f)));
                tex->Bind(0);
                if (mat && mat->doubleSided) glDisable(GL_CULL_FACE);
                sm.mesh->Draw();
                if (mat && mat->doubleSided) glEnable(GL_CULL_FACE);
            }
        }
    }

    // Terrain + foliage
    if (!terrains.empty()) {
        ND_PROFILE_SCOPE("Terrain");
        auto ts = GetShader("terrain.glsl");
        auto treeShader = GetShader("lit.glsl", { "INSTANCED" });
        auto grassShader = GetShader("grass.glsl");
        auto white = Texture2D::White();
        for (auto& t : terrains) {
            TerrainComponent& tc = *t.t;
            ts->Bind();
            ApplyLights(*ts, L, env, cam);
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
                ApplyLights(*treeShader, L, env, cam);
                treeShader->SetMat4("uViewProj", viewProj);
                treeShader->SetMat4("uModel", t.world);
                treeShader->SetInt("uAlbedoMap", 0);
                treeShader->SetInt("uEntityID", (int)t.id);
                treeShader->SetFloat("uTiling", 1.0f);
                treeShader->SetFloat("uSpecular", 0.05f);
                treeShader->SetFloat("uShininess", 8.0f);
                treeShader->SetVec3("uEmissive", vec3(0.0f));
                for (auto& tb : tc.runtime->trees) {
                    treeShader->SetVec4("uColor", tb.color);
                    (tb.texture ? tb.texture : white)->Bind(0);
                    tb.mesh->DrawInstanced(tb.count);
                }
            }
            if (s_Settings.grass && tc.grass && tc.runtime->grassCount > 0) {
                glDisable(GL_CULL_FACE);
                glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                grassShader->Bind();
                ApplyLights(*grassShader, L, env, cam);
                grassShader->SetMat4("uViewProj", viewProj);
                grassShader->SetMat4("uModel", t.world);
                grassShader->SetFloat("uTime", time);
                grassShader->SetVec3("uCameraPosV", cam.position);
                grassShader->SetFloat("uMaxDistance", s_Settings.grassDistance);
                grassShader->SetVec3("uGrassColor", tc.grassColor);
                tc.runtime->grassMesh->DrawInstanced(tc.runtime->grassCount);
                glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glEnable(GL_CULL_FACE);
            }
        }
    }

    // ---- Transparent: water ----------------------------------------------
    glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    {
        auto ws = GetShader("water.glsl");
        bool bound = false;
        reg.View<TransformComponent, WaterComponent>([&](EntityId id, TransformComponent&, WaterComponent& w) {
            Entity e(id, &scene);
            if (!scene.IsActiveInHierarchy(e)) return;
            if (!bound) {
                ws->Bind();
                ApplyLights(*ws, L, env, cam);
                ws->SetMat4("uViewProj", viewProj);
                ws->SetFloat("uTime", time);
                bound = true;
            }
            mat4 world = scene.WorldMatrix(e) * glm::scale(mat4(1.0f), vec3(w.size.x, 1.0f, w.size.y));
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
    {
        ND_PROFILE_SCOPE("Particles");
        struct PE { ParticleEmitterComponent* e; mat4 world; float dist; };
        std::vector<PE> emitters;
        uint32_t total = 0;
        reg.View<TransformComponent, ParticleEmitterComponent>([&](EntityId id, TransformComponent&, ParticleEmitterComponent& p) {
            Entity e(id, &scene);
            if (!p.runtime || p.runtime->particles.empty() || !scene.IsActiveInHierarchy(e)) return;
            mat4 w = scene.WorldMatrix(e);
            emitters.push_back({ &p, w, glm::distance(vec3(w[3]), cam.position) });
            total += (uint32_t)p.runtime->particles.size();
        });
        Profiler::Stats().particles = total;
        if (!emitters.empty()) {
            std::sort(emitters.begin(), emitters.end(), [](const PE& a, const PE& b) { return a.dist > b.dist; });
            auto ps = GetShader("particle.glsl");
            ps->Bind();
            ApplyLights(*ps, L, env, cam);
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

    // ---- Debug lines ----------------------------------------------------
    if (opt.showColliders) scene.Physics().DebugDraw();
    if (opt.showNavGrid) scene.Nav().DebugDraw();
    auto& lines = DebugDraw::Lines();
    if (!lines.empty()) {
        auto ls = GetShader("line.glsl");
        ls->Bind();
        ls->SetMat4("uViewProj", viewProj);
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
    }
    DebugDraw::Clear();

    glDisable(GL_BLEND);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindVertexArray(0);
}

} // namespace nd
