#pragma once
#include "nd/core/Base.h"
#include "nd/scene/ECS.h"

namespace nd {

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
    int shadowMapSize = 2048;
    float shadowDistance = 70.0f;
    float grassDistance = 70.0f;
    bool shadows = true;
    bool grass = true;
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

    // Builds a camera from a CameraComponent-bearing entity's world matrix.
    static RenderCamera MakeCamera(const mat4& world, float fovDeg, float aspect, float nearClip, float farClip, bool ortho = false, float orthoSize = 10.0f);

private:
    static RenderSettings s_Settings;
};

} // namespace nd
