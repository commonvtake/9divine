#pragma once
#include "nd/core/Base.h"
#include "nd/scene/Components.h"
#include "nd/renderer/Mesh.h"
#include "nd/renderer/Texture.h"

namespace nd {

class TerrainRuntime {
public:
    ~TerrainRuntime();
    Ref<Mesh> mesh;
    int builtResolution = 0;

    // Grass: one clump mesh drawn instanced
    Ref<Mesh> grassMesh;
    uint32_t grassVBO = 0;
    int grassCount = 0;

    // Trees: one instanced batch per tree part / material
    struct TreeBatch {
        Ref<Mesh> mesh;
        vec4 color{ 1.0f };
        Ref<Texture2D> texture;
        uint32_t vbo = 0;
        int count = 0;
    };
    std::vector<TreeBatch> trees;
};

enum class SculptMode { Raise, Lower, Smooth, Flatten };

namespace TerrainSystem {
// Fills heights with fractal noise (seeded).
void Generate(TerrainComponent& t);
// Creates/updates the GPU mesh if the terrain is dirty.
void Update(TerrainComponent& t);
// Regenerates grass and trees scattering.
void BuildFoliage(TerrainComponent& t);
// Brush editing in local (terrain) coordinates.
void Sculpt(TerrainComponent& t, const vec2& localXZ, float radius, float strength, SculptMode mode, float flattenHeight = 0.0f);
// Value noise helpers (also handy for gameplay).
float Noise2D(float x, float y, int seed);
float FBM(float x, float y, int seed, int octaves);
}

} // namespace nd
