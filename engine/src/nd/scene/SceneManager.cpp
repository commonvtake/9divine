#include "nd/scene/SceneManager.h"
#include "nd/scene/SceneSerializer.h"
#include "nd/audio/Audio.h"
#include "nd/core/Log.h"

namespace nd {

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
    return scene;
}

bool SceneManager::Update() {
    if (s_Pending.empty()) return false;
    std::string path = s_Pending;
    auto callback = std::move(s_OnLoaded);
    s_Pending.clear();
    s_OnLoaded = nullptr;

    auto scene = LoadNow(path);
    if (!scene) {
        ND_ERROR("Scene switch to '{}' failed", path);
        return false;
    }
    bool wasRunning = s_Active && s_Active->IsRunning();
    if (s_Active && wasRunning) s_Active->OnRuntimeStop();
    s_Active = scene;
    if (wasRunning) s_Active->OnRuntimeStart();
    if (callback) callback(*s_Active);
    ND_INFO("Switched to scene {}", path);
    return true;
}

} // namespace nd
