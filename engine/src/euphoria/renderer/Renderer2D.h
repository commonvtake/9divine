#pragma once
// Flat 2D rendering: sprites (SpriteRendererComponent) and Tiled maps (TilemapComponent),
// drawn as sorted, batched textured quads in the XY plane. Used by Renderer::RenderScene
// when the scene's environment has mode2D on.
//
// Sorting: by order (layer), then - for y-sorted things in the same order - by the bottom
// edge's height (lower on screen = in front), then by creation.
//
// Pixel perfect (SceneEnvironment::pixelPerfect, in the game - not the editor's scene view):
// the scene is drawn at a low resolution near referenceResolution with the camera snapped to
// whole pixels, then scaled up by the largest whole number that fits the window. Every art
// pixel becomes the same number of screen pixels, and nothing shimmers as the camera moves.
#include "euphoria/core/Base.h"
#include "euphoria/renderer/Renderer.h"

namespace eu {

struct SceneEnvironment;

class Renderer2D {
public:
    static void Init();
    static void Shutdown();

    // Returns the camera actually used (pixel perfect: snapped), for overlays drawn afterwards.
    static RenderCamera Render(Scene& scene, const RenderCamera& camera, Framebuffer& target, const RenderOptions& options);

    // The camera the game is actually drawn with for a `width` x `height` viewport (pixel
    // perfect: snapped, low-res projection). Scripts converting between screen and world
    // positions should use this one.
    struct View {
        RenderCamera camera;
        int lowWidth = 0, lowHeight = 0;    // render size (pixel perfect) or the viewport size
        int scale = 1;                      // screen pixels per art pixel (pixel perfect)
        bool lowRes = false;
    };
    static View ComputeView(const SceneEnvironment& env, const RenderCamera& camera, int width, int height, bool editor);

    struct Stats { int quads = 0, drawCalls = 0; };
    static Stats LastStats();
};

} // namespace eu
