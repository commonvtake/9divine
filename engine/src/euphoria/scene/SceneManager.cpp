#include "euphoria/scene/SceneManager.h"
#include "euphoria/scene/SceneSerializer.h"
#include "euphoria/assets/AssetManager.h"
#include "euphoria/renderer/Renderer.h"
#include "euphoria/core/Profiler.h"
#include "euphoria/audio/Audio.h"
#include "euphoria/core/Log.h"

namespace eu {

Ref<Scene> SceneManager::s_Active;
std::string SceneManager::s_Pending;
std::function<void(Scene&)> SceneManager::s_OnLoaded;
std::function<void()> SceneManager::onQuitRequested;

void SceneManager::LoadScene(const std::string& path, std::function<void(Scene&)> onLoaded) {
    s_Pending = path;
    s_OnLoaded = std::move(onLoaded);
}

void SceneManager::ReloadActive(std::function<void(Scene&)> onLoaded) {
    if (s_Active && !s_Active->path.empty()) LoadScene(s_Active->path, std::move(onLoaded));
}

Ref<Scene> SceneManager::LoadNow(const std::string& path) {
    auto scene = MakeRef<Scene>();
    SceneSerializer s(*scene);
    if (!s.Load(path)) return nullptr;
    Preload(*scene);
    return scene;
}

// Loads every model and texture the scene references now, while the screen is already
// switching, instead of the first time each one comes into view (a mid-game hitch).
void SceneManager::Preload(Scene& scene) {
    EU_PROFILE_SCOPE("Preload");
    auto& reg = scene.Reg();
    reg.View<MeshRendererComponent>([&](EntityId, MeshRendererComponent& mr) {
        if (!mr.mesh.empty() && (!mr.model || mr.loadedMesh != mr.mesh)) {
            mr.model = AssetManager::GetModel(mr.mesh);
            mr.loadedMesh = mr.mesh;
            mr.loadedVersion = AssetManager::Version();
        }
        for (const std::string* t : { &mr.texture, &mr.normalMap, &mr.metallicRoughnessMap, &mr.emissiveMap })
            if (!t->empty()) AssetManager::GetTexture(*t);
    });
    reg.View<UIElementComponent>([&](EntityId, UIElementComponent& u) { if (!u.image.empty()) AssetManager::GetTexture(u.image); });
}

bool SceneManager::Update() {
    if (s_Pending.empty()) return false;
    std::string path = s_Pending;
    auto callback = std::move(s_OnLoaded);
    s_Pending.clear();
    s_OnLoaded = nullptr;

    auto scene = LoadNow(path);
    if (!scene) {
        EU_ERROR("Scene switch to '{}' failed", path);
        return false;
    }
    bool wasRunning = s_Active && s_Active->IsRunning();
    if (s_Active && wasRunning) s_Active->OnRuntimeStop();
    s_Active = scene;
    if (wasRunning) s_Active->OnRuntimeStart();
    if (callback) callback(*s_Active);
    EU_INFO("Switched to scene {}", path);
    return true;
}

} // namespace eu
