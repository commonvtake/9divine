#pragma once
#include "euphoria/core/Base.h"
#include "euphoria/scene/ECS.h"

namespace eu {

class Scene;
class Framebuffer;
struct RenderCamera;
struct AABB;

// Bounding volume hierarchy over a triangle soup, built with binned SAH (12 bins, leaves of
// at most 4 triangles). Nodes are stored depth-first; an interior node's children are at
// `leftFirst` and `leftFirst + 1`; a leaf covers triangles [leftFirst, leftFirst + count).
struct BVH {
    struct Node {
        vec3 bmin;
        uint32_t leftFirst = 0;
        vec3 bmax;
        uint32_t count = 0;   // 0 = interior node
    };
    std::vector<Node> nodes;
    std::vector<uint32_t> triIndex;   // triangle order after building

    // centroids/bounds: one entry per triangle
    void Build(const std::vector<vec3>& v0, const std::vector<vec3>& v1, const std::vector<vec3>& v2);
    // Reference closest-hit query (CPU), used by tests: returns hit distance or -1.
    float Intersect(const vec3& origin, const vec3& dir, const std::vector<vec3>& v0, const std::vector<vec3>& v1,
                    const std::vector<vec3>& v2, uint32_t* triOut = nullptr) const;
    int Depth() const;
};

// Progressive GPU path tracer. Renders the scene's triangles (meshes incl. skinned poses,
// terrain, trees, water) with next-event estimation for the sun and point/spot lights,
// GGX + diffuse importance sampling and Russian roulette, accumulating one sample per pixel
// per frame. The camera standing still (and nothing static changing) lets the image converge;
// animated characters keep a short moving average so they don't smear forever.
// Grass, particles and outlines are raster-only effects and are not traced.
class RayTracer {
public:
    struct Lights {
        bool hasSun = false;
        vec3 sunDir{ 0, -1, 0 }, sunColor{ 0.0f };
        int pointCount = 0;
        vec3 pointPos[8], pointColor[8];
        float pointRange[8];
        int spotCount = 0;
        vec3 spotPos[4], spotDir[4], spotColor[4];
        float spotRange[4], spotCos[4];
    };

    static void Init();
    static void Shutdown();
    // Traces one more sample per pixel and writes the converged-so-far HDR image, entity IDs
    // and depth into `hdr` (attachments: RGBA16F, R32I, depth).
    static void Render(Scene& scene, const RenderCamera& cam, const Lights& lights, Framebuffer& hdr);
    static void Reset();
    static int SampleCount();
    static size_t TriangleCount();
};

} // namespace eu
