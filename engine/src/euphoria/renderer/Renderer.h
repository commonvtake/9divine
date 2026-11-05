#pragma once
#include "euphoria/core/Base.h"
#include "euphoria/scene/ECS.h"

namespace eu {

class Scene;
class Framebuffer;
class Shader;

struct RenderCamera {
    mat4 view{ 1.0f };
    mat4 projection{ 1.0f };
    vec3 position{ 0.0f };
    float nearClip = 0.1f, farClip = 800.0f;
};

struct RenderOptions {
    bool showColliders = false;
    bool showNavGrid = false;
    bool showLightGizmos = false;
    bool editorMode = false;
    EntityId selected = NullEntity;
};

struct RenderSettings {
    int shadowMapSize = 2048;          // per cascade
    int shadowCascades = 3;            // 1-4: more = sharper shadows far away
    float shadowDistance = 150.0f;
    float shadowSoftness = 1.5f;       // PCF filter radius in texels
    bool ssao = true;
    bool bloom = true;
    bool fxaa = true;
    bool reflections = true;           // live reflection cubemap around the camera
    int reflectionSize = 128;          // cubemap face resolution
    // Ray tracing (path traced instead of rasterized; accumulates while nothing moves)
    bool rayTraced = false;
    int rtMaxBounces = 4;
    int rtMaxSamples = 4096;           // stop refining after this many samples per pixel
    float grassDistance = 70.0f;
    bool shadows = true;
    bool grass = true;
    bool batching = true;           // draw identical static meshes with one instanced call
};

class Renderer {
public:
    static void Init();
    static void Shutdown();

    // Draws a scene into `target` (expects attachments: RGBA8 color, R32I entity ids, depth).
    static void RenderScene(Scene& scene, const RenderCamera& camera, Framebuffer& target, const RenderOptions& options = {});

    static RenderSettings& Settings() { return s_Settings; }
    static Ref<Shader> GetShader(const std::string& file, const std::vector<std::string>& defines = {});
    static void ReloadShaders();
    static void Prewarm();          // compile all shader variants now (done at Init)

    // Builds a camera from a CameraComponent-bearing entity's world matrix.
    static RenderCamera MakeCamera(const mat4& world, float fovDeg, float aspect, float nearClip, float farClip, bool ortho = false, float orthoSize = 10.0f);

private:
    static RenderSettings s_Settings;
};

} // namespace eu
