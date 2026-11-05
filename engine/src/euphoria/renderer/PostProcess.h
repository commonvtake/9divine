#pragma once
#include "euphoria/core/Base.h"

namespace eu {

struct SceneEnvironment;
struct RenderSettings;

// A single-texture render target used by the post stack (and by the renderer for its HDR
// scene buffer). Owns its FBO and texture; Resize() is a no-op when the size is unchanged.
struct RenderTarget {
    uint32_t fbo = 0, color = 0;
    int width = 0, height = 0;
    uint32_t internalFormat = 0;   // GL enum, e.g. GL_RGBA16F
    void Resize(int w, int h, uint32_t internalFormat, bool linearFilter = true);
    void Bind() const;
    void Release();
};

// HDR -> display. Runs after the 3D scene has been drawn into a linear HDR color buffer:
//   1. SSAO from the depth buffer (half resolution) + depth-aware blur
//   2. Bloom: threshold, 13-tap downsample chain, tent-filter upsample
//   3. Composite: AO, bloom, exposure, ACES filmic tone mapping, white balance, contrast,
//      saturation, vignette, sRGB gamma (luma in alpha)
//   4. FXAA into the destination framebuffer's first color attachment
class PostProcess {
public:
    struct Inputs {
        uint32_t hdrColor = 0;        // RGBA16F scene
        uint32_t depth = 0;           // depth texture of the scene
        int width = 0, height = 0;
        mat4 projection{ 1.0f };
        float nearClip = 0.1f, farClip = 800.0f;
        uint32_t destFbo = 0;         // framebuffer to write the final image to (attachment 0)
    };
    static void Init();
    static void Shutdown();
    static void Run(const Inputs& in, const SceneEnvironment& env, const RenderSettings& settings);

    // Debug views for the editor
    static uint32_t AOTexture();
    static uint32_t BloomTexture();
};

} // namespace eu
