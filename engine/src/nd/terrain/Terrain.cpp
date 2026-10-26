#include "nd/terrain/Terrain.h"
#include "nd/renderer/GL.h"
#include "nd/assets/AssetManager.h"
#include "nd/core/Profiler.h"
#include <cmath>

namespace nd {

// ------------------------------------------------------------ TerrainComponent helpers
float TerrainComponent::HeightAtLocal(float x, float z) const {
    if (heights.empty() || resolution < 2) return 0.0f;
    float half = size * 0.5f;
    float fx = (x + half) / size * (resolution - 1);
    float fz = (z + half) / size * (resolution - 1);
    fx = glm::clamp(fx, 0.0f, (float)(resolution - 1) - 0.001f);
    fz = glm::clamp(fz, 0.0f, (float)(resolution - 1) - 0.001f);
    int x0 = (int)fx, z0 = (int)fz;
    float tx = fx - x0, tz = fz - z0;
    auto H = [&](int ix, int iz) { return heights[(size_t)iz * resolution + ix]; };
    float a = H(x0, z0) * (1 - tx) + H(x0 + 1, z0) * tx;
    float b = H(x0, z0 + 1) * (1 - tx) + H(x0 + 1, z0 + 1) * tx;
    return a * (1 - tz) + b * tz;
}

vec3 TerrainComponent::NormalAtLocal(float x, float z) const {
    float e = size / (resolution - 1);
    float hl = HeightAtLocal(x - e, z), hr = HeightAtLocal(x + e, z);
    float hd = HeightAtLocal(x, z - e), hu = HeightAtLocal(x, z + e);
    return glm::normalize(vec3(hl - hr, 2.0f * e, hd - hu));
}

TerrainRuntime::~TerrainRuntime() {
    if (!g_GLContextAlive) return;
    if (grassVBO) glDeleteBuffers(1, &grassVBO);
    for (auto& t : trees) if (t.vbo) glDeleteBuffers(1, &t.vbo);
}

namespace TerrainSystem {

static float Hash(int x, int y, int seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / (float)0xFFFFFF;
}

float Noise2D(float x, float y, int seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float tx = x - xi, ty = y - yi;
    float u = tx * tx * (3 - 2 * tx), v = ty * ty * (3 - 2 * ty);
    float a = Hash(xi, yi, seed), b = Hash(xi + 1, yi, seed);
    float c = Hash(xi, yi + 1, seed), d = Hash(xi + 1, yi + 1, seed);
    return glm::mix(glm::mix(a, b, u), glm::mix(c, d, u), v);
}

float FBM(float x, float y, int seed, int octaves) {
    float sum = 0.0f, amp = 0.5f, freq = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; i++) {
        sum += Noise2D(x * freq, y * freq, seed + i * 31) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return sum / norm;
}

void Generate(TerrainComponent& t) {
    t.resolution = glm::clamp(t.resolution, 9, 1025);
    int res = t.resolution;
    t.heights.assign((size_t)res * res, 0.0f);
    float half = t.size * 0.5f;
    for (int z = 0; z < res; z++)
        for (int x = 0; x < res; x++) {
            float lx = -half + x * t.size / (res - 1);
            float lz = -half + z * t.size / (res - 1);
            float n = FBM(lx * t.noiseScale + 100.0f, lz * t.noiseScale + 100.0f, t.seed, t.octaves);
            float h = std::pow(glm::clamp((n - 0.28f) / 0.6f, 0.0f, 1.0f), 1.7f) * t.heightScale;
            // Calm, flat-ish area in the middle so there's somewhere to start.
            float r = std::sqrt(lx * lx + lz * lz);
            float center = glm::smoothstep(t.size * 0.08f, t.size * 0.2f, r);
            h = glm::mix(1.6f + n * 0.4f, h, center);
            // Drop the edges a little (island feel)
            float edge = glm::smoothstep(half * 0.75f, half, std::max(std::abs(lx), std::abs(lz)));
            h = glm::mix(h, -2.0f, edge);
            t.heights[(size_t)z * res + x] = h;
        }
    t.dirty = true;
    t.foliageDirty = true;
}

void Update(TerrainComponent& t) {
    if (!t.runtime) { t.runtime = MakeRef<TerrainRuntime>(); t.dirty = true; t.foliageDirty = true; }
    int res = t.resolution;
    if (t.heights.size() != (size_t)res * res) Generate(t);
    if (t.dirty) {
        ND_PROFILE_SCOPE("TerrainMesh");
        float half = t.size * 0.5f, step = t.size / (res - 1);
        std::vector<Vertex> verts((size_t)res * res);
        for (int z = 0; z < res; z++)
            for (int x = 0; x < res; x++) {
                Vertex& v = verts[(size_t)z * res + x];
                float lx = -half + x * step, lz = -half + z * step;
                v.position = { lx, t.heights[(size_t)z * res + x], lz };
                auto H = [&](int ix, int iz) {
                    ix = glm::clamp(ix, 0, res - 1); iz = glm::clamp(iz, 0, res - 1);
                    return t.heights[(size_t)iz * res + ix];
                };
                v.normal = glm::normalize(vec3(H(x - 1, z) - H(x + 1, z), 2.0f * step, H(x, z - 1) - H(x, z + 1)));
                v.uv = { (float)x / (res - 1), (float)z / (res - 1) };
            }
        if (t.runtime->mesh && t.runtime->builtResolution == res) {
            t.runtime->mesh->Update(verts);
        } else {
            std::vector<uint32_t> idx;
            idx.reserve((size_t)(res - 1) * (res - 1) * 6);
            for (int z = 0; z < res - 1; z++)
                for (int x = 0; x < res - 1; x++) {
                    uint32_t a = z * res + x, b = a + res;
                    idx.insert(idx.end(), { a, b, a + 1, a + 1, b, b + 1 });
                }
            t.runtime->mesh = MakeRef<Mesh>(verts, idx, true);
            t.runtime->builtResolution = res;
        }
        t.dirty = false;
    }
    if (t.foliageDirty) {
        BuildFoliage(t);
        t.foliageDirty = false;
    }
}

static Ref<Mesh> MakeGrassClump() {
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    const int segs = 4;
    for (int blade = 0; blade < 3; blade++) {
        float yaw = blade * PI / 3.0f + 0.3f;
        vec3 dir(std::cos(yaw), 0, std::sin(yaw));
        vec3 lean(-dir.z, 0, dir.x);
        float offset = (blade - 1) * 0.12f;
        uint32_t base = (uint32_t)v.size();
        for (int s = 0; s <= segs; s++) {
            float t = (float)s / segs;
            float w = 0.5f * (1.0f - t * 0.9f);
            vec3 c = lean * (offset + t * t * 0.25f) + vec3(0, t, 0);
            for (int side = 0; side < 2; side++) {
                Vertex vert;
                vert.position = c + dir * (side ? w : -w) * 0.25f;
                vert.normal = { 0, 1, 0 };
                vert.uv = { (float)side, t };
                v.push_back(vert);
            }
        }
        for (int s = 0; s < segs; s++) {
            uint32_t a = base + s * 2;
            idx.insert(idx.end(), { a, a + 1, a + 2, a + 1, a + 3, a + 2 });
        }
    }
    return MakeRef<Mesh>(v, idx);
}

static void AppendTransformed(std::vector<Vertex>& out, std::vector<uint32_t>& idx, const Mesh& m, const mat4& xf) {
    uint32_t base = (uint32_t)out.size();
    mat3 nm = glm::transpose(glm::inverse(mat3(xf)));
    for (auto v : m.Vertices()) {
        v.position = vec3(xf * vec4(v.position, 1.0f));
        v.normal = glm::normalize(nm * v.normal);
        out.push_back(v);
    }
    for (auto i : m.Indices()) idx.push_back(base + i);
}

static uint32_t MakeInstanceVBO(Mesh& mesh, const std::vector<mat4>& data) {
    uint32_t vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(mat4), data.data(), GL_STATIC_DRAW);
    mesh.AttachInstanceBuffer(vbo);
    return vbo;
}

void BuildFoliage(TerrainComponent& t) {
    ND_PROFILE_SCOPE("Foliage");
    auto& rt = *t.runtime;
    if (rt.grassVBO) { glDeleteBuffers(1, &rt.grassVBO); rt.grassVBO = 0; }
    for (auto& tb : rt.trees) if (tb.vbo) glDeleteBuffers(1, &tb.vbo);
    rt.trees.clear();
    rt.grassCount = 0;

    uint32_t rng = (uint32_t)t.seed * 9781u + 1u;
    auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
    float half = t.size * 0.5f;

    // Grass
    if (t.grass) {
        if (!rt.grassMesh) rt.grassMesh = MakeGrassClump();
        int target = std::min(80000, (int)(t.size * t.size * t.grassDensity));
        std::vector<mat4> inst;
        inst.reserve(target);
        for (int i = 0; i < target; i++) {
            float x = (rnd() * 2 - 1) * half, z = (rnd() * 2 - 1) * half;
            float h = t.HeightAtLocal(x, z);
            if (h < t.grassMinHeight || h > t.snowHeight - 1.5f) continue;
            if (t.NormalAtLocal(x, z).y < 0.86f) continue;
            mat4 m(0.0f);
            m[0] = vec4(x, h - 0.05f, z, 0.45f + rnd() * 0.5f);
            m[1] = vec4(rnd() * 2.0f * PI, rnd(), 0.8f + rnd() * 0.6f, 0.0f);
            inst.push_back(m);
        }
        if (!inst.empty()) {
            rt.grassVBO = MakeInstanceVBO(*rt.grassMesh, inst);
            rt.grassCount = (int)inst.size();
        }
    }

    // Trees
    if (t.treeCount > 0) {
        std::vector<mat4> xforms;
        int attempts = 0;
        while ((int)xforms.size() < t.treeCount && attempts++ < t.treeCount * 30) {
            float x = (rnd() * 2 - 1) * half * 0.92f, z = (rnd() * 2 - 1) * half * 0.92f;
            if (x * x + z * z < (t.size * 0.12f) * (t.size * 0.12f)) continue;   // keep the middle clear
            float h = t.HeightAtLocal(x, z);
            if (h < t.sandHeight + 0.6f || h > t.snowHeight - 2.0f) continue;
            if (t.NormalAtLocal(x, z).y < 0.82f) continue;
            float s = 0.8f + rnd() * 0.7f;
            mat4 m = glm::translate(mat4(1.0f), vec3(x, h - 0.1f, z)) *
                     glm::rotate(mat4(1.0f), rnd() * 2.0f * PI, vec3(0, 1, 0)) * glm::scale(mat4(1.0f), vec3(s));
            xforms.push_back(m);
        }
        if (!xforms.empty()) {
            Ref<Model> model = t.treeModel.empty() ? nullptr : AssetManager::GetModel(t.treeModel);
            if (model && !model->meshes.empty()) {
                for (auto& sm : model->meshes) {
                    TerrainRuntime::TreeBatch tb;
                    tb.mesh = MakeRef<Mesh>(sm.mesh->Vertices(), sm.mesh->Indices());
                    if (sm.material >= 0 && sm.material < (int)model->materials.size()) {
                        tb.color = model->materials[sm.material].baseColor;
                        tb.texture = model->materials[sm.material].albedo;
                    }
                    tb.vbo = MakeInstanceVBO(*tb.mesh, xforms);
                    tb.count = (int)xforms.size();
                    rt.trees.push_back(tb);
                }
            } else {
                // Built-in stylised tree: trunk + three stacked cones.
                std::vector<Vertex> tv, cv;
                std::vector<uint32_t> ti, ci;
                AppendTransformed(tv, ti, *Primitives::Cylinder(), glm::translate(mat4(1.0f), vec3(0, 1.1f, 0)) * glm::scale(mat4(1.0f), vec3(0.32f, 2.2f, 0.32f)));
                AppendTransformed(cv, ci, *Primitives::Cone(), glm::translate(mat4(1.0f), vec3(0, 2.7f, 0)) * glm::scale(mat4(1.0f), vec3(2.6f, 2.2f, 2.6f)));
                AppendTransformed(cv, ci, *Primitives::Cone(), glm::translate(mat4(1.0f), vec3(0, 3.7f, 0)) * glm::scale(mat4(1.0f), vec3(2.0f, 2.0f, 2.0f)));
                AppendTransformed(cv, ci, *Primitives::Cone(), glm::translate(mat4(1.0f), vec3(0, 4.6f, 0)) * glm::scale(mat4(1.0f), vec3(1.3f, 1.6f, 1.3f)));
                TerrainRuntime::TreeBatch trunk, canopy;
                trunk.mesh = MakeRef<Mesh>(tv, ti);
                trunk.color = { 0.42f, 0.29f, 0.19f, 1.0f };
                canopy.mesh = MakeRef<Mesh>(cv, ci);
                canopy.color = { 0.20f, 0.42f, 0.22f, 1.0f };
                for (auto* tb : { &trunk, &canopy }) {
                    tb->vbo = MakeInstanceVBO(*tb->mesh, xforms);
                    tb->count = (int)xforms.size();
                    rt.trees.push_back(*tb);
                }
            }
        }
    }
}

void Sculpt(TerrainComponent& t, const vec2& p, float radius, float strength, SculptMode mode, float flattenHeight) {
    int res = t.resolution;
    if (t.heights.size() != (size_t)res * res || radius <= 0.0f) return;
    float half = t.size * 0.5f, step = t.size / (res - 1);
    int cx = (int)((p.x + half) / step), cz = (int)((p.y + half) / step);
    int r = (int)std::ceil(radius / step) + 1;
    std::vector<float> src = t.heights;
    for (int z = std::max(0, cz - r); z <= std::min(res - 1, cz + r); z++)
        for (int x = std::max(0, cx - r); x <= std::min(res - 1, cx + r); x++) {
            float lx = -half + x * step, lz = -half + z * step;
            float d = glm::distance(vec2(lx, lz), p);
            if (d > radius) continue;
            float f = 1.0f - glm::smoothstep(0.0f, 1.0f, d / radius);
            float& h = t.heights[(size_t)z * res + x];
            switch (mode) {
            case SculptMode::Raise: h += strength * f; break;
            case SculptMode::Lower: h -= strength * f; break;
            case SculptMode::Smooth: {
                float sum = 0.0f; int n = 0;
                for (int dz = -1; dz <= 1; dz++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int ix = glm::clamp(x + dx, 0, res - 1), iz = glm::clamp(z + dz, 0, res - 1);
                        sum += src[(size_t)iz * res + ix]; n++;
                    }
                h = glm::mix(h, sum / n, glm::clamp(strength * f * 4.0f, 0.0f, 1.0f));
                break;
            }
            case SculptMode::Flatten: h = glm::mix(h, flattenHeight, glm::clamp(strength * f * 2.0f, 0.0f, 1.0f)); break;
            }
        }
    t.dirty = true;
}

} // namespace TerrainSystem
} // namespace nd
