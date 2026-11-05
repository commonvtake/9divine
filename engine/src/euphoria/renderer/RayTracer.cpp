#include "euphoria/renderer/RayTracer.h"
#include "euphoria/renderer/Renderer.h"
#include "euphoria/renderer/Shader.h"
#include "euphoria/renderer/Framebuffer.h"
#include "euphoria/renderer/Mesh.h"
#include "euphoria/renderer/Texture.h"
#include "euphoria/renderer/GL.h"
#include "euphoria/scene/Scene.h"
#include "euphoria/assets/Model.h"
#include "euphoria/assets/AssetManager.h"
#include "euphoria/terrain/Terrain.h"
#include "euphoria/core/Profiler.h"
#include "euphoria/core/Log.h"
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace eu {

// ======================================================================= BVH
namespace {
struct Bounds {
    vec3 mn{ 1e30f }, mx{ -1e30f };
    void Grow(const vec3& p) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
    void Grow(const Bounds& b) { mn = glm::min(mn, b.mn); mx = glm::max(mx, b.mx); }
    float Area() const {
        vec3 e = mx - mn;
        return (e.x < 0.0f) ? 0.0f : e.x * e.y + e.y * e.z + e.z * e.x;
    }
};
} // namespace

void BVH::Build(const std::vector<vec3>& v0, const std::vector<vec3>& v1, const std::vector<vec3>& v2) {
    const uint32_t n = (uint32_t)v0.size();
    nodes.clear();
    triIndex.resize(n);
    for (uint32_t i = 0; i < n; i++) triIndex[i] = i;
    if (n == 0) return;
    std::vector<vec3> centroid(n);
    std::vector<Bounds> tb(n);
    for (uint32_t i = 0; i < n; i++) {
        centroid[i] = (v0[i] + v1[i] + v2[i]) / 3.0f;
        tb[i].Grow(v0[i]); tb[i].Grow(v1[i]); tb[i].Grow(v2[i]);
    }
    nodes.reserve(2 * n);
    nodes.push_back({});
    nodes[0].leftFirst = 0;
    nodes[0].count = n;

    struct Work { uint32_t node; };
    std::vector<Work> stack{ { 0 } };
    constexpr int kBins = 12;
    while (!stack.empty()) {
        uint32_t ni = stack.back().node;
        stack.pop_back();
        Node& node = nodes[ni];
        Bounds nb, cb;
        for (uint32_t i = 0; i < node.count; i++) {
            uint32_t t = triIndex[node.leftFirst + i];
            nb.Grow(tb[t]);
            cb.Grow(centroid[t]);
        }
        node.bmin = nb.mn;
        node.bmax = nb.mx;
        if (node.count <= 4) continue;

        // Binned SAH over the axis-aligned centroid bounds
        float bestCost = 1e30f;
        int bestAxis = -1;
        float bestSplit = 0.0f;
        for (int axis = 0; axis < 3; axis++) {
            float lo = cb.mn[axis], hi = cb.mx[axis];
            if (hi - lo < 1e-7f) continue;
            Bounds bins[kBins];
            uint32_t counts[kBins] = {};
            float scale = kBins / (hi - lo);
            for (uint32_t i = 0; i < node.count; i++) {
                uint32_t t = triIndex[node.leftFirst + i];
                int b = std::min(kBins - 1, (int)((centroid[t][axis] - lo) * scale));
                counts[b]++;
                bins[b].Grow(tb[t]);
            }
            float leftArea[kBins - 1], rightArea[kBins - 1];
            uint32_t leftCount[kBins - 1], rightCount[kBins - 1];
            Bounds lb, rb;
            uint32_t lc = 0, rc = 0;
            for (int i = 0; i < kBins - 1; i++) {
                lc += counts[i]; lb.Grow(bins[i]); leftCount[i] = lc; leftArea[i] = lb.Area();
                rc += counts[kBins - 1 - i]; rb.Grow(bins[kBins - 1 - i]); rightCount[kBins - 2 - i] = rc; rightArea[kBins - 2 - i] = rb.Area();
            }
            for (int i = 0; i < kBins - 1; i++) {
                float cost = leftCount[i] * leftArea[i] + rightCount[i] * rightArea[i];
                if (leftCount[i] && rightCount[i] && cost < bestCost) {
                    bestCost = cost;
                    bestAxis = axis;
                    bestSplit = lo + (i + 1) / scale;
                }
            }
        }
        if (bestAxis < 0 || bestCost >= node.count * nb.Area()) continue;   // splitting doesn't pay off

        // Partition
        uint32_t i = node.leftFirst, j = node.leftFirst + node.count - 1;
        while (i <= j) {
            if (centroid[triIndex[i]][bestAxis] < bestSplit) i++;
            else { std::swap(triIndex[i], triIndex[j]); if (j == 0) break; j--; }
        }
        uint32_t leftCount = i - node.leftFirst;
        if (leftCount == 0 || leftCount == node.count) continue;
        uint32_t left = (uint32_t)nodes.size();
        nodes.push_back({});
        nodes.push_back({});
        Node& self = nodes[ni];                 // re-fetch: push_back may have reallocated
        nodes[left].leftFirst = self.leftFirst;
        nodes[left].count = leftCount;
        nodes[left + 1].leftFirst = i;
        nodes[left + 1].count = self.count - leftCount;
        self.leftFirst = left;
        self.count = 0;
        stack.push_back({ left + 1 });
        stack.push_back({ left });
    }
}

namespace {
bool RayAABB(const vec3& o, const vec3& invD, const vec3& mn, const vec3& mx, float tmax) {
    vec3 t0 = (mn - o) * invD, t1 = (mx - o) * invD;
    vec3 tl = glm::min(t0, t1), th = glm::max(t0, t1);
    float a = std::max(std::max(tl.x, tl.y), std::max(tl.z, 0.0f));
    float b = std::min(std::min(th.x, th.y), std::min(th.z, tmax));
    return a <= b;
}
float RayTri(const vec3& o, const vec3& d, const vec3& a, const vec3& b, const vec3& c) {
    vec3 e1 = b - a, e2 = c - a, p = glm::cross(d, e2);
    float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-9f) return -1.0f;
    float inv = 1.0f / det;
    vec3 s = o - a;
    float u = glm::dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return -1.0f;
    vec3 q = glm::cross(s, e1);
    float v = glm::dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;
    float t = glm::dot(e2, q) * inv;
    return t > 1e-5f ? t : -1.0f;
}
} // namespace

float BVH::Intersect(const vec3& o, const vec3& d, const std::vector<vec3>& v0, const std::vector<vec3>& v1,
                     const std::vector<vec3>& v2, uint32_t* triOut) const {
    if (nodes.empty()) return -1.0f;
    vec3 invD(1.0f / (std::abs(d.x) > 1e-12f ? d.x : 1e-12f), 1.0f / (std::abs(d.y) > 1e-12f ? d.y : 1e-12f),
              1.0f / (std::abs(d.z) > 1e-12f ? d.z : 1e-12f));
    float best = 1e30f;
    uint32_t bestTri = 0;
    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& n = nodes[stack[--sp]];
        if (!RayAABB(o, invD, n.bmin, n.bmax, best)) continue;
        if (n.count > 0) {
            for (uint32_t i = 0; i < n.count; i++) {
                uint32_t t = triIndex[n.leftFirst + i];
                float h = RayTri(o, d, v0[t], v1[t], v2[t]);
                if (h > 0.0f && h < best) { best = h; bestTri = t; }
            }
        } else if (sp < 62) {
            stack[sp++] = n.leftFirst;
            stack[sp++] = n.leftFirst + 1;
        }
    }
    if (best >= 1e30f) return -1.0f;
    if (triOut) *triOut = bestTri;
    return best;
}

int BVH::Depth() const {
    if (nodes.empty()) return 0;
    std::vector<std::pair<uint32_t, int>> st{ { 0, 1 } };
    int depth = 0;
    while (!st.empty()) {
        auto [i, d] = st.back();
        st.pop_back();
        depth = std::max(depth, d);
        if (nodes[i].count == 0) { st.push_back({ nodes[i].leftFirst, d + 1 }); st.push_back({ nodes[i].leftFirst + 1, d + 1 }); }
    }
    return depth;
}

// ======================================================================= GPU path tracer
namespace {

// One material per distinct look; the shader indexes it per triangle.
struct RTMaterial {
    vec3 albedo{ 1.0f };   // linear
    float roughness = 0.5f;
    vec3 emissive{ 0.0f };
    float metallic = 0.0f;
    float water = 0.0f;
};

struct Geometry {
    std::vector<vec3> v0, v1, v2, n0, n1, n2, c0, c1, c2;
    std::vector<int> mat;
    std::vector<int> entity;
    std::vector<RTMaterial> materials;

    void Add(const vec3& a, const vec3& b, const vec3& c, const vec3& na, const vec3& nb, const vec3& nc, int m, int ent,
             const vec3& ca = vec3(1.0f), const vec3& cb = vec3(1.0f), const vec3& cc = vec3(1.0f)) {
        v0.push_back(a); v1.push_back(b); v2.push_back(c);
        n0.push_back(na); n1.push_back(nb); n2.push_back(nc);
        c0.push_back(ca); c1.push_back(cb); c2.push_back(cc);
        mat.push_back(m);
        entity.push_back(ent);
    }
    void Clear() {
        for (auto* v : { &v0, &v1, &v2, &n0, &n1, &n2, &c0, &c1, &c2 }) v->clear();
        mat.clear(); entity.clear(); materials.clear();
    }
};

struct BufferTexture {
    uint32_t buffer = 0, texture = 0;
    void Upload(const std::vector<vec4>& data) {
        if (!buffer) { glGenBuffers(1, &buffer); glGenTextures(1, &texture); }
        glBindBuffer(GL_TEXTURE_BUFFER, buffer);
        glBufferData(GL_TEXTURE_BUFFER, std::max<size_t>(1, data.size()) * sizeof(vec4), data.empty() ? nullptr : data.data(), GL_STATIC_DRAW);
        glBindTexture(GL_TEXTURE_BUFFER, texture);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, buffer);
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
    }
    void Release() {
        if (buffer && g_GLContextAlive) { glDeleteBuffers(1, &buffer); glDeleteTextures(1, &texture); }
        buffer = texture = 0;
    }
};

struct RTData {
    Geometry geo;
    BVH bvh;
    BufferTexture tris, nodes, mats;
    // Ping-pong accumulation targets sharing one ID and depth texture
    uint32_t fbo[2] = { 0, 0 }, accum[2] = { 0, 0 }, ids = 0, depth = 0;
    int width = 0, height = 0, current = 0;
    int samples = 0;
    uint64_t staticHash = 0, dynamicHash = 0, viewHash = 0;
    bool dynamicMoving = false;
    size_t lastTriCount = 0;
    mat4 lastView{ 0.0f }, lastProj{ 0.0f };
    vec3 anchorPos{ 1e30f }, anchorFwd{ 0.0f };
    uint64_t lightHash = 0;
    uint32_t vao = 0;
    int frame = 0;
};
RTData* s_RT = nullptr;

float FloatBits(int32_t i) { float f; std::memcpy(&f, &i, 4); return f; }
template <typename T> void Mix(uint64_t& h, const T& v) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&v);
    for (size_t i = 0; i < sizeof(T); i++) { h ^= p[i]; h *= 1099511628211ull; }
}
vec3 Lin(const vec3& c) { return glm::pow(glm::max(c, vec3(0.0f)), vec3(2.2f)); }

// The terrain shader's procedural coloring, evaluated per vertex on the CPU.
float Hash2(float x, float y) { float v = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f; return v - std::floor(v); }
float Noise2(float x, float y) {
    float ix = std::floor(x), iy = std::floor(y), fx = x - ix, fy = y - iy;
    float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    float a = Hash2(ix, iy), b = Hash2(ix + 1, iy), c = Hash2(ix, iy + 1), d = Hash2(ix + 1, iy + 1);
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
}
vec3 TerrainColor(const TerrainComponent& t, const vec3& local, const vec3& N) {
    auto ss = [](float e0, float e1, float x) { float k = glm::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return k * k * (3 - 2 * k); };
    float n = Noise2(local.x * 0.35f, local.z * 0.35f) * 0.6f + Noise2(local.x * 2.1f, local.z * 2.1f) * 0.4f;
    float slope = 1.0f - N.y;
    vec3 grass = glm::mix(t.grassColor * 0.85f, t.grassColor * 1.12f, n);
    vec3 c = glm::mix(Lin(t.sandColor) * (0.9f + 0.2f * n), Lin(grass), ss(t.sandHeight - 0.3f, t.sandHeight + 0.4f, local.y + n * 0.3f));
    c = glm::mix(c, Lin(t.rockColor) * (0.8f + 0.4f * n), ss(0.28f, 0.45f, slope + (n - 0.5f) * 0.1f));
    c = glm::mix(c, Lin(t.snowColor), ss(t.snowHeight - 1.0f, t.snowHeight + 1.0f, local.y + n * 1.5f) * (1.0f - ss(0.4f, 0.6f, slope)));
    return c;
}

// Average color of a texture (its 1x1 mip), so textured materials keep their overall tint.
std::unordered_map<uint32_t, vec3> s_AvgColor;
vec3 AverageColor(const Ref<Texture2D>& tex) {
    if (!tex) return vec3(1.0f);
    auto it = s_AvgColor.find(tex->Id());
    if (it != s_AvgColor.end()) return it->second;
    glBindTexture(GL_TEXTURE_2D, tex->Id());
    glGenerateMipmap(GL_TEXTURE_2D);
    int w = tex->Width(), h = tex->Height(), level = 0;
    while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); level++; }
    float px[4] = { 1, 1, 1, 1 };
    glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_FLOAT, px);
    vec3 c = Lin(vec3(px[0], px[1], px[2]));
    s_AvgColor[tex->Id()] = c;
    return c;
}

// Collects every traceable triangle in world space. Returns hashes of the static and
// animated content so callers can tell what changed.
void Gather(Scene& scene, Geometry& g, uint64_t& staticHash, uint64_t& dynamicHash) {
    g.Clear();
    staticHash = dynamicHash = 1469598103934665603ull;
    Registry& reg = scene.Reg();
    std::unordered_map<uint64_t, int> matIndex;
    auto material = [&](const RTMaterial& m) {
        uint64_t h = 1469598103934665603ull;
        Mix(h, m.albedo); Mix(h, m.roughness); Mix(h, m.emissive); Mix(h, m.metallic); Mix(h, m.water);
        auto it = matIndex.find(h);
        if (it != matIndex.end()) return it->second;
        int i = (int)g.materials.size();
        g.materials.push_back(m);
        matIndex[h] = i;
        return i;
    };

    // Meshes (skinned ones posed on the CPU)
    reg.View<TransformComponent, MeshRendererComponent>([&](EntityId id, TransformComponent&, MeshRendererComponent& mr) {
        if (!mr.visible || !mr.model || !scene.CachedActive(id)) return;
        const mat4& world = scene.CachedWorld(id);
        mat3 nworld = glm::transpose(glm::inverse(mat3(world)));
        auto* anim = reg.TryGet<AnimatorComponent>(id);
        const std::vector<mat4>* bones = (mr.model->IsSkinned() && anim && anim->runtime && !anim->runtime->skinMatrices.empty())
                                             ? &anim->runtime->skinMatrices : nullptr;
        uint64_t& h = bones ? dynamicHash : staticHash;
        Mix(h, world); Mix(h, mr.model.get()); Mix(h, mr.color); Mix(h, mr.metallic); Mix(h, mr.roughness); Mix(h, mr.emissive);
        if (bones) for (auto& b : *bones) Mix(h, b);
        for (auto& sm : mr.model->meshes) {
            const MaterialData* md = (mr.useModelMaterials && sm.material >= 0 && sm.material < (int)mr.model->materials.size())
                                         ? &mr.model->materials[sm.material] : nullptr;
            RTMaterial m;
            Ref<Texture2D> tex = !mr.texture.empty() ? AssetManager::GetTexture(mr.texture) : (md ? md->albedo : nullptr);
            m.albedo = Lin(vec3(mr.color) * (md ? vec3(md->baseColor) : vec3(1.0f))) * AverageColor(tex);
            m.roughness = md ? md->roughness : mr.roughness;
            m.metallic = md ? md->metallic : mr.metallic;
            m.emissive = mr.emissive + (md ? md->emissive : vec3(0.0f));
            int mi = material(m);
            const auto& vs = sm.mesh->Vertices();
            const auto& is = sm.mesh->Indices();
            auto xf = [&](const Vertex& v, vec3& p, vec3& n) {
                vec4 pos(v.position, 1.0f);
                vec3 nrm = v.normal;
                if (bones) {
                    float ws = v.weights.x + v.weights.y + v.weights.z + v.weights.w;
                    if (ws > 0.0f) {
                        mat4 skin(0.0f);
                        for (int k = 0; k < 4; k++)
                            if (v.weights[k] > 0.0f && v.joints[k] >= 0 && v.joints[k] < (int)bones->size()) skin += (*bones)[v.joints[k]] * v.weights[k];
                        pos = skin * pos;
                        nrm = mat3(skin) * nrm;
                    }
                }
                p = vec3(world * pos);
                n = glm::normalize(nworld * nrm);
            };
            for (size_t i = 0; i + 2 < is.size(); i += 3) {
                vec3 p[3], n[3];
                for (int k = 0; k < 3; k++) xf(vs[is[i + k]], p[k], n[k]);
                g.Add(p[0], p[1], p[2], n[0], n[1], n[2], mi, (int)id);
            }
        }
    });

    // Terrain + trees
    reg.View<TransformComponent, TerrainComponent>([&](EntityId id, TransformComponent&, TerrainComponent& t) {
        if (!t.runtime || !t.runtime->mesh || !scene.CachedActive(id)) return;
        const mat4& world = scene.CachedWorld(id);
        mat3 nworld = glm::transpose(glm::inverse(mat3(world)));
        Mix(staticHash, world); Mix(staticHash, t.runtime->mesh.get()); Mix(staticHash, t.runtime->mesh->Vertices().size());
        if (!t.heights.empty()) Mix(staticHash, t.heights[t.heights.size() / 2]);
        RTMaterial terrainMat;
        terrainMat.roughness = 0.92f;
        int tm = material(terrainMat);
        const auto& vs = t.runtime->mesh->Vertices();
        const auto& is = t.runtime->mesh->Indices();
        std::vector<vec3> colors(vs.size());
        for (size_t i = 0; i < vs.size(); i++) colors[i] = TerrainColor(t, vs[i].position, glm::normalize(vs[i].normal));
        for (size_t i = 0; i + 2 < is.size(); i += 3) {
            uint32_t a = is[i], b = is[i + 1], c = is[i + 2];
            g.Add(vec3(world * vec4(vs[a].position, 1)), vec3(world * vec4(vs[b].position, 1)), vec3(world * vec4(vs[c].position, 1)),
                  glm::normalize(nworld * vs[a].normal), glm::normalize(nworld * vs[b].normal), glm::normalize(nworld * vs[c].normal),
                  tm, (int)id, colors[a], colors[b], colors[c]);
        }
        for (auto& tb : t.runtime->trees) {
            RTMaterial m;
            m.albedo = Lin(vec3(tb.color)) * AverageColor(tb.texture);
            m.roughness = 0.8f;
            int mi = material(m);
            const auto& tv = tb.mesh->Vertices();
            const auto& ti = tb.mesh->Indices();
            for (const mat4& inst : tb.instances) {
                mat4 w = world * inst;
                mat3 nw = glm::transpose(glm::inverse(mat3(w)));
                for (size_t i = 0; i + 2 < ti.size(); i += 3) {
                    const Vertex &a = tv[ti[i]], &b = tv[ti[i + 1]], &c = tv[ti[i + 2]];
                    g.Add(vec3(w * vec4(a.position, 1)), vec3(w * vec4(b.position, 1)), vec3(w * vec4(c.position, 1)),
                          glm::normalize(nw * a.normal), glm::normalize(nw * b.normal), glm::normalize(nw * c.normal), mi, (int)id);
                }
            }
        }
    });

    // Water: a flat, glossy dielectric
    reg.View<TransformComponent, WaterComponent>([&](EntityId id, TransformComponent&, WaterComponent& w) {
        if (!scene.CachedActive(id)) return;
        mat4 world = scene.CachedWorld(id) * glm::scale(mat4(1.0f), vec3(w.size.x, 1.0f, w.size.y));
        Mix(staticHash, world); Mix(staticHash, w.deepColor);
        RTMaterial m;
        m.albedo = Lin(glm::mix(vec3(w.deepColor), vec3(w.shallowColor), 0.3f));
        m.roughness = 0.04f;
        m.water = 1.0f;
        int mi = material(m);
        vec3 p[4] = { vec3(world * vec4(-0.5f, 0, -0.5f, 1)), vec3(world * vec4(0.5f, 0, -0.5f, 1)),
                      vec3(world * vec4(0.5f, 0, 0.5f, 1)), vec3(world * vec4(-0.5f, 0, 0.5f, 1)) };
        vec3 up = glm::normalize(vec3(world * vec4(0, 1, 0, 0)));
        g.Add(p[0], p[2], p[1], up, up, up, mi, (int)id);
        g.Add(p[0], p[3], p[2], up, up, up, mi, (int)id);
    });
}

void Upload(RTData& R) {
    const Geometry& g = R.geo;
    const uint32_t n = (uint32_t)g.v0.size();
    std::vector<vec4> tris;
    tris.reserve((size_t)n * 9);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t t = R.bvh.triIndex[k];   // store in BVH order so leaves index contiguously
        tris.push_back(vec4(g.v0[t], (float)g.mat[t]));
        tris.push_back(vec4(g.v1[t], FloatBits(g.entity[t])));
        tris.push_back(vec4(g.v2[t], 0.0f));
        tris.push_back(vec4(g.n0[t], 0.0f));
        tris.push_back(vec4(g.n1[t], 0.0f));
        tris.push_back(vec4(g.n2[t], 0.0f));
        tris.push_back(vec4(g.c0[t], 0.0f));
        tris.push_back(vec4(g.c1[t], 0.0f));
        tris.push_back(vec4(g.c2[t], 0.0f));
    }
    std::vector<vec4> nodes;
    nodes.reserve(R.bvh.nodes.size() * 2);
    for (auto& nd : R.bvh.nodes) {
        nodes.push_back(vec4(nd.bmin, FloatBits((int32_t)nd.leftFirst)));
        nodes.push_back(vec4(nd.bmax, FloatBits((int32_t)nd.count)));
    }
    std::vector<vec4> mats;
    for (auto& m : g.materials) {
        mats.push_back(vec4(m.albedo, m.roughness));
        mats.push_back(vec4(m.emissive, m.metallic));
        mats.push_back(vec4(m.water, 0, 0, 0));
    }
    R.tris.Upload(tris);
    R.nodes.Upload(nodes);
    R.mats.Upload(mats);
}

void EnsureTargets(RTData& R, int w, int h) {
    if (R.fbo[0] && R.width == w && R.height == h) return;
    if (R.fbo[0]) {
        glDeleteFramebuffers(2, R.fbo);
        glDeleteTextures(2, R.accum);
        glDeleteTextures(1, &R.ids);
        glDeleteTextures(1, &R.depth);
    }
    R.width = w;
    R.height = h;
    auto tex2d = [&](uint32_t& t, GLenum ifmt, GLenum fmt, GLenum type) {
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    tex2d(R.ids, GL_R32I, GL_RED_INTEGER, GL_INT);
    tex2d(R.depth, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8);
    glGenFramebuffers(2, R.fbo);
    for (int i = 0; i < 2; i++) {
        tex2d(R.accum[i], GL_RGBA32F, GL_RGBA, GL_FLOAT);
        glBindFramebuffer(GL_FRAMEBUFFER, R.fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, R.accum[i], 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, R.ids, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, R.depth, 0);
        GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers(2, bufs);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) EU_ERROR("Ray tracing framebuffer is incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    R.samples = 0;
}

} // namespace

void RayTracer::Init() {
    s_RT = new RTData();
    glGenVertexArrays(1, &s_RT->vao);
}

void RayTracer::Shutdown() {
    if (!s_RT) return;
    if (g_GLContextAlive) {
        s_RT->tris.Release();
        s_RT->nodes.Release();
        s_RT->mats.Release();
        if (s_RT->fbo[0]) {
            glDeleteFramebuffers(2, s_RT->fbo);
            glDeleteTextures(2, s_RT->accum);
            glDeleteTextures(1, &s_RT->ids);
            glDeleteTextures(1, &s_RT->depth);
        }
        glDeleteVertexArrays(1, &s_RT->vao);
    }
    delete s_RT;
    s_RT = nullptr;
    s_AvgColor.clear();
}

void RayTracer::Reset() { if (s_RT) s_RT->samples = 0; }
int RayTracer::SampleCount() { return s_RT ? s_RT->samples : 0; }
size_t RayTracer::TriangleCount() { return s_RT ? s_RT->geo.v0.size() : 0; }

void RayTracer::Render(Scene& scene, const RenderCamera& cam, const Lights& L, Framebuffer& hdr) {
    EU_PROFILE_SCOPE("Ray tracing");
    RTData& R = *s_RT;
    const auto& rs = Renderer::Settings();
    const SceneEnvironment& env = scene.Environment();
    const int W = hdr.Width(), H = hdr.Height();
    EnsureTargets(R, W, H);

    // ---- Geometry: rebuild when anything moved -----------------------------
    uint64_t staticHash, dynamicHash;
    {
        EU_PROFILE_SCOPE("RT gather");
        Gather(scene, R.geo, staticHash, dynamicHash);
    }
    bool moved = staticHash != R.staticHash || dynamicHash != R.dynamicHash;
    bool topologyChanged = R.geo.v0.size() != R.lastTriCount;   // objects added/removed
    if (moved || R.bvh.nodes.empty()) {
        EU_PROFILE_SCOPE("RT build");
        R.bvh.Build(R.geo.v0, R.geo.v1, R.geo.v2);
        Upload(R);
    }
    // Moving things (animated characters, spinning pickups, a dragged gizmo) switch the
    // accumulation to a short moving average instead of restarting it, so the rest of the
    // image keeps converging.
    R.dynamicMoving = moved;
    R.staticHash = staticHash;
    R.dynamicHash = dynamicHash;
    R.lastTriCount = R.geo.v0.size();

    // ---- Restart accumulation when the view, lights or static scene change --
    // What restarts accumulation and what only shortens it:
    //  - a real camera move (> 2 cm or > 0.25 degrees since the last restart), a resolution,
    //    sky/fog/quality change, or objects being added/removed: restart from zero;
    //  - small camera drift (a follow camera easing, a character settling), flickering
    //    lights, moving objects: keep going as a short moving average, so the still parts of
    //    the image stay clean and the moving parts don't smear.
    vec3 fwd = -vec3(glm::inverse(cam.view)[2]);
    float drift = glm::distance(cam.position, R.anchorPos);
    float turn = glm::degrees(std::acos(glm::clamp(glm::dot(fwd, R.anchorFwd), -1.0f, 1.0f)));
    bool projChanged = false;
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) projChanged |= std::abs(cam.projection[c][r] - R.lastProj[c][r]) > 1e-4f;
    bool cameraJumped = drift > 0.02f || turn > 0.25f || projChanged;
    bool cameraDrifting = !cameraJumped && (cam.view != R.lastView);
    R.lastView = cam.view;
    if (cameraJumped) { R.anchorPos = cam.position; R.anchorFwd = fwd; R.lastProj = cam.projection; }

    uint64_t view = 1469598103934665603ull;
    Mix(view, W); Mix(view, H);
    Mix(view, env.skyTop); Mix(view, env.skyHorizon); Mix(view, env.groundColor); Mix(view, env.ambientIntensity);
    Mix(view, env.fog); Mix(view, env.fogDensity); Mix(view, env.fogColor); Mix(view, rs.rtMaxBounces);
    uint64_t lights = 1469598103934665603ull;
    Mix(lights, L.sunDir); Mix(lights, L.sunColor); Mix(lights, L.pointCount); Mix(lights, L.spotCount);
    for (int i = 0; i < L.pointCount; i++) { Mix(lights, L.pointPos[i]); Mix(lights, L.pointColor[i]); }
    for (int i = 0; i < L.spotCount; i++) { Mix(lights, L.spotPos[i]); Mix(lights, L.spotColor[i]); Mix(lights, L.spotDir[i]); }
    bool lightsChanged = lights != R.lightHash;
    R.lightHash = lights;
    if (view != R.viewHash || topologyChanged || cameraJumped) R.samples = 0;
    R.viewHash = view;
    R.dynamicMoving = R.dynamicMoving || lightsChanged || cameraDrifting;

    int prev = R.current;
    bool converged = R.samples >= rs.rtMaxSamples;
    if (!converged) {
        R.current = 1 - R.current;
        glBindFramebuffer(GL_FRAMEBUFFER, R.fbo[R.current]);
        glViewport(0, 0, W, H);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_ALWAYS);
        glDepthMask(GL_TRUE);
        auto pt = Renderer::GetShader("pathtrace.glsl");
        pt->Bind();
        auto bindBuf = [&](int unit, uint32_t tex, const char* name) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_BUFFER, tex);
            pt->SetInt(name, unit);
        };
        bindBuf(0, R.tris.texture, "uTris");
        bindBuf(1, R.nodes.texture, "uNodes");
        bindBuf(2, R.mats.texture, "uMats");
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, R.accum[prev]);
        pt->SetInt("uPrev", 3);
        glActiveTexture(GL_TEXTURE0);
        // While characters animate, cap the history (a moving average) so they don't leave
        // long trails; 64 frames hides subtle motion like idle breathing yet averages noise.
        int effective = R.dynamicMoving ? std::min(R.samples, 64) : R.samples;
        pt->SetFloat("uBlend", 1.0f / (effective + 1));
        pt->SetInt("uFrame", R.frame++);
        pt->SetInt("uMaxBounces", glm::clamp(rs.rtMaxBounces, 1, 8));
        pt->SetInt("uHasGeometry", R.bvh.nodes.empty() ? 0 : 1);
        mat4 viewProj = cam.projection * cam.view;
        pt->SetMat4("uViewProj", viewProj);
        pt->SetMat4("uInvViewProj", glm::inverse(viewProj));
        pt->SetVec2("uResolution", vec2((float)W, (float)H));
        // Lighting uniforms (names shared with lighting.glsl)
        pt->SetVec3("uCameraPos", cam.position);
        pt->SetInt("uHasSun", L.hasSun ? 1 : 0);
        pt->SetVec3("uSunDir", glm::normalize(L.sunDir));
        pt->SetVec3("uSunColor", L.sunColor);
        pt->SetVec3("uSkyTop", env.skyTop);
        pt->SetVec3("uSkyHorizon", env.skyHorizon);
        pt->SetVec3("uGroundColor", env.groundColor);
        pt->SetFloat("uAmbient", env.ambientIntensity);
        pt->SetInt("uPointCount", L.pointCount);
        if (L.pointCount > 0) {
            pt->SetVec3Array("uPointPos", L.pointPos, L.pointCount);
            pt->SetVec3Array("uPointColor", L.pointColor, L.pointCount);
            pt->SetFloatArray("uPointRange", L.pointRange, L.pointCount);
        }
        pt->SetInt("uSpotCount", L.spotCount);
        if (L.spotCount > 0) {
            pt->SetVec3Array("uSpotPos", L.spotPos, L.spotCount);
            pt->SetVec3Array("uSpotDir", L.spotDir, L.spotCount);
            pt->SetVec3Array("uSpotColor", L.spotColor, L.spotCount);
            pt->SetFloatArray("uSpotRange", L.spotRange, L.spotCount);
            pt->SetFloatArray("uSpotCos", L.spotCos, L.spotCount);
        }
        pt->SetInt("uFog", env.fog ? 1 : 0);
        pt->SetVec3("uFogColor", env.fogColor);
        pt->SetFloat("uFogDensity", env.fogDensity);
        glBindVertexArray(R.vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glDepthFunc(GL_LEQUAL);
        R.samples++;
    }

    // ---- Copy color, IDs and depth into the HDR buffer for post processing --
    glBindFramebuffer(GL_READ_FRAMEBUFFER, R.fbo[R.current]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdr.Id());
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    GLenum c0[2] = { GL_COLOR_ATTACHMENT0, GL_NONE };
    glDrawBuffers(2, c0);
    glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    GLenum c1[2] = { GL_NONE, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, c1);
    glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    GLenum both[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glBindFramebuffer(GL_FRAMEBUFFER, hdr.Id());
    glDrawBuffers(2, both);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    Profiler::Stats().rtSamples = R.samples;
}

} // namespace eu
